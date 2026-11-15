#include "Platform.h"

// Fallbacks for platforms without a native implementation (the wheel then uses a plain backdrop).
void Platform::captureScreen (juce::Rectangle<int>, std::function<void (juce::Image)> cb)
{
    juce::MessageManager::callAsync ([cb] { cb ({}); });
}

// Not done on this platform: FL's plugin windows live inside its main window here,
// so the LOADOUT bar simply stays visible.
bool Platform::hideHostWindow (juce::Component&)  { return false; }
void Platform::showHostWindow (juce::Component&)  {}
void Platform::closeHostWindow (juce::Component&) {}
