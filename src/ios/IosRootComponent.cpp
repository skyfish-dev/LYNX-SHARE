#include "ios/IosRootComponent.h"

#include <cmath>

namespace lynx::ios
{
namespace
{
const juce::Colour background { 0xff14151a };
const juce::Colour panel { 0xff1d1f26 };
const juce::Colour panelAlt { 0xff22242c };
const juce::Colour accent { 0xffe0653a };
const juce::Colour accentDim { 0xff5a3a2e };
const juce::Colour text { 0xffe8e9ee };
const juce::Colour textDim { 0xff9aa0ae };
const juce::Colour muteRed { 0xffcf4a4a };

juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

void styleButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, panelAlt);
    button.setColour (juce::TextButton::buttonOnColourId, accent);
    button.setColour (juce::TextButton::textColourOffId, text);
    button.setColour (juce::TextButton::textColourOnId, text);
}

void styleLabel (juce::Label& label, float height, juce::Justification justification, juce::Colour colour = text)
{
    label.setFont (uiFont (height));
    label.setColour (juce::Label::textColourId, colour);
    label.setJustificationType (justification);
    label.setInterceptsMouseClicks (false, false);
}

// the notch and the home indicator on iOS live outside the Display's safe area
juce::BorderSize<int> safeAreaInsetsFor (const juce::Component& component)
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (component.getScreenBounds()))
        return display->safeAreaInsets;

    return {};
}
} // namespace

// ---------------------------------------------------------------------------
// StepGrid

StepGrid::StepGrid (AudioEngine& engineToUse) : audio (engineToUse)
{
    setOpaque (true);
    setInterceptsMouseClicks (true, false);
}

int StepGrid::contentHeight() const
{
    return juce::jmax (rowHeight, audio.channelCount() * rowHeight);
}

juce::Rectangle<int> StepGrid::rowBounds (int channel) const
{
    return { 0, channel * rowHeight, getWidth(), rowHeight };
}

juce::Rectangle<int> StepGrid::muteBounds (int channel) const
{
    const auto row = rowBounds (channel);
    return { nameWidth - 44, row.getY() + (rowHeight - 26) / 2, 34, 26 };
}

juce::Rectangle<int> StepGrid::stepBounds (int channel, int step) const
{
    const auto row = rowBounds (channel);

    if (step < 0 || step >= audio.visibleSteps())
        return {};

    const auto width = juce::jmax (4, juce::jmax (1, row.getWidth() - nameWidth) / audio.visibleSteps());
    return { nameWidth + step * width, row.getY() + 5, juce::jmax (1, width - 3), row.getHeight() - 10 };
}

void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (background);

    const auto channels = audio.channelCount();

    if (channels == 0)
    {
        g.setColour (textDim);
        g.setFont (uiFont (16.0f));
        g.drawText ("Open a project (.lynx, .flp, .als, .flm) with the OPEN button",
                    getLocalBounds().reduced (20), juce::Justification::centred);
        return;
    }

    const auto playing = audio.isPlaying();
    const auto playhead = audio.playheadStep();

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto row = rowBounds (channel);

        g.setColour (channel % 2 == 0 ? panel : panelAlt);
        g.fillRect (row);

        // name
        auto name = row.withWidth (nameWidth).withTrimmedLeft (10).withTrimmedRight (48);
        g.setColour (text);
        g.setFont (uiFont (15.0f));
        g.drawText (audio.channelName (channel), name, juce::Justification::centredLeft, true);

        // mute
        const auto mute = muteBounds (channel);
        const auto muted = audio.channelMuted (channel);
        g.setColour (muted ? muteRed : juce::Colour (0xff343742));
        g.fillRoundedRectangle (mute.toFloat(), 5.0f);
        g.setColour (muted ? juce::Colours::white : textDim);
        g.setFont (uiFont (13.0f, true));
        g.drawText (muted ? "M" : "m", mute, juce::Justification::centred);

        // steps
        for (int step = 0; step < audio.visibleSteps(); ++step)
        {
            const auto bounds = stepBounds (channel, step);
            const auto on = audio.stepOn (channel, step);
            const auto beat = step % 4 == 0;

            auto colour = on ? accent : (beat ? juce::Colour (0xff33363f) : juce::Colour (0xff2a2d35));

            if (on && playing && step == playhead)
                colour = juce::Colour (0xffffb28c);

            g.setColour (colour);
            g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
        }

        g.setColour (juce::Colour (0xff0d0e12));
        g.fillRect (row.getX(), row.getBottom() - 1, row.getWidth(), 1);
    }

    if (playing)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (stepBounds (0, playhead).withY (0).withHeight (getHeight()).expanded (1, 0));
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    const auto channel = point.y / rowHeight;

    if (channel < 0 || channel >= audio.channelCount())
        return;

    if (muteBounds (channel).contains (point))
    {
        audio.setChannelMuted (channel, ! audio.channelMuted (channel));
        repaint();
        return;
    }

    for (int step = 0; step < audio.visibleSteps(); ++step)
        if (stepBounds (channel, step).contains (point))
        {
            audio.toggleStep (channel, step);
            repaint();
            return;
        }
}

// ---------------------------------------------------------------------------
// RootComponent

RootComponent::RootComponent (AudioEngine& engineToUse) : audio (engineToUse), grid (engineToUse)
{
    setOpaque (true);

    styleLabel (titleLabel, 21.0f, juce::Justification::centredLeft);
    styleLabel (patternLabel, 15.0f, juce::Justification::centred);
    styleLabel (bpmLabel, 17.0f, juce::Justification::centred);

    for (auto* button : { &openButton, &playButton, &rewindButton, &previousPattern, &nextPatternButton, &bpmDown, &bpmUp })
    {
        styleButton (*button);
        addAndMakeVisible (*button);
    }

    addAndMakeVisible (titleLabel);
    addAndMakeVisible (patternLabel);
    addAndMakeVisible (bpmLabel);

    openButton.onClick = [this] { openProject(); };
    playButton.onClick = [this]
    {
        if (! audio.isAudioOpen())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No audio output",
                                                    audio.status());
            return;
        }

        audio.togglePlay();
        refreshLabels();
        grid.repaint();
    };
    rewindButton.onClick = [this] { audio.rewind(); grid.repaint(); };
    previousPattern.onClick = [this] { audio.nextPattern (-1); refreshLabels(); grid.repaint(); };
    nextPatternButton.onClick = [this] { audio.nextPattern (1); refreshLabels(); grid.repaint(); };
    bpmDown.onClick = [this] { audio.nudgeBpm (-1.0); refreshLabels(); };
    bpmUp.onClick = [this] { audio.nudgeBpm (1.0); refreshLabels(); };

    gridViewport.setViewedComponent (&grid, false);
    gridViewport.setScrollBarsShown (true, false);
    gridViewport.setScrollBarThickness (10);
    addAndMakeVisible (gridViewport);

    refreshLabels();
    startTimerHz (30);
}

void RootComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void RootComponent::resized()
{
    auto area = getLocalBounds();
    const auto safe = safeAreaInsetsFor (*this);

    area.removeFromTop (safe.getTop());
    area.removeFromBottom (safe.getBottom());
    area.removeFromLeft (safe.getLeft());
    area.removeFromRight (safe.getRight());

    auto top = area.removeFromTop (54);
    top.reduce (6, 6);

    openButton.setBounds (top.removeFromRight (78));
    top.removeFromRight (6);
    playButton.setBounds (top.removeFromRight (72));
    top.removeFromRight (6);
    rewindButton.setBounds (top.removeFromRight (52));
    top.removeFromRight (8);
    titleLabel.setBounds (top);

    auto bottom = area.removeFromBottom (56);
    bottom.reduce (8, 8);

    bpmUp.setBounds (bottom.removeFromRight (42));
    bpmLabel.setBounds (bottom.removeFromRight (64));
    bpmDown.setBounds (bottom.removeFromRight (42));

    bottom.removeFromRight (14);
    nextPatternButton.setBounds (bottom.removeFromRight (42));
    patternLabel.setBounds (bottom.removeFromRight (juce::jmin (220, juce::jmax (120, bottom.getWidth() / 2))));
    previousPattern.setBounds (bottom.removeFromRight (42));

    gridViewport.setBounds (area);
    grid.setSize (juce::jmax (200, gridViewport.getMaximumVisibleWidth()), grid.contentHeight());
}

void RootComponent::timerCallback()
{
    if (audio.isPlaying())
        grid.repaint();
}

void RootComponent::refreshLabels()
{
    titleLabel.setText (audio.projectName(), juce::dontSendNotification);
    bpmLabel.setText (juce::String (juce::roundToInt (audio.bpm())), juce::dontSendNotification);
    playButton.setButtonText (audio.isPlaying() ? "STOP" : "PLAY");

    if (audio.patternCount() > 0)
        patternLabel.setText ("P" + juce::String (audio.currentPattern() + 1) + " | "
                                  + audio.patternName (audio.currentPattern()),
                              juce::dontSendNotification);
    else
        patternLabel.setText ("No pattern", juce::dontSendNotification);

    previousPattern.setEnabled (audio.patternCount() > 1);
    nextPatternButton.setEnabled (audio.patternCount() > 1);

    grid.setSize (juce::jmax (200, gridViewport.getMaximumVisibleWidth()), grid.contentHeight());
}

void RootComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a project",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*.lynx;*.flp;*.als;*.flm");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto url = fc.getURLResult();

                              if (url.isNotEmpty())
                                  openProjectURL (url);
                          });
}

void RootComponent::openProjectURL (const juce::URL& url)
{
    juce::String error;

    if (! audio.loadProjectURL (url, error))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                               "Could not open " + url.getFileName(), error);

    refreshLabels();
    grid.repaint();
}

void RootComponent::openProjectFile (const juce::File& file)
{
    openProjectURL (juce::URL (file));
}
} // namespace lynx::ios
