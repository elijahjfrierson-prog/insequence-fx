#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace isfx
{
// Master-bus chain of third-party VST3 / AU plug-ins, used by playback and export.
class PluginRack
{
public:
    PluginRack();
    // Scans the standard VST3 (and AU on macOS) locations. A plug-in that crashed a previous scan is skipped
    // (dead-man's-pedal file); failures are returned by name.
    juce::StringArray scan (std::function<void (float, const juce::String&)> progress);
    const juce::KnownPluginList& known() const { return list; }
    bool add (const juce::PluginDescription&, juce::String& error);
    void remove (int index);
    void setBypass (int index, bool b);
    int size() const { const juce::ScopedLock sl (lock); return (int) slots.size(); }
    juce::String name (int index) const;
    bool bypassed (int index) const;
    juce::AudioPluginInstance* instance (int index);
    void prepare (double rate, int block);
    void reset();
    void process (float* L, float* R, int n);   // in place, n <= block (larger buffers are split)
    juce::var toVar() const;                    // chain + plug-in state for the project file
    void fromVar (const juce::var&, juce::StringArray& errors);
    juce::File listFile() const;
private:
    struct Slot { std::unique_ptr<juce::AudioPluginInstance> p; bool bypass = false; };
    juce::AudioPluginFormatManager formats;
    juce::KnownPluginList list;
    std::vector<Slot> slots;
    juce::CriticalSection lock;
    double rate = 48000; int block = 512;
    juce::AudioBuffer<float> buf;
    juce::MidiBuffer midi;
};
}
