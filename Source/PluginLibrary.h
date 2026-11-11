#pragma once
#include <JuceHeader.h>

/** UTF-8 literal helper (keeps MSVC + juce::String happy with non-ASCII characters). */
inline juce::String U (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/**
    Shared between every LOADOUT instance in the host process:
    the plugin formats, the scanned plugin list, the wheel slots, the recents and the prefs.
*/
class PluginLibrary : public juce::ChangeBroadcaster,
                      private juce::AsyncUpdater
{
public:
    PluginLibrary();
    ~PluginLibrary() override;

    //==============================================================================
    struct Item
    {
        enum class Kind { empty, folder, plugin };
        Kind kind = Kind::empty;
        juce::String name;      // folder name or plugin name
        juce::String sub;       // manufacturer / category
        juce::String id;        // plugin identifier string

        bool isEmpty()  const { return kind == Kind::empty; }
        bool isFolder() const { return kind == Kind::folder; }
        bool isPlugin() const { return kind == Kind::plugin; }

        static Item folder (const juce::String& n)            { Item i; i.kind = Kind::folder; i.name = n; return i; }
        static Item fromDescription (const juce::PluginDescription& d);
    };

    static constexpr int numSlots = 10;
    static constexpr int maxRecents = 5;

    //==============================================================================
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList known;

    juce::Array<juce::PluginDescription> getAllPlugins() const;
    juce::Array<Item> getFolderContents (const juce::String& folder) const;
    juce::Array<Item> search (const juce::String& query, int maxResults) const;
    std::unique_ptr<juce::PluginDescription> findById (const juce::String& id) const;

    static juce::StringArray getFolderNames();
    static juce::String categoryOf (const juce::PluginDescription&);

    //==============================================================================
    Item getSlot (int index) const;
    void setSlot (int index, const Item&);
    void resetSlots();

    juce::Array<Item> getRecents() const;
    void pushRecent (const Item&);

    bool  soundEnabled = true;
    bool  pickTracks   = false;
    void  savePrefs();

    //==============================================================================
    void startScan();
    bool isScanning() const                 { return scanning.load(); }
    float getScanProgress() const           { return scanProgress.load(); }
    juce::String getScanStatus() const;

private:
    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;
    std::atomic<bool> scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    juce::CriticalSection statusLock;
    juce::String currentlyScanning;

    juce::Array<Item> slots, recents;
    mutable juce::CriticalSection dataLock;

    juce::File dataDir;
    juce::File knownFile()  const { return dataDir.getChildFile ("plugins.xml"); }
    juce::File slotsFile()  const { return dataDir.getChildFile ("wheel.json"); }
    juce::File pedalFile()  const { return dataDir.getChildFile ("scan-in-progress.txt"); }

    void loadAll();
    void saveKnown();
    void saveWheel();
    static juce::var itemToVar (const Item&);
    Item itemFromVar (const juce::var&) const;

    void handleAsyncUpdate() override;   // scan progress -> listeners
    friend class ScanThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginLibrary)
};
