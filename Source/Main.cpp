#include <juce_gui_basics/juce_gui_basics.h>
#include "AppLookAndFeel.h"
#include "MainComponent.h"

class OSCFadeBridgeApplication : public juce::JUCEApplication
{
public:
    OSCFadeBridgeApplication() = default;

    const juce::String getApplicationName() override { return "OSC Fade Bridge"; }
    const juce::String getApplicationVersion() override { return "1.1.1"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Desktop::getInstance().getDefaultLookAndFeel()
                                         .findColour (juce::ResizableWindow::backgroundColourId),
                               DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);

            auto* content = new MainComponent();
            content->onTitleRequested = [this] (const juce::String& title) { setName (title); };
            content->refreshTitle();
            setContentOwned (content, true);

            centreWithSize (getWidth(), getHeight());
            setResizable (true, true);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    AppLookAndFeel lookAndFeel;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (OSCFadeBridgeApplication)
