#pragma once
#include "PluginLibrary.h"
#include "Platform.h"
#include <array>

/**
    LOADOUT: a tiny plugin host. One plugin chosen from the wheel runs inside it.
    Every LOADOUT in the project registers itself so a "remote" instance (e.g. on the master)
    can send plugins to other tracks.
*/
class LoadoutProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster
{
public:
    LoadoutProcessor();
    ~LoadoutProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return "LOADOUT"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    void updateTrackProperties (const TrackProperties&) override;

    //==============================================================================
    /** Loads a plugin into this instance (message thread). */
    bool loadPlugin (const juce::PluginDescription&, juce::String& error);
    void unloadPlugin();
    juce::String getLoadedName() const;
    bool hasPlugin() const;

    /** Queues one of the wheel's UI sounds; it is mixed into this instance's output
        (so you hear it through the DAW's audio interface). Message thread. */
    void playUISound (int soundIndex);

    void showPluginWindow();
    void closePluginWindow();

    juce::String getDisplayName() const;
    void setCustomName (const juce::String&);
    juce::String getCustomName() const                  { return customName; }

    juce::SharedResourcePointer<PluginLibrary> library;

    //==============================================================================
    static juce::Array<LoadoutProcessor*> getAllInstances();

    /** Flips to false in the destructor - lets async callbacks check the instance still exists. */
    std::shared_ptr<bool> aliveFlag() const             { return alive; }

private:
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    class HostedWindow;

    std::unique_ptr<juce::AudioPluginInstance> hosted;
    juce::PluginDescription hostedDesc;
    juce::CriticalSection hostedLock;
    std::unique_ptr<HostedWindow> hostedWindow;

    juce::AudioBuffer<float> scratch;

    struct UIVoice { int sound = -1; double pos = 0.0; };
    std::array<UIVoice, 8> uiVoices;
    juce::AbstractFifo uiFifo { 64 };
    std::array<int, 64> uiQueue {};
    void mixUISounds (juce::AudioBuffer<float>&);
    void processHosted (juce::AudioBuffer<float>&, juce::MidiBuffer&);
    double currentRate = 44100.0;
    int currentBlock = 512;

    juce::String customName, trackName;
    int instanceNumber = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoadoutProcessor)
};
