#include "Platform.h"

// Fallbacks for platforms without a native implementation (the wheel then uses a plain backdrop).
void Platform::captureScreen (juce::Rectangle<int>, std::function<void (juce::Image)> cb)
{
    juce::MessageManager::callAsync ([cb] { cb ({}); });
}

void Platform::playWav (int, const juce::MemoryBlock&) {}
