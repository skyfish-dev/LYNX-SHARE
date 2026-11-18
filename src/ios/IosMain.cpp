// iOS entry point. The desktop app (src/Main.cpp) builds a multi-window workspace with plugin hosting and desktop
// audio drivers; this builds the touch-first front end instead, on the same JUCE-free engine (see docs/IOS.md).

#include <juce_gui_basics/juce_gui_basics.h>

#include "ios/IosAudioEngine.h"
#include "ios/IosRootComponent.h"

namespace
{
class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (lynx::ios::AudioEngine& engine)
        : DocumentWindow ("LYNX SHARE", juce::Colour (0xff14151a), DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (false);
        setTitleBarHeight (0);
        setContentOwned (new lynx::ios::RootComponent (engine), true);
        setResizable (false, false);

        // iOS always presents the peer full screen; the bounds are only a fallback for the display query
        auto area = juce::Rectangle<int> (0, 0, 1024, 768);

        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            area = display->userBounds.toNearestInt();

        setBounds (area);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }
};

class LynxApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "LYNX SHARE"; }
    const juce::String getApplicationVersion() override { return LYNX_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::Desktop::getInstance().setOrientationsEnabled (juce::Desktop::rotatedClockwise | juce::Desktop::rotatedAntiClockwise);

        engine = std::make_unique<lynx::ios::AudioEngine>();

        window = std::make_unique<MainWindow> (*engine);

        // the audio session is opened once the app is on screen (iOS rejects it before the window is up)
        juce::MessageManager::callAsync ([this]
        {
            if (engine != nullptr && ! engine->open())
                DBG ("LYNX: audio device did not open: " << engine->status());
        });
    }

    void shutdown() override
    {
        window.reset();

        if (engine != nullptr)
        {
            engine->close();
            engine.reset();
        }
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<lynx::ios::AudioEngine> engine;
    std::unique_ptr<MainWindow> window;
};
} // namespace

START_JUCE_APPLICATION (LynxApplication)
