#pragma once
#include "PluginProcessor.h"
#include "Platform.h"

/**
    The fullscreen LOADOUT wheel.

    When opened it captures the screen behind it (so the DAW is visible), then covers the
    screen with a borderless window that renders that capture plus the liquid-glass wheel
    through an OpenGL shader. Text and icons are painted on top by JUCE.
*/
class WheelOverlay : public juce::Component,
                     private juce::OpenGLRenderer,
                     private juce::Timer
{
public:
    /** Captures the screen and opens the wheel for this LOADOUT instance. */
    static void open (LoadoutProcessor& owner);
    static void closeIfOwnedBy (LoadoutProcessor&);
    static bool isOpen();

    ~WheelOverlay() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using Item = PluginLibrary::Item;

    WheelOverlay (LoadoutProcessor&, juce::Image snapshot, juce::Rectangle<int> screenArea);

    //==============================================================================
    // GL thread
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    // message thread
    void timerCallback() override;

    //==============================================================================
    static constexpr int N = 10;
    static constexpr int maxGlass = 24;

    struct Glass { juce::Rectangle<float> r; float radius, alpha, dark, tint; };

    struct Uniforms
    {
        float w = 1, h = 1, cx = 0, cy = 0, scale = 1;
        float H[N] {}, E[N] {};
        float M = 0, press = 0, openA = 0, mx = -9999, my = -9999;
        int numGlass = 0;
        float rects[maxGlass * 4] {}, rectParams[maxGlass * 4] {};
    };

    struct Hotspot { juce::Rectangle<float> r; std::function<void()> action; };

    struct Target
    {
        LoadoutProcessor* proc;
        std::shared_ptr<bool> alive;
        juce::String name;
        bool isOwner;
    };

    //==============================================================================
    LoadoutProcessor& owner;
    std::shared_ptr<bool> ownerAlive;
    juce::SharedResourcePointer<PluginLibrary> lib;
    juce::Image snapshot;

    juce::OpenGLContext gl;
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    juce::uint32 vao = 0, vbo = 0, tex = 0;
    juce::SpinLock uniformLock;
    Uniforms uniforms;

    // view state
    bool inFolder = false;
    juce::String folderName;
    juce::Array<Item> folderItems, searchResults;
    int page = 0;
    bool searching = false;
    juce::String query;
    int hover = -1;
    juce::Array<Item> recents;

    // drag & drop
    struct Pending { int seg = -1; Item item; juce::Point<float> start; bool active = false; };
    Pending pending;
    bool dragging = false;
    Item dragItem;
    juce::Point<float> mousePos { -9999.0f, -9999.0f };

    // track picker
    bool pickerOpen = false;
    Item pickerItem;
    std::vector<Target> targets;
    std::set<int> picked;

    // animation
    float H[N] {}, HV[N] {}, E[N] {};
    float M = 0, MV = 0, press = 0, openA = 0;
    bool closing = false, openPluginAfterClose = false;
    float labelA = 1.0f;   // label fade when the wheel content changes
    bool needsRepaint = true;

    juce::String toastText, toastLabel;
    double toastUntil = 0;

    // layout (rebuilt each frame)
    std::vector<Glass> glass;
    std::vector<Hotspot> hotspots;
    juce::Rectangle<float> crumbRect, pickerRect;
    std::vector<juce::Rectangle<float>> recentRects, pickerRows, toolRects;
    juce::Rectangle<float> pickerAll, pickerNone, pickerCancel, pickerGo;

    //==============================================================================
    float wheelScale() const;
    float animScale() const    { return wheelScale() * (0.86f + 0.14f * openA); }
    juce::Point<float> centre() const { return getLocalBounds().toFloat().getCentre(); }
    juce::Point<float> toDesign (juce::Point<float>) const;
    bool inCentre (juce::Point<float> design) const;
    int segmentAt (juce::Point<float> design) const;
    juce::Point<float> centreHalfSize() const;

    juce::Array<Item> currentItems() const;
    int numPages() const;
    void setHover (int);
    void contentChanged();

    void activate (int segment);
    void choose (const Item&);
    void commit (const Item&, const std::vector<Target>&);
    void enterFolder (const Item&);
    void back();
    void openSearch();
    void closeSearch (bool silent = false);
    void runSearch();
    void startDrag (const Item&);
    void endDrag (bool drop);
    void clearSlot (int);
    void openPicker (const Item&);
    void closePicker();
    void confirmPicker();
    void togglePick (int);
    void beginClose();
    void toast (const juce::String& label, const juce::String& text);
    void sound (UISounds::Sound);
    void keySound (juce::juce_wchar, bool backspace = false);

    void layout();
    void updateHover (juce::Point<float> pos);
    void rightClick (juce::Point<float> pos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WheelOverlay)
};
