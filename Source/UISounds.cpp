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
                if (t < 0.009) env = vol * t / 0.009;
                else           env = vol * std::pow (0.0001 / vol, (t - 0.009) / juce::jmax (0.001, dur - 0.009));

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

        // short noise burst through a band-pass, instant attack + exponential decay
        void burst (double start, double dur, double vol, double fc, double q, int seed)
        {
            ensure (start + dur + 0.02);
            juce::Random rng (seed);
            const auto s0 = (size_t) (start * sr), len = (size_t) (dur * sr);
            const double w0 = juce::MathConstants<double>::twoPi * fc / sr, alpha = std::sin (w0) / (2.0 * q);
            const double b0 = alpha, b2 = -alpha, a0 = 1.0 + alpha, a1 = -2.0 * std::cos (w0), a2 = 1.0 - alpha;
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (size_t i = 0; i < len; ++i)
            {
                const double t = (double) i / sr;
                const double x = rng.nextDouble() * 2.0 - 1.0;
                const double y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                const double atk = juce::jmin (1.0, t / 0.0015);
                data[s0 + i] += (float) (y * vol * 3.0 * atk * std::exp (-t / (dur * 0.25)));
            }
        }

        // mechanical "thock": bright click + low body thump + soft key-release tick
        // soft, felted "thock": muted click + low body + gentle release
        void keyClick (double pitch, int seed, double release = 0.042)
        {
            burst (0.0,     0.009, 0.05, 1900.0 * pitch, 0.7, seed);          // muted switch click
            burst (0.0,     0.030, 0.05,  650.0 * pitch, 1.0, seed + 1);      // keycap body
            tone (150.0 * pitch, 0.045, 0.045, false, 95.0 * pitch);          // soft bottom-out
            burst (release, 0.008, 0.015, 1400.0 * pitch, 0.7, seed + 2);     // release
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

            // master: -4.5 dB and a gentle low-pass (~4.5 kHz) so nothing sounds harsh
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 4500.0f / (float) sr);
            float lp = 0.0f;
            for (auto s : data)
            {
                lp += a * (s - lp);
                out.writeShort ((short) juce::jlimit (-32767, 32767, (int) (lp * 0.6f * 32767.0f)));
            }

            return out.getMemoryBlock();
        }
    };

    std::vector<juce::MemoryBlock> buildAll()
    {
        std::vector<juce::MemoryBlock> wavs ((size_t) UISounds::numSounds);
        auto make = [&] (UISounds::Sound s, auto&& fn) { Synth sy; fn (sy); wavs[(size_t) s] = sy.toWav(); };

        make (UISounds::tick,   [] (Synth& s) { s.tone (2100, .03, .03); s.tone (1050, .03, .015); });
        make (UISounds::select, [] (Synth& s) { s.tone (520, .18, .07, false, 780); s.tone (1040, .14, .02, false, 1560, .01); });
        make (UISounds::folder, [] (Synth& s) { s.tone (440, .11, .05, false, 590); s.tone (660, .13, .035, false, 0, .06); });
        make (UISounds::back,   [] (Synth& s) { s.tone (590, .11, .045, false, 440); });
        make (UISounds::open,   [] (Synth& s) { s.noise (.24, .045, 300, 1400); });
        make (UISounds::close,  [] (Synth& s) { s.noise (.22, .035, 1300, 300); });
        make (UISounds::search, [] (Synth& s) { s.tone (740, .09, .035, false, 1100); s.noise (.14, .015, 900, 1800); });
        make (UISounds::pin,    [] (Synth& s) { s.tone (740, .11, .05); s.tone (1100, .18, .045, false, 0, .08); });
        make (UISounds::clear,  [] (Synth& s) { s.tone (420, .15, .045, false, 300); });
        make (UISounds::toggle, [] (Synth& s) { s.tone (1100, .05, .03); });
        make (UISounds::key1,     [] (Synth& s) { s.keyClick (1.00, 11); });
        make (UISounds::key2,     [] (Synth& s) { s.keyClick (0.94, 23, 0.046); });
        make (UISounds::key3,     [] (Synth& s) { s.keyClick (1.07, 37, 0.039); });
        make (UISounds::key4,     [] (Synth& s) { s.keyClick (0.98, 41, 0.044); });
        make (UISounds::keySpace, [] (Synth& s) { s.keyClick (0.78, 53, 0.055); s.burst (0.0, 0.05, 0.02, 420.0, 0.8, 54); });
        make (UISounds::keyBack,  [] (Synth& s) { s.keyClick (0.86, 67, 0.050); });
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

void UISounds::playKey (juce::juce_wchar typed, bool isBackspace)
{
    static juce::Random rng;
    static int last = -1;

    if (isBackspace)      { play (keyBack); return; }
    if (typed == ' ')     { play (keySpace); return; }

    int v = rng.nextInt (4);
    if (v == last) v = (v + 1) % 4;   // never the same click twice in a row
    last = v;
    play ((Sound) (key1 + v));
}
