#pragma once
#include "App.h"

namespace isfx
{
struct ImportResult
{
    std::vector<MediaItem> items;
    std::map<juce::String, juce::Image> thumbs;
    std::map<juce::String, Waveform> waves;
    std::map<juce::String, std::shared_ptr<const std::vector<float>>> stereo;
    juce::String newSongId;
    SongAnalysis song;
    std::shared_ptr<std::vector<float>> songMono;
    juce::StringArray errors;
    juce::Array<juce::File> luts, presets, templates;
};
using ProgressCb = std::function<void (double, const juce::String&)>;
// Worker-thread import: probe, thumbnails, waveforms, song analysis and take sync (no project access)
ImportResult decodeFiles (const juce::Array<juce::File>&, const juce::String& songId, std::shared_ptr<std::vector<float>> songMono,
                          bool makeSong, ProgressCb, std::function<bool()> cancelled);
SyncInfo syncTake (const juce::File& take, const std::vector<float>& songMono, juce::String& err);

class MainComponent : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void tryQuit (std::function<void()> quit);
    void openFile (const juce::File&);
    int runSelfTest (const juce::File& outDir);
    std::function<void (const juce::String&)> onTitle;
private:
    void timerCallback() override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void refresh (bool inspectorToo);
    void seek (Ticks t);
    void togglePlay();
    void setTool (Tool t);
    void runTask (const juce::String& title, std::function<void (ProgressCb, std::function<bool()>)> work, std::function<void()> done);
    void importFiles (const juce::Array<juce::File>&);
    void applyImport (ImportResult&);
    void placeSongClip();
    void setSong (const juce::String& id);
    void autoSync (const juce::String& onlyId = {});
    void relink (const juce::String& id);
    void removeMedia (const juce::String& id);
    void loadMediaCaches();
    void generate();
    void chop();
    void save (bool as, std::function<void()> after = {});
    void open();
    void newProject();
    void exportDialog();
    bool exportSequence (const Project& p, int seqIndex, const ExportJob& job, ProgressCb, std::function<bool()> cancelled, juce::String& err);
    void fileMenu();
    void presetsMenu();
    void pluginsMenu();
    void askText (const juce::String& title, const juce::String& def, std::function<void (juce::String)> cb);
    void updateSeqBox();
    void showMsg (const juce::String& title, const juce::String& msg);

    ISFXLookAndFeel lnf;
    AppState st;
    PluginRack rack;
    juce::AudioDeviceManager devices;
    Player player { st, rack };
    Preview preview { st };
    Timeline timeline { st };
    MediaPanel media { st };
    Inspector inspector { st };
    juce::TooltipWindow tips { this, 450 };
    juce::ScrollBar hscroll { false };
    juce::TextButton fileBtn { "File" }, undoBtn { "Undo" }, redoBtn { "Redo" }, importBtn { "Import" }, presetsBtn { "Presets" },
        pluginsBtn { "Plug-ins" }, playBtn { "Play" }, exportBtn { "EXPORT" },
        toolBtn[4] { juce::TextButton ("E Select"), juce::TextButton ("P Paint"), juce::TextButton ("D Delete"), juce::TextButton ("C Cut") },
        snapBtn { "S Snap" }, rippleBtn { "R Ripple" }, gridBtn { "B Grid" }, chopBtn { "CHOP" }, genBtn { "GENERATE 3 EDITS" },
        keepBtn { "Keep range" }, delRangeBtn { "Delete range" }, zoomIn { "+" }, zoomOut { "-" };
    juce::ComboBox seqBox, aspectBox, snapBox, chopBox, styleBox;
    juce::Label status;
    std::vector<std::unique_ptr<juce::DocumentWindow>> pluginWindows;
    std::shared_ptr<std::vector<float>> songMono;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> dialog;
    bool selfTest = false;
    juce::uint32 genSeed = 1;
};
}
