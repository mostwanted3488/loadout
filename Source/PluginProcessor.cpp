#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WheelOverlay.h"

//==============================================================================
namespace
{
    juce::CriticalSection& registryLock()            { static juce::CriticalSection c; return c; }
    juce::Array<LoadoutProcessor*>& registry()       { static juce::Array<LoadoutProcessor*> a; return a; }
    std::atomic<int> instanceCounter { 0 };
}

juce::Array<LoadoutProcessor*> LoadoutProcessor::getAllInstances()
{
    const juce::ScopedLock sl (registryLock());
    return registry();
}

//==============================================================================
class LoadoutProcessor::HostedWindow : public juce::DocumentWindow
{
public:
    HostedWindow (LoadoutProcessor& o, juce::AudioProcessorEditor* ed, const juce::String& title)
        : juce::DocumentWindow (title, juce::Colour (0xff15171c), juce::DocumentWindow::closeButton), owner (o)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        setAlwaysOnTop (true);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([&o = owner, alive = owner.aliveFlag()]
        {
            if (*alive) o.closePluginWindow();
        });
    }

private:
    LoadoutProcessor& owner;
};

//==============================================================================
LoadoutProcessor::LoadoutProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    instanceNumber = ++instanceCounter;
    const juce::ScopedLock sl (registryLock());
    registry().add (this);
}

LoadoutProcessor::~LoadoutProcessor()
{
    *alive = false;
    WheelOverlay::closeIfOwnedBy (*this);
    {
        const juce::ScopedLock sl (registryLock());
        registry().removeFirstMatchingValue (this);
    }
    closePluginWindow();

    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostedLock);
        std::swap (old, hosted);
    }
}

//==============================================================================
bool LoadoutProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out || layouts.getMainInputChannelSet().isDisabled();
}

void LoadoutProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    currentBlock = samplesPerBlock;
    scratch.setSize (16, juce::jmax (1, samplesPerBlock), false, true, true);

    const juce::ScopedLock sl (hostedLock);
    if (hosted != nullptr)
    {
        hosted->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
        hosted->prepareToPlay (sampleRate, samplesPerBlock);
    }
}

void LoadoutProcessor::releaseResources()
{
    const juce::ScopedLock sl (hostedLock);
    if (hosted != nullptr)
        hosted->releaseResources();
}

double LoadoutProcessor::getTailLengthSeconds() const
{
    const juce::ScopedLock sl (hostedLock);
    return hosted != nullptr ? hosted->getTailLengthSeconds() : 0.0;
}

void LoadoutProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const juce::ScopedTryLock tl (hostedLock);
    if (! tl.isLocked() || hosted == nullptr)
        return;   // pass-through while a plugin is being swapped / nothing loaded

    const int need = juce::jmax (hosted->getTotalNumInputChannels(), hosted->getTotalNumOutputChannels());

    if (need <= buffer.getNumChannels())
    {
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), juce::jmax (1, need), numSamples);
        hosted->processBlock (view, midi);
    }
    else if (need <= scratch.getNumChannels() && numSamples <= scratch.getNumSamples())
    {
        juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(), need, numSamples);
        view.clear();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            view.copyFrom (ch, 0, buffer, ch, 0, numSamples);

        hosted->processBlock (view, midi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.copyFrom (ch, 0, view, ch, 0, numSamples);
    }
}

//==============================================================================
bool LoadoutProcessor::loadPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    JUCE_ASSERT_MESSAGE_THREAD

    auto instance = library->formats.createPluginInstance (desc, currentRate, currentBlock, error);
    if (instance == nullptr)
        return false;

    // try to match our stereo layout, instruments simply keep their own
    instance->enableAllBuses();
    auto layout = instance->getBusesLayout();
    if (instance->getBusCount (true) > 0)   layout.getChannelSet (true, 0)  = getMainBusNumInputChannels() == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    if (instance->getBusCount (false) > 0)  layout.getChannelSet (false, 0) = getMainBusNumOutputChannels() == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    instance->setBusesLayout (layout);

    instance->setRateAndBufferSizeDetails (currentRate, currentBlock);
    instance->prepareToPlay (currentRate, currentBlock);

    closePluginWindow();

    {
        const juce::ScopedLock sl (hostedLock);
        std::swap (hosted, instance);
        hostedDesc = desc;
    }

    if (instance != nullptr)
        instance->releaseResources();
    instance.reset();

    setLatencySamples (hosted->getLatencySamples());
    updateHostDisplay (ChangeDetails().withLatencyChanged (true).withNonParameterStateChanged (true));
    sendChangeMessage();
    return true;
}

void LoadoutProcessor::unloadPlugin()
{
    closePluginWindow();
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (hostedLock);
        std::swap (old, hosted);
        hostedDesc = {};
    }
    old.reset();
    setLatencySamples (0);
    updateHostDisplay (ChangeDetails().withLatencyChanged (true).withNonParameterStateChanged (true));
    sendChangeMessage();
}

juce::String LoadoutProcessor::getLoadedName() const
{
    const juce::ScopedLock sl (hostedLock);
    return hosted != nullptr ? hostedDesc.name : juce::String();
}

bool LoadoutProcessor::hasPlugin() const
{
    const juce::ScopedLock sl (hostedLock);
    return hosted != nullptr;
}

void LoadoutProcessor::showPluginWindow()
{
    if (hostedWindow != nullptr)
    {
        hostedWindow->toFront (true);
        return;
    }

    if (hosted == nullptr)
        return;

    if (auto* ed = hosted->createEditorIfNeeded())
        hostedWindow = std::make_unique<HostedWindow> (*this, ed, hostedDesc.name + U("  \xc2\xb7  ") + getDisplayName());
}

void LoadoutProcessor::closePluginWindow()
{
    hostedWindow.reset();
}

//==============================================================================
juce::String LoadoutProcessor::getDisplayName() const
{
    if (customName.isNotEmpty()) return customName;
    if (trackName.isNotEmpty())  return trackName;
    return "Track " + juce::String (instanceNumber);
}

void LoadoutProcessor::setCustomName (const juce::String& n)
{
    customName = n.trim();
    sendChangeMessage();
}

void LoadoutProcessor::updateTrackProperties (const TrackProperties& p)
{
    if (p.name.has_value())
    {
        trackName = *p.name;
        juce::MessageManager::callAsync ([this, a = aliveFlag()] { if (*a) sendChangeMessage(); });
    }
}

//==============================================================================
void LoadoutProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("LOADOUT");
    state.setProperty ("name", customName, nullptr);

    const juce::ScopedLock sl (hostedLock);
    if (hosted != nullptr)
    {
        if (auto xml = hostedDesc.createXml())
            state.setProperty ("plugin", xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

        juce::MemoryBlock mb;
        hosted->getStateInformation (mb);
        state.setProperty ("pluginState", mb.toBase64Encoding(), nullptr);
    }

    juce::MemoryOutputStream out (dest, false);
    state.writeToStream (out);
}

void LoadoutProcessor::setStateInformation (const void* data, int size)
{
    auto state = juce::ValueTree::readFromData (data, (size_t) size);
    if (! state.hasType ("LOADOUT"))
        return;

    auto apply = [this, state]
    {
        customName = state["name"].toString();

        const auto pluginXml = state["plugin"].toString();
        if (pluginXml.isEmpty())
        {
            unloadPlugin();
            return;
        }

        juce::PluginDescription desc;
        if (auto xml = juce::parseXML (pluginXml); xml != nullptr && desc.loadFromXml (*xml))
        {
            juce::String err;
            if (loadPlugin (desc, err))
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (state["pluginState"].toString()) && mb.getSize() > 0)
                {
                    const juce::ScopedLock sl (hostedLock);
                    if (hosted != nullptr)
                        hosted->setStateInformation (mb.getData(), (int) mb.getSize());
                }
            }
        }
        sendChangeMessage();
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync ([apply, a = aliveFlag()] { if (*a) apply(); });
}

//==============================================================================
juce::AudioProcessorEditor* LoadoutProcessor::createEditor()
{
    return new LoadoutEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoadoutProcessor();
}
