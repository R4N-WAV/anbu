#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Anbu presets: a factory bank compiled into the plugin plus the user's own presets.
//
// User presets are small XML files (*.anbupreset) in Documents/R4N/Anbu/Presets; sub-folders there show up
// as categories in the menu. A preset holds the sound: Peak reduction, Color, Input, Output, Mix, mode and
// noise. Oversampling, phase, Auto Gain and Bypass are monitoring/quality choices and are left as they are.
// The current preset (name, category, where it came from) is saved with the session.
// Message thread only.
namespace anbu
{
struct Preset
{
    juce::String name, category;
    bool factory = true;
    int index = -1;                  // factory: index in the bank; -1 = the default setting
    juce::File file;                 // user preset file
    std::map<juce::String, float> values;
};

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    static const juce::StringArray& soundParams();
    static juce::File userFolder();

    // the full list in menu order: the default setting, the factory bank, then the user's presets
    const std::vector<Preset>& all();
    void rescan();

    void load (const Preset&);
    bool saveUser (const juce::String& name, juce::String& error);     // into the user folder (overwrites the same name)
    bool deleteUser (const Preset&);
    void step (int dir);                                                 // previous / next in the list

    juce::String currentName() const;
    juce::String currentCategory() const;
    bool currentIsUser() const;
    int currentListIndex();                                              // -1 when the current preset is not in the list
    bool isModified();                                                   // the sound differs from the loaded preset

private:
    std::map<juce::String, float> currentValues() const;
    void setCurrent (const Preset&);
    static std::map<juce::String, float> defaults (juce::AudioProcessorValueTreeState&);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Preset> list;
    bool scanned = false;
};
}
