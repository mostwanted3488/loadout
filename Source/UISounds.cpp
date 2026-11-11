#include "Platform.h"

// Tiny synth that renders the wheel's UI sounds into in-memory WAVs once,
// then hands them to the OS player (so they never end up in the DAW's audio).
namespace
{
    constexpr double sr = 44100.0;

    struct Synth
    {
        std::vector<float> data;

        void ensure (double seconds)
        {
            const auto n = (size_t) (seconds * sr) + 64;
            if (data.size() < n) data.resize (n, 0.0f);
        }

        void tone (double f, double dur, double vol, bool triangle = false, double f2 = 0.0, double delay = 0.0)
        {
            ensure (delay + dur + 0.03);
            const auto start = (size_t) (delay * sr);
            const auto len   = (size_t) (dur * sr);
            double phase = 0.0;

            for (size_t i = 0; i < len; ++i)
            {
                const double t = (double) i / sr;
                double freq = f;
                if (f2 > 0.0)
                    freq = f * std::pow (f2 / f, juce::jmin (1.0, t / (dur * 0.7)));

                phase += freq / sr;
                phase -= std::floor (phase);

                const double osc = triangle ? (4.0 * std::abs (phase - 0.5) - 1.0)
                                            : std::sin (phase * juce::MathConstants<double>::twoPi);

                double env;
                if (t < 0.004) env = vol * t / 0.004;
                else           env = vol * std::pow (0.0001 / vol, (t - 0.004) / juce::jmax (0.001, dur - 0.004));

                data[start + i] += (float) (osc * env);
            }
        }

        void noise (double dur, double vol, double f0, double f1)
        {
            ensure (dur + 0.03);
            juce::Random rng (1234);
            const auto len = (size_t) (dur * sr);
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;

            for (size_t i = 0; i < len; ++i)
            {
                const double t = (double) i / sr;
                const double fc = f0 * std::pow (f1 / f0, t / dur);
                const double q = 1.6;
                const double w0 = juce::MathConstants<double>::twoPi * fc / sr;
                const double alpha = std::sin (w0) / (2.0 * q);
                const double b0 = alpha, b2 = -alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;

                const double x = rng.nextDouble() * 2.0 - 1.0;
                const double y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1; x1 = x; y2 = y1; y1 = y;

                double env;
                const double peak = dur * 0.3;
                if (t < peak) env = vol * t / peak;
                else          env = vol * std::pow (0.0001 / vol, (t - peak) / (dur - peak));

                data[i] += (float) (y * env * 2.5);
            }
        }

        juce::MemoryBlock toWav() const
        {
            juce::MemoryOutputStream out;
            const auto numSamples = (uint32_t) data.size();
            const uint32_t dataBytes = numSamples * 2;

            out.write ("RIFF", 4);  out.writeInt ((int) (36 + dataBytes));  out.write ("WAVE", 4);
            out.write ("fmt ", 4);  out.writeInt (16);  out.writeShort (1);  out.writeShort (1);
            out.writeInt ((int) sr); out.writeInt ((int) sr * 2);  out.writeShort (2);  out.writeShort (16);
            out.write ("data", 4);  out.writeInt ((int) dataBytes);

            for (auto s : data)
                out.writeShort ((short) juce::jlimit (-32767, 32767, (int) (s * 32767.0f)));

            return out.getMemoryBlock();
        }
    };

    std::vector<juce::MemoryBlock> buildAll()
    {
        std::vector<juce::MemoryBlock> wavs ((size_t) UISounds::numSounds);
        auto make = [&] (UISounds::Sound s, auto&& fn) { Synth sy; fn (sy); wavs[(size_t) s] = sy.toWav(); };

        make (UISounds::tick,   [] (Synth& s) { s.tone (3400, .035, .045); s.tone (1700, .03, .025, true); });
        make (UISounds::select, [] (Synth& s) { s.tone (620, .16, .11, false, 980); s.tone (1240, .12, .04, false, 1960, .01); });
        make (UISounds::folder, [] (Synth& s) { s.tone (520, .1, .07, true, 700); s.tone (780, .12, .05, false, 0, .06); });
        make (UISounds::back,   [] (Synth& s) { s.tone (700, .1, .06, true, 500); });
        make (UISounds::open,   [] (Synth& s) { s.noise (.22, .09, 500, 2600); });
        make (UISounds::close,  [] (Synth& s) { s.noise (.2, .07, 2400, 450); });
        make (UISounds::search, [] (Synth& s) { s.tone (880, .08, .05, false, 1320); s.noise (.14, .04, 1800, 3200); });
        make (UISounds::pin,    [] (Synth& s) { s.tone (880, .1, .08); s.tone (1320, .16, .08, false, 0, .08); });
        make (UISounds::clear,  [] (Synth& s) { s.tone (440, .14, .07, true, 300); });
        make (UISounds::toggle, [] (Synth& s) { s.tone (1500, .04, .05); });
        return wavs;
    }
}

void UISounds::play (Sound s)
{
    static const auto wavs = buildAll();
    static double lastTick = 0.0;

    if (s == tick)
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastTick < 28.0)
            return;
        lastTick = now;
    }

    if (juce::isPositiveAndBelow ((int) s, (int) numSounds))
        Platform::playWav ((int) s, wavs[(size_t) s]);
}
