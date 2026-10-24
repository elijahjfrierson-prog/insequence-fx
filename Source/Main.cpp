#include "MainComponent.h"
#include "BinaryData.h"

namespace isfx
{
class EulaComponent : public juce::Component
{
public:
    std::function<void (bool)> onDone;
    EulaComponent()
    {
        text.setMultiLine (true); text.setReadOnly (true); text.setScrollbarsShown (true);
        text.setText (juce::String::fromUTF8 (BinaryData::LICENSE_txt, BinaryData::LICENSE_txtSize));
        text.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
        addAndMakeVisible (text); addAndMakeVisible (agree); addAndMakeVisible (accept); addAndMakeVisible (quit);
        accept.setEnabled (false);
        agree.onClick = [this] { accept.setEnabled (agree.getToggleState()); };
        accept.onClick = [this] { if (onDone) onDone (true); };
        quit.onClick = [this] { if (onDone) onDone (false); };
        setSize (720, 560);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::panel);
        g.setColour (theme::accent); g.setFont (juce::Font (20.0f, juce::Font::bold));
        g.drawText ("InSequence FX - License Agreement", 16, 10, getWidth() - 32, 30, juce::Justification::left);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16); r.removeFromTop (36);
        auto b = r.removeFromBottom (36);
        quit.setBounds (b.removeFromRight (100)); b.removeFromRight (8); accept.setBounds (b.removeFromRight (120));
        agree.setBounds (b);
        r.removeFromBottom (8); text.setBounds (r);
    }
private:
    juce::TextEditor text;
    juce::ToggleButton agree { "I have read and agree to the license agreement" };
    juce::TextButton accept { "Accept" }, quit { "Quit" };
};

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow (const juce::String& name)
        : juce::DocumentWindow (name, theme::bg, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        mc = new MainComponent();
        mc->onTitle = [this] (const juce::String& t) { setName (t); };
        setContentOwned (mc, true);
        setResizable (true, true);
        setResizeLimits (1180, 720, 10000, 10000);
        centreWithSize (std::min (1600, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                        std::min (960, juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 40));
        setVisible (true);
    }
    void closeButtonPressed() override { mc->tryQuit ([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }); }
    MainComponent* mc = nullptr;
};

class App : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "InSequence FX"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& cmd) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "InSequence FX"; o.filenameSuffix = ".settings"; o.folderName = "InSequence FX";
        o.osxLibrarySubFolder = "Application Support";
        settings = std::make_unique<juce::PropertiesFile> (o);
        auto args = juce::StringArray::fromTokens (cmd, true);
        args.trim(); args.removeEmptyStrings();
        if (args.contains ("--selftest"))
        {
            window = std::make_unique<MainWindow> ("InSequence FX (self test)");
            int i = args.indexOf ("--selftest");
            juce::File out = i + 1 < args.size() ? juce::File::getCurrentWorkingDirectory().getChildFile (args[i + 1].unquoted())
                                                 : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("isfx-selftest");
            juce::MessageManager::callAsync ([this, out]
            {
                int rc = window->mc->runSelfTest (out);
                setApplicationReturnValue (rc);
                quit();
            });
            return;
        }
        if (settings->getIntValue ("eulaAccepted", 0) != 1) showEula (cmd); else start (cmd);
    }

    void showEula (const juce::String& cmd)
    {
        auto* e = new EulaComponent();
        juce::DialogWindow::LaunchOptions lo;
        lo.content.setOwned (e); lo.dialogTitle = "InSequence FX License"; lo.dialogBackgroundColour = theme::panel;
        lo.escapeKeyTriggersCloseButton = false; lo.useNativeTitleBar = true; lo.resizable = false;
        eula = lo.launchAsync();
        e->onDone = [this, cmd] (bool ok)
        {
            if (eula) eula->exitModalState (0);
            juce::MessageManager::callAsync ([this, ok, cmd]
            {
                delete eula.getComponent();
                if (! ok) { quit(); return; }
                settings->setValue ("eulaAccepted", 1); settings->saveIfNeeded();
                start (cmd);
            });
        };
    }

    void start (const juce::String& cmd)
    {
        window = std::make_unique<MainWindow> ("InSequence FX");
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile (cmd.unquoted().trim());
        if (cmd.isNotEmpty() && f.existsAsFile() && f.hasFileExtension ("isfx")) window->mc->openFile (f);
    }

    void anotherInstanceStarted (const juce::String& cmd) override
    {
        juce::File f (cmd.unquoted().trim());
        if (window && f.existsAsFile() && f.hasFileExtension ("isfx")) window->mc->openFile (f);
    }

    void shutdown() override { window.reset(); settings.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::Component::SafePointer<juce::DialogWindow> eula;
};
}

START_JUCE_APPLICATION (isfx::App)
