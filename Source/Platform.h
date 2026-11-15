#pragma once
#include <JuceHeader.h>

namespace Platform
{
    /** Grabs what is currently on screen in the given (logical) area.
        The callback always fires on the message thread; the image is invalid if the
        capture failed or screen-recording permission was refused. */
    void captureScreen (juce::Rectangle<int> logicalArea, std::function<void (juce::Image)> callback);
}

//==============================================================================
namespace UISounds
{
    enum Sound { tick, select, folder, back, open, close, search, pin, clear, toggle,
                 key1, key2, key3, key4, keySpace, keyBack, numSounds };

    /** Renders every sound once (call on the message thread before audio uses them). */
    void init();

    /** Mono 44.1 kHz samples of a sound (empty if not initialised). */
    const std::vector<float>& samples (Sound);

    /** Returns the sound to play, or numSounds if it should be skipped (hover ticks are rate-limited). */
    Sound filter (Sound);

    /** ASMR keyboard click for the search field (random variant so fast typing sounds natural). */
    Sound keyFor (juce::juce_wchar typed, bool isBackspace = false);
}
