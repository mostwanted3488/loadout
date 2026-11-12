#include "PluginLibrary.h"

//==============================================================================
class PluginLibrary::ScanThread : public juce::Thread
{
public:
    explicit ScanThread (PluginLibrary& l) : juce::Thread ("LOADOUT plugin scan"), lib (l) {}

    // Fast scan: lists VST3 files WITHOUT opening the plugins (no pop-ups, no waiting).
    // Plugins that ship a moduleinfo.json give their real name/vendor/category instantly;
    // the others are listed by file name and only opened the first time you load them.
    void run() override
    {
        juce::AudioPluginFormat* vst3 = nullptr;
        for (auto* f : lib.formats.getFormats())
            if (f->getName() == "VST3")
                vst3 = f;

        juce::Array<juce::PluginDescription> found;

        if (vst3 != nullptr)
        {
            const auto files = vst3->searchPathsForPlugins (vst3->getDefaultLocationsToSearch(), true, false);

            for (int i = 0; i < files.size() && ! threadShouldExit(); ++i)
            {
                const auto& path = files[i];
                const juce::File file (path);
                {
                    const juce::ScopedLock sl (lib.statusLock);
                    lib.currentlyScanning = path;
                }
                lib.scanProgress = (float) i / (float) juce::jmax (1, files.size());
                lib.triggerAsyncUpdate();

                const bool hasModuleInfo = file.getChildFile ("Contents").getChildFile ("Resources").getChildFile ("moduleinfo.json").existsAsFile()
                                        || file.getChildFile ("Contents").getChildFile ("moduleinfo.json").existsAsFile();
                const bool isShell = file.getFileName().containsIgnoreCase ("shell");

                if (hasModuleInfo || isShell)
                {
                    juce::OwnedArray<juce::PluginDescription> types;
                    vst3->findAllTypesForFile (types, path);   // moduleinfo = instant; shells (Waves) are opened once

                    if (! types.isEmpty())
                    {
                        for (auto* t : types)
                        {
                            found.add (*t);
                            lib.known.addType (*t);
                        }
                        continue;
                    }
                }

                // already resolved earlier? reuse it
                bool reused = false;
                for (auto& t : lib.known.getTypes())
                    if (t.fileOrIdentifier == path && t.pluginFormatName == "VST3")
                    {
                        found.add (t);
                        reused = true;
                    }
                if (reused)
                    continue;

                juce::PluginDescription d;
                d.pluginFormatName = "VST3";
                d.fileOrIdentifier = path;
                d.name = d.descriptiveName = file.getFileNameWithoutExtension();
                d.version = "lazy";
                d.lastFileModTime = file.getLastModificationTime();
                found.add (d);
            }
        }

        {
            const juce::ScopedLock sl (lib.listLock);
            lib.listed = found;
        }
        lib.saveList();
        lib.saveKnown();
        lib.scanProgress = 1.0f;
        lib.scanning = false;
        lib.triggerAsyncUpdate();
    }

private:
    PluginLibrary& lib;
};

//==============================================================================
PluginLibrary::Item PluginLibrary::Item::fromDescription (const juce::PluginDescription& d)
{
    Item i;
    i.kind = Kind::plugin;
    i.name = d.name;
    i.sub  = d.manufacturerName;
    i.id   = makeId (d);
    return i;
}

juce::String PluginLibrary::makeId (const juce::PluginDescription& d)
{
    return d.pluginFormatName + "|" + d.fileOrIdentifier + "|" + d.name;
}

std::unique_ptr<juce::PluginDescription> PluginLibrary::resolve (const juce::PluginDescription& d, juce::String& error)
{
    if (d.version != "lazy")
        return std::make_unique<juce::PluginDescription> (d);

    // opened before?
    for (auto& t : known.getTypes())
        if (t.fileOrIdentifier == d.fileOrIdentifier && t.pluginFormatName == d.pluginFormatName)
            return std::make_unique<juce::PluginDescription> (t);

    juce::AudioPluginFormat* fmt = nullptr;
    for (auto* f : formats.getFormats())
        if (f->getName() == d.pluginFormatName)
            fmt = f;

    if (fmt == nullptr)
    {
        error = "format not available";
        return {};
    }

    juce::OwnedArray<juce::PluginDescription> types;
    fmt->findAllTypesForFile (types, d.fileOrIdentifier);
    if (types.isEmpty())
    {
        error = "couldn't open " + d.name;
        return {};
    }

    for (auto* t : types)
        known.addType (*t);
    saveKnown();

    // swap the lazy entry for the real one(s) so names/vendors/categories become accurate
    {
        const juce::ScopedLock sl (listLock);
        for (int i = listed.size(); --i >= 0;)
            if (listed.getReference (i).fileOrIdentifier == d.fileOrIdentifier)
                listed.remove (i);
        for (auto* t : types)
            listed.add (*t);
    }
    saveList();

    for (auto* t : types)
        if (t->name.equalsIgnoreCase (d.name))
            return std::make_unique<juce::PluginDescription> (*t);
    return std::make_unique<juce::PluginDescription> (*types.getFirst());
}

PluginLibrary::PluginLibrary()
{
    formats.addDefaultFormats();

   #if JUCE_MAC
    dataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("Application Support").getChildFile ("LOADOUT");
   #else
    dataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LOADOUT");
   #endif
    dataDir.createDirectory();

    loadAll();
}

PluginLibrary::~PluginLibrary()
{
    if (scanThread != nullptr)
        scanThread->stopThread (8000);

    cancelPendingUpdate();
}

//==============================================================================
juce::StringArray PluginLibrary::getFolderNames()
{
    return { "EQ", "Dynamics", "Reverb & Delay", "Instruments", "Distortion", "Modulation", "Utility", "Other" };
}

juce::String PluginLibrary::categoryOf (const juce::PluginDescription& d)
{
    if (d.isInstrument)
        return "Instruments";

    const auto s = (d.category + " " + d.name + " " + d.descriptiveName).toLowerCase();
    auto has = [&s] (std::initializer_list<const char*> words)
    {
        for (auto* w : words)
            if (s.contains (w))
                return true;
        return false;
    };

    if (has ({ "synth", "instrument", "sampler", "generator", "musicdevice", "serum", "vital", "sylenth",
               "massive", "omnisphere", "kontakt", "keyscape", "nexus", "spire", "diva", "pigments",
               "phase plant", "analog lab", "labs", "sytrus", "flex", "piano", "keys", "rhodes",
               "string", "orchestra", "drum", "808", "bass station", "arturia" }))           return "Instruments";
    if (has ({ "eq", "equaliz", "filter" }))                                               return "EQ";
    if (has ({ "dynamic", "compress", "limit", "gate", "clip", "expander", "transient",
               "ott", "de-ess", "deess", "maxim" }))                                       return "Dynamics";
    if (has ({ "reverb", "verb", "delay", "echo", "room", "hall", "space" }))              return "Reverb & Delay";
    if (has ({ "distort", "saturat", "crush", "fuzz", "drive", "tape", "decimat",
               "amp", "tube", "warm", "soundgoodizer" }))                                  return "Distortion";
    if (has ({ "modulat", "chorus", "flang", "phas", "tremolo", "vibrato", "pitch",
               "shift", "rotary", "ensemble", "glitch", "stutter" }))                      return "Modulation";
    if (has ({ "analy", "meter", "tool", "utility", "tuner", "gain", "stereo", "spectrum",
               "scope", "balance", "imager", "mono" }))                                    return "Utility";
    return "Other";
}

juce::Array<juce::PluginDescription> PluginLibrary::getAllPlugins() const
{
    juce::Array<juce::PluginDescription> types;
    {
        const juce::ScopedLock sl (listLock);
        types = listed;
    }
    juce::Array<juce::PluginDescription> out;
    juce::StringArray seen;

    // VST3 first so it wins over the AU version of the same plugin
    std::stable_sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
    {
        return (a.pluginFormatName == "VST3") && (b.pluginFormatName != "VST3");
    });

    for (auto& d : types)
    {
        if (d.name.equalsIgnoreCase ("LOADOUT"))
            continue;

        const auto key = d.name.toLowerCase() + "|" + d.manufacturerName.toLowerCase();
        if (seen.contains (key))
            continue;

        seen.add (key);
        out.add (d);
    }

    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}

juce::Array<PluginLibrary::Item> PluginLibrary::getFolderContents (const juce::String& folder) const
{
    juce::Array<Item> out;
    for (auto& d : getAllPlugins())
        if (categoryOf (d) == folder)
            out.add (Item::fromDescription (d));
    return out;
}

juce::Array<PluginLibrary::Item> PluginLibrary::search (const juce::String& rawQuery, int maxResults) const
{
    const auto q = rawQuery.trim().toLowerCase();
    juce::Array<Item> out;
    if (q.isEmpty())
        return out;

    struct Scored { int score; juce::PluginDescription d; };
    std::vector<Scored> scored;

    for (auto& d : getAllPlugins())
    {
        const auto n = d.name.toLowerCase();
        int score = -1;

        if (n.startsWith (q))                                         score = 0;
        else
        {
            juce::StringArray words;
            words.addTokens (n, " -_/.", "");
            for (auto& w : words)
                if (w.startsWith (q)) { score = 1; break; }
        }

        if (score < 0 && n.contains (q))                              score = 2;
        if (score < 0 && (d.manufacturerName.toLowerCase().contains (q)
                          || categoryOf (d).toLowerCase().contains (q)))   score = 3;

        if (score < 0)
        {
            int j = 0;
            for (int i = 0; i < n.length() && j < q.length(); ++i)
                if (n[i] == q[j]) ++j;
            if (j == q.length()) score = 4;
        }

        if (score >= 0)
            scored.push_back ({ score, d });
    }

    std::stable_sort (scored.begin(), scored.end(), [] (const Scored& a, const Scored& b)
    {
        if (a.score != b.score) return a.score < b.score;
        return a.d.name.length() < b.d.name.length();
    });

    for (auto& s : scored)
    {
        if (out.size() >= maxResults) break;
        out.add (Item::fromDescription (s.d));
    }
    return out;
}

std::unique_ptr<juce::PluginDescription> PluginLibrary::findById (const juce::String& id) const
{
    {
        const juce::ScopedLock sl (listLock);
        for (auto& d : listed)
            if (makeId (d) == id)
                return std::make_unique<juce::PluginDescription> (d);
    }
    return known.getTypeForIdentifierString (id);   // ids saved by older versions
}

//==============================================================================
PluginLibrary::Item PluginLibrary::getSlot (int index) const
{
    const juce::ScopedLock sl (dataLock);
    return juce::isPositiveAndBelow (index, slots.size()) ? slots[index] : Item();
}

void PluginLibrary::setSlot (int index, const Item& item)
{
    {
        const juce::ScopedLock sl (dataLock);
        if (! juce::isPositiveAndBelow (index, slots.size()))
            return;
        slots.set (index, item);
    }
    saveWheel();
    sendChangeMessage();
}

void PluginLibrary::resetSlots()
{
    {
        const juce::ScopedLock sl (dataLock);
        slots.clear();
        for (auto& f : getFolderNames())
            slots.add (Item::folder (f));
        while (slots.size() < numSlots)
            slots.add (Item());
    }
    saveWheel();
    sendChangeMessage();
}

juce::Array<PluginLibrary::Item> PluginLibrary::getRecents() const
{
    const juce::ScopedLock sl (dataLock);
    return recents;
}

void PluginLibrary::pushRecent (const Item& item)
{
    if (! item.isPlugin())
        return;

    {
        const juce::ScopedLock sl (dataLock);
        for (int i = recents.size(); --i >= 0;)
            if (recents.getReference (i).id == item.id)
                recents.remove (i);
        recents.insert (0, item);
        while (recents.size() > maxRecents)
            recents.removeLast();
    }
    saveWheel();
}

void PluginLibrary::savePrefs()   { saveWheel(); }

//==============================================================================
void PluginLibrary::startScan()
{
    if (scanning.load())
        return;

    if (scanThread != nullptr)
        scanThread->stopThread (2000);

    scanning = true;
    scanProgress = 0.0f;
    scanThread = std::make_unique<ScanThread> (*this);
    scanThread->startThread();
    sendChangeMessage();
}

juce::String PluginLibrary::getScanStatus() const
{
    if (scanning.load())
    {
        const juce::ScopedLock sl (statusLock);
        auto file = juce::File::createFileWithoutCheckingPath (currentlyScanning).getFileNameWithoutExtension();
        return "Scanning " + juce::String (juce::roundToInt (scanProgress.load() * 100.0f)) + "%"
               + (file.isNotEmpty() ? "  " + file : juce::String());
    }

    const auto n = getAllPlugins().size();
    return n == 0 ? juce::String ("No plugins yet - click Rescan")
                  : juce::String (n) + " plugins";
}

void PluginLibrary::handleAsyncUpdate()   { sendChangeMessage(); }

//==============================================================================
juce::var PluginLibrary::itemToVar (const Item& i)
{
    auto* o = new juce::DynamicObject();
    if (i.isFolder())       o->setProperty ("folder", i.name);
    else if (i.isPlugin())  { o->setProperty ("id", i.id); o->setProperty ("name", i.name); o->setProperty ("sub", i.sub); }
    return juce::var (o);
}

PluginLibrary::Item PluginLibrary::itemFromVar (const juce::var& v) const
{
    if (v.hasProperty ("folder"))
        return Item::folder (v["folder"].toString());

    if (v.hasProperty ("id"))
    {
        Item i;
        i.kind = Item::Kind::plugin;
        i.id   = v["id"].toString();
        i.name = v["name"].toString();
        i.sub  = v["sub"].toString();
        return i;
    }
    return {};
}

void PluginLibrary::loadAll()
{
    if (auto xml = juce::parseXML (knownFile()))
        known.recreateFromXml (*xml);

    if (auto xml = juce::parseXML (listFile()))
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e))
                listed.add (d);
        }

    const auto json = juce::JSON::parse (slotsFile());
    if (auto* arr = json["slots"].getArray(); arr != nullptr && arr->size() == numSlots)
    {
        for (auto& v : *arr)
            slots.add (itemFromVar (v));
    }
    else
    {
        for (auto& f : getFolderNames())
            slots.add (Item::folder (f));
        while (slots.size() < numSlots)
            slots.add (Item());
    }

    if (auto* arr = json["recents"].getArray())
        for (auto& v : *arr)
            if (auto it = itemFromVar (v); it.isPlugin())
                recents.add (it);

    if (json.hasProperty ("sound"))       soundEnabled = (bool) json["sound"];
    if (json.hasProperty ("pickTracks"))  pickTracks   = (bool) json["pickTracks"];
}

void PluginLibrary::saveKnown()
{
    if (auto xml = known.createXml())
        xml->writeTo (knownFile());
}

void PluginLibrary::saveList()
{
    juce::XmlElement root ("LOADOUT_PLUGINS");
    {
        const juce::ScopedLock sl (listLock);
        for (auto& d : listed)
            if (auto e = d.createXml())
                root.addChildElement (e.release());
    }
    root.writeTo (listFile());
}

void PluginLibrary::saveWheel()
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> s, r;
    {
        const juce::ScopedLock sl (dataLock);
        for (auto& i : slots)   s.add (itemToVar (i));
        for (auto& i : recents) r.add (itemToVar (i));
    }
    o->setProperty ("slots", s);
    o->setProperty ("recents", r);
    o->setProperty ("sound", soundEnabled);
    o->setProperty ("pickTracks", pickTracks);
    slotsFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}
