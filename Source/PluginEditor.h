#pragma once
#include "PluginProcessor.h"

/** The small panel shown in the DAW. The wheel itself opens fullscreen (WheelOverlay). */
class LoadoutEditor : public juce::AudioProcessorEditor,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit LoadoutEditor (LoadoutProcessor&);
    ~LoadoutEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct GlassLook : juce::LookAndFeel_V4
    {
        GlassLook();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();

    LoadoutProcessor& proc;
    GlassLook look;

    juce::TextButton openWheel, showPlugin, removePlugin, sendMode, soundToggle, scan;
    juce::TextEditor nameEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoadoutEditor)
};
