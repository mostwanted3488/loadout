#include "PluginEditor.h"
#include "WheelOverlay.h"

namespace
{
    juce::Font font (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions().withHeight (h).withStyle (bold ? "Bold" : "Regular"));
    }
}

//==============================================================================
LoadoutEditor::GlassLook::GlassLook()
{
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff111111));
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::white.withAlpha (0.06f));
    setColour (juce::TextEditor::outlineColourId, juce::Colours::white.withAlpha (0.18f));
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::white.withAlpha (0.55f));
    setColour (juce::TextEditor::textColourId, juce::Colours::white);
    setColour (juce::TextEditor::highlightColourId, juce::Colours::white.withAlpha (0.25f));
    setColour (juce::CaretComponent::caretColourId, juce::Colours::white);
}

void LoadoutEditor::GlassLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float rad = r.getHeight() * 0.5f;
    const bool main = b.getProperties()["main"];

    if (main)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.8f : (over ? 1.0f : 0.9f)));
        g.fillRoundedRectangle (r, rad);
        return;
    }

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (over ? 0.2f : 0.13f), r.getTopLeft(),
                                             juce::Colours::white.withAlpha (over ? 0.1f : 0.05f), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, rad);
    g.setColour (juce::Colours::white.withAlpha (down ? 0.5f : 0.22f));
    g.drawRoundedRectangle (r, rad, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawLine (r.getX() + rad, r.getY() + 1.0f, r.getRight() - rad, r.getY() + 1.0f, 1.0f);
}

juce::Font LoadoutEditor::GlassLook::getTextButtonFont (juce::TextButton&, int)
{
    return font (12.0f);
}

//==============================================================================
LoadoutEditor::LoadoutEditor (LoadoutProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);

    openWheel.setButtonText ("Open wheel   (Tab)");
    openWheel.getProperties().set ("main", true);
    openWheel.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff111111));
    openWheel.onClick = [this] { WheelOverlay::open (proc); };

    showPlugin.setButtonText ("Show plugin");
    showPlugin.onClick = [this] { proc.showPluginWindow(); };

    removePlugin.setButtonText ("Remove");
    removePlugin.onClick = [this] { proc.unloadPlugin(); };

    sendMode.onClick = [this]
    {
        proc.library->pickTracks = ! proc.library->pickTracks;
        proc.library->savePrefs();
        refresh();
    };

    soundToggle.onClick = [this]
    {
        proc.library->soundEnabled = ! proc.library->soundEnabled;
        proc.library->savePrefs();
        refresh();
    };

    scan.onClick = [this] { proc.library->startScan(); refresh(); };

    nameEditor.setFont (font (13.0f, false));
    nameEditor.setTextToShowWhenEmpty ("Track name (auto)", juce::Colours::white.withAlpha (0.4f));
    nameEditor.setText (proc.getCustomName(), false);
    nameEditor.setIndents (12, 7);
    nameEditor.onTextChange = [this] { proc.setCustomName (nameEditor.getText()); };
    nameEditor.onReturnKey  = [this] { grabKeyboardFocus(); };

    for (auto* c : std::initializer_list<juce::Component*> { &openWheel, &showPlugin, &removePlugin, &sendMode, &soundToggle, &scan, &nameEditor })
        addAndMakeVisible (c);

    setWantsKeyboardFocus (true);
    setSize (460, 300);

    proc.addChangeListener (this);
    proc.library->addChangeListener (this);

    if (proc.library->getAllPlugins().isEmpty() && ! proc.library->isScanning())
        proc.library->startScan();

    refresh();
    startTimerHz (4);
}

LoadoutEditor::~LoadoutEditor()
{
    proc.removeChangeListener (this);
    proc.library->removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void LoadoutEditor::changeListenerCallback (juce::ChangeBroadcaster*)   { refresh(); }
void LoadoutEditor::timerCallback()                                       { if (proc.library->isScanning()) repaint(); }

void LoadoutEditor::refresh()
{
    const bool has = proc.hasPlugin();
    showPlugin.setEnabled (has);
    removePlugin.setEnabled (has);
    sendMode.setButtonText (juce::String ("Send to:  ") + (proc.library->pickTracks ? "Pick tracks" : "This track"));
    soundToggle.setButtonText (juce::String ("Sound:  ") + (proc.library->soundEnabled ? "On" : "Off"));
    scan.setButtonText (proc.library->isScanning() ? "Scanning..." : "Rescan plugins");
    scan.setEnabled (! proc.library->isScanning());
    repaint();
}

bool LoadoutEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::tabKey)
    {
        WheelOverlay::open (proc);
        return true;
    }
    return false;
}

//==============================================================================
void LoadoutEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff07090d));

    struct Blob { juce::uint32 c; float x, y, r; };
    for (auto bl : { Blob { 0xff2b6cff, .15f, .2f, .7f }, Blob { 0xff00d4ff, .95f, .9f, .6f }, Blob { 0xff8a2be2, .85f, .1f, .5f } })
    {
        const juce::Point<float> c (b.getWidth() * bl.x, b.getHeight() * bl.y);
        const auto col = juce::Colour (bl.c);
        g.setGradientFill (juce::ColourGradient (col.withAlpha (0.28f), c, col.withAlpha (0.0f),
                                                 c + juce::Point<float> (b.getWidth() * bl.r, 0.0f), true));
        g.fillRect (b);
    }

    // glass card
    const auto card = b.reduced (14.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRoundedRectangle (card, 20.0f);
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (card.reduced (0.5f), 20.0f, 1.0f);

    g.setColour (juce::Colours::white);
    g.setFont (font (18.0f));
    g.drawText ("LOADOUT", juce::Rectangle<float> (card.getX() + 22, card.getY() + 16, 200, 24), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (font (10.0f, false));
    g.drawText ("PLUGIN WHEEL", juce::Rectangle<float> (card.getX() + 22, card.getY() + 38, 200, 14), juce::Justification::centredLeft);

    const auto loaded = proc.getLoadedName();
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (font (10.0f, false));
    g.drawText ("LOADED", juce::Rectangle<float> (card.getX() + 22, card.getY() + 66, 200, 14), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white.withAlpha (loaded.isEmpty() ? 0.45f : 1.0f));
    g.setFont (font (loaded.isEmpty() ? 14.0f : 20.0f, ! loaded.isEmpty()));
    g.drawText (loaded.isEmpty() ? juce::String ("Nothing loaded - open the wheel") : loaded,
                (int) card.getX() + 22, (int) card.getY() + 80, (int) card.getWidth() - 44, 28, juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (font (11.0f, false));
    g.drawText (proc.library->getScanStatus(), (int) card.getX() + 22, (int) card.getBottom() - 34, (int) card.getWidth() - 160, 18,
                juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::white.withAlpha (0.32f));
    g.setFont (font (10.0f));
    g.drawText ("@buyorbealoser", (int) card.getRight() - 140, (int) card.getBottom() - 34, 118, 18, juce::Justification::centredRight);
}

void LoadoutEditor::resized()
{
    auto card = getLocalBounds().reduced (14).reduced (22, 0);
    nameEditor.setBounds (card.getRight() - 170, 30, 170, 30);

    auto row1 = juce::Rectangle<int> (card.getX(), 134, card.getWidth(), 34);
    openWheel.setBounds (row1.removeFromLeft (170));
    row1.removeFromLeft (8);
    showPlugin.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (8);
    removePlugin.setBounds (row1);

    auto row2 = juce::Rectangle<int> (card.getX(), 178, card.getWidth(), 30);
    sendMode.setBounds (row2.removeFromLeft (150));
    row2.removeFromLeft (8);
    soundToggle.setBounds (row2.removeFromLeft (100));
    row2.removeFromLeft (8);
    scan.setBounds (row2);
}
