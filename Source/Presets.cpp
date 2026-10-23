#include "Presets.h"

namespace anbu
{
namespace
{
// the sound parameters a preset holds
const char* const kIds[] = { "peakReduction", "color", "inputGain", "outputGain", "mix", "mode", "noiseOn", "noiseLevel" };

struct Factory
{
    const char* category;
    const char* name;
    float pr, color, input, output, mix, mode, noiseOn, noiseLevel;
};

// Output is set so each preset comes out at about the loudness it went in with (measured on vocals and a mix,
// tools/presets/makeup.py). PR / Color / Input carry the character.
// clang-format off
const Factory kBank[] = {
    // category      name                            PR   Col    In   Out   Mix   Lim   Noi  NLev
    { "Vocals",       "Lead Vocal - Smooth",          24,   25,    0,    4,  100,    0,    0,  -96 },
    { "Vocals",       "Lead Vocal - Upfront",         30,   35,    0,    7,  100,    1,    0,  -96 },
    { "Vocals",       "Lead Vocal - Silk",            24,   60,    0,    4,  100,    0,    0,  -96 },
    { "Vocals",       "Lead Vocal - Gentle",          22,   20,    0,    3,  100,    0,    0,  -96 },
    { "Vocals",       "Rap - Tight",                  33,   30,    0,    8,  100,    1,    0,  -96 },
    { "Vocals",       "Rap - Dense",                  36,   45,    3,    8,  100,    1,    0,  -96 },
    { "Vocals",       "Belt Control",                 36,   25,    0,    9,  100,    1,    0,  -96 },
    { "Vocals",       "Whisper Detail",               18,   45,    6,   -2,  100,    0,    0,  -96 },
    { "Vocals",       "Backing Vocals - Tuck",        33,   40,    0,    8,  100,    0,    0,  -96 },
    { "Vocals",       "Vocal Parallel - Thick",       56,   50,    0,    4,   40,    0,    0,  -96 },
    { "Vocals",       "Podcast & Voice-over",         28,   15,    0,    6,  100,    0,    0,  -96 },
    { "Instruments",  "Bass - Round",                 37,   50,    0,    6,  100,    0,    0,  -96 },
    { "Instruments",  "Bass - Growl",                 33,   85,    6,    3,  100,    0,    0,  -96 },
    { "Instruments",  "Acoustic Guitar",              31,   30,    0,    4,  100,    0,    0,  -96 },
    { "Instruments",  "Electric Guitar - Sustain",    46,   40,    0,    9,  100,    0,    0,  -96 },
    { "Instruments",  "Keys & Piano",                 29,   35,    0,    3,  100,    0,    0,  -96 },
    { "Instruments",  "Strings & Pads",               27,   40,    0,    2,  100,    0,    0,  -96 },
    { "Instruments",  "Drum Bus - Glue",              29,   45,    0,    3,   70,    0,    0,  -96 },
    { "Instruments",  "Drums - Smash Parallel",       48,   70,    6,    3,   35,    1,    0,  -96 },
    { "Mix & Master", "Mix Bus - Gentle Glue",        25,   20,    0,    1,  100,    0,    0,  -96 },
    { "Mix & Master", "Mix Bus - Warm",               27,   45,    0,    2,  100,    0,    0,  -96 },
    { "Mix & Master", "Master - Light Touch",         24,   15,    0,    1,  100,    0,    0,  -96 },
    { "Mix & Master", "Stem Leveler",                 35,    0,    0,    5,  100,    0,    0,  -96 },
    { "Color",        "Console Tone",                  0,   25,    0,    1,  100,    0,    0,  -96 },
    { "Color",        "Silk - Subtle",                 0,   50,    0,    1,  100,    0,    0,  -96 },
    { "Color",        "Silk - Rich",                   0,   80,    0,    1,  100,    0,    0,  -96 },
    { "Color",        "Silk - Full",                   0,  100,    0,    1,  100,    0,    0,  -96 },
    { "Color",        "Clean Leveler",                29,    0,    0,    6,  100,    0,    0,  -96 },
    { "Creative",     "Lo-Fi Tape Air",               39,   90,    0,   10,  100,    0,    1,  -70 },
    { "Creative",     "Pumping Limiter",              38,   30,   12,    4,  100,    1,    0,  -96 },
    { "Creative",     "Vintage Hiss Bed",             24,   35,    0,    4,  100,    0,    1,  -84 },
    { "Creative",     "Dirty Parallel",               42,  100,   12,    3,   30,    1,    0,  -96 },
};
// clang-format on

constexpr const char* kExt = ".anbupreset";
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s) : apvts (s) {}

const juce::StringArray& PresetManager::soundParams()
{
    static const juce::StringArray ids (kIds, (int) std::size (kIds));
    return ids;
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("R4N").getChildFile ("Anbu").getChildFile ("Presets");
}

std::map<juce::String, float> PresetManager::defaults (juce::AudioProcessorValueTreeState& s)
{
    std::map<juce::String, float> v;
    for (auto& id : soundParams())
        if (auto* p = s.getParameter (id)) v[id] = p->convertFrom0to1 (p->getDefaultValue());
    return v;
}

void PresetManager::rescan()
{
    list.clear();
    {
        Preset d;
        d.name = "Default Setting"; d.category = {}; d.factory = true; d.index = -1;
        d.values = defaults (apvts);
        list.push_back (d);
    }
    for (int i = 0; i < (int) std::size (kBank); ++i)
    {
        const auto& f = kBank[i];
        Preset p;
        p.name = f.name; p.category = f.category; p.factory = true; p.index = i;
        const float v[] = { f.pr, f.color, f.input, f.output, f.mix, f.mode, f.noiseOn, f.noiseLevel };
        for (int k = 0; k < (int) std::size (kIds); ++k) p.values[kIds[k]] = v[k];
        list.push_back (p);
    }
    // user presets: the folder itself and one level of sub-folders (categories), sorted by category then name
    std::vector<Preset> user;
    const auto root = userFolder();
    if (root.isDirectory())
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, juce::String ("*") + kExt, juce::File::findFiles))
        {
            const auto f = entry.getFile();
            const auto rel = f.getParentDirectory().getRelativePathFrom (root);
            if (rel.containsChar (juce::File::getSeparatorChar())) continue;          // deeper than one level
            auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr || ! xml->hasTagName ("AnbuPreset")) continue;
            Preset p;
            p.factory = false;
            p.file = f;
            p.name = f.getFileNameWithoutExtension();
            p.category = rel == "." ? juce::String() : rel;
            p.values = defaults (apvts);
            for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            {
                const auto id = e->getStringAttribute ("id");
                if (soundParams().contains (id)) p.values[id] = (float) e->getDoubleAttribute ("value");
            }
            user.push_back (p);
        }
    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b)
    {
        if (a.category != b.category) return a.category.isEmpty() || (! b.category.isEmpty() && a.category.compareNatural (b.category) < 0);
        return a.name.compareNatural (b.name) < 0;
    });
    list.insert (list.end(), user.begin(), user.end());
    scanned = true;
}

const std::vector<Preset>& PresetManager::all()
{
    if (! scanned) rescan();
    return list;
}

void PresetManager::load (const Preset& p)
{
    for (auto& [id, value] : p.values)
        if (auto* prm = apvts.getParameter (id))
        {
            const float n = prm->convertTo0to1 (value);
            if (std::abs (prm->getValue() - n) < 1.0e-6f) continue;
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (n);
            prm->endChangeGesture();
        }
    setCurrent (p);
}

void PresetManager::setCurrent (const Preset& p)
{
    auto& st = apvts.state;
    st.setProperty ("presetName", p.name, nullptr);
    st.setProperty ("presetCategory", p.category, nullptr);
    st.setProperty ("presetUser", ! p.factory, nullptr);
    st.setProperty ("presetFile", p.factory ? juce::String() : p.file.getFullPathName(), nullptr);
}

std::map<juce::String, float> PresetManager::currentValues() const
{
    std::map<juce::String, float> v;
    for (auto& id : soundParams())
        if (auto* p = apvts.getParameter (id)) v[id] = p->convertFrom0to1 (p->getValue());
    return v;
}

bool PresetManager::saveUser (const juce::String& rawName, juce::String& error)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) { error = "Type a name first"; return false; }
    const auto dir = userFolder();
    if (! dir.isDirectory() && ! dir.createDirectory()) { error = "Can't create the preset folder"; return false; }
    juce::XmlElement xml ("AnbuPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("plugin", "Anbu");
    for (auto& [id, value] : currentValues())
    {
        auto* e = xml.createNewChildElement ("Param");
        e->setAttribute ("id", id);
        e->setAttribute ("value", (double) value);
    }
    const auto file = dir.getChildFile (name + kExt);
    if (! xml.writeTo (file)) { error = "Can't write " + file.getFullPathName(); return false; }
    rescan();
    for (auto& p : list)
        if (! p.factory && p.file == file) { setCurrent (p); break; }
    return true;
}

bool PresetManager::deleteUser (const Preset& p)
{
    if (p.factory || ! p.file.existsAsFile()) return false;
    const bool wasCurrent = currentIsUser() && apvts.state.getProperty ("presetFile").toString() == p.file.getFullPathName();
    const bool ok = p.file.moveToTrash() || p.file.deleteFile();
    rescan();
    if (ok && wasCurrent)
    {
        auto& st = apvts.state;     // the sound stays; it just no longer has a name
        st.setProperty ("presetName", "", nullptr);
        st.setProperty ("presetUser", false, nullptr);
        st.setProperty ("presetFile", "", nullptr);
    }
    return ok;
}

juce::String PresetManager::currentName() const
{
    return apvts.state.getProperty ("presetName", "Default Setting").toString();
}

juce::String PresetManager::currentCategory() const
{
    return apvts.state.getProperty ("presetCategory").toString();
}

bool PresetManager::currentIsUser() const
{
    return (bool) apvts.state.getProperty ("presetUser", false);
}

int PresetManager::currentListIndex()
{
    const auto& l = all();
    const auto name = currentName();
    const bool user = currentIsUser();
    const auto file = apvts.state.getProperty ("presetFile").toString();
    for (int i = 0; i < (int) l.size(); ++i)
    {
        const auto& p = l[(size_t) i];
        if (user ? (! p.factory && p.file.getFullPathName() == file) : (p.factory && p.name == name)) return i;
    }
    return -1;
}

bool PresetManager::isModified()
{
    const int i = currentListIndex();
    if (i < 0) return currentName().isNotEmpty();
    const auto cur = currentValues();
    for (auto& [id, v] : list[(size_t) i].values)
    {
        const auto it = cur.find (id);
        if (it != cur.end() && std::abs (it->second - v) > 0.5f) return true;
    }
    return false;
}

void PresetManager::step (int dir)
{
    const auto& l = all();
    if (l.empty()) return;
    int i = currentListIndex();
    const int n = (int) l.size();
    i = i < 0 ? (dir > 0 ? 0 : n - 1) : ((i + dir) % n + n) % n;
    load (l[(size_t) i]);
}
}
