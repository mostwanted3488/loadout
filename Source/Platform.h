#pragma once
#include <JuceHeader.h>

namespace Platform
{
    /** Grabs what is currently on screen in the given (logical) area.
        The callback always fires on the message thread; the image is invalid if the
        capture failed or screen-recording permission was refused. */
    void captureScreen (juce::Rectangle<int> logicalArea, std::function<void (juce::Image)> callback);

    /** Plays a WAV held in memory through the OS (never through the DAW's audio path). */
    void playWav (int soundIndex, const juce::MemoryBlock& wav);
}

//==============================================================================
namespace UISounds
{
    enum Sound { tick, select, folder, back, open, close, search, pin, clear, toggle,
                 key1, key2, key3, key4, keySpace, keyBack, numSounds };
    void play (Sound);

    /** ASMR keyboard click for the search field (random variant so fast typing sounds natural). */
    void playKey (juce::juce_wchar typed, bool isBackspace = false);
}
