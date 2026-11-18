#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ios/IosAudioEngine.h"

namespace lynx::ios
{
// The Channel Rack's step sequencer, drawn for touch: one row per channel (name, mute, 16 steps).
class StepGrid final : public juce::Component
{
public:
    explicit StepGrid (AudioEngine& engineToUse);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int contentHeight() const;

    static constexpr int rowHeight = 46;
    static constexpr int nameWidth = 148;

private:
    juce::Rectangle<int> rowBounds (int channel) const;
    juce::Rectangle<int> muteBounds (int channel) const;
    juce::Rectangle<int> stepBounds (int channel, int step) const;

    AudioEngine& audio;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepGrid)
};

// The whole app: transport, project opening and the step grid.
class RootComponent final : public juce::Component, private juce::Timer
{
public:
    explicit RootComponent (AudioEngine& engineToUse);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshLabels();
    void openProject();

    AudioEngine& audio;

    juce::Label titleLabel, patternLabel, bpmLabel;
    juce::TextButton openButton { "OPEN" }, playButton { "PLAY" }, rewindButton { "|<" };
    juce::TextButton previousPattern { "<" }, nextPatternButton { ">" };
    juce::TextButton bpmDown { "-" }, bpmUp { "+" };

    juce::Viewport gridViewport;
    StepGrid grid;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RootComponent)
};
} // namespace lynx::ios
