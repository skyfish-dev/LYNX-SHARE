#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

#include "core/RackRenderer.h"
#include "core/WorkerPool.h"

namespace lynx::ios
{
// Plays a Song through the JUCE-free engine (core/RackRenderer) into CoreAudio.
//
// Threading: the UI edits `editSong` and calls rebuild() on the message thread, which publishes an immutable
// RackState through an atomic pointer. The audio thread only reads the published pointer, so it never allocates and
// never locks. Recent states stay alive for a while after they are replaced (the audio callback may still hold one).
class AudioEngine final : public juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    bool open();
    void close();
    bool isAudioOpen() const { return deviceManager.getCurrentAudioDevice() != nullptr; }

    bool loadProject (const juce::File& file, juce::String& error);
    // iOS: the document picker hands out security-scoped URLs that are only readable through the URL's bookmark,
    // so the app reads those bytes itself and copies the project into its own Documents folder.
    bool loadProjectURL (const juce::URL& url, juce::String& error);
    juce::String projectName() const { return projectTitle; }
    juce::String status() const { return statusText; }

    int channelCount() const { return (int) editSong.channels.size(); }
    juce::String channelName (int channel) const;
    bool channelMuted (int channel) const;
    void setChannelMuted (int channel, bool muted);

    int patternCount() const { return (int) editSong.patterns.size(); }
    int currentPattern() const { return selectedPattern; }
    juce::String patternName (int) const;
    void selectPattern (int index);
    void nextPattern (int delta);

    bool stepOn (int channel, int step) const;
    void toggleStep (int channel, int step);
    int visibleSteps() const { return 16; }

    void play();
    void stop();
    void togglePlay();
    void rewind();
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }

    double bpm() const noexcept { return tempo.load (std::memory_order_relaxed); }
    void nudgeBpm (double delta);

    double positionTicks() const noexcept { return tick.load (std::memory_order_relaxed); }
    double loopLengthTicks() const noexcept { return loopLength.load (std::memory_order_relaxed); }
    // The 0-based step the playhead is on (0 .. visibleSteps()-1).
    int playheadStep() const;

    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void rebuild();
    void publish (std::unique_ptr<const lynx::RackState> next);
    void updateLoopLength();
    void pruneRetained();
    juce::File findSampleFile (const std::string& path) const;
    std::shared_ptr<const lynx::SampleData> loadSample (const std::string& path);

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<lynx::WorkerPool> workers;
    std::unique_ptr<lynx::RackRenderer> renderer;

    lynx::Song editSong;
    juce::File projectFolder;
    juce::String projectTitle { "No project" };
    juce::String statusText;

    std::atomic<const lynx::RackState*> live { nullptr };
    struct Retained
    {
        std::unique_ptr<const lynx::RackState> state;
        std::uint64_t publishedAtBlock = 0;
    };
    std::vector<Retained> retained;
    std::map<std::string, std::shared_ptr<const lynx::SampleData>> sampleCache;

    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);

    std::atomic<bool> playing { false };
    std::atomic<double> tick { 0.0 };
    std::atomic<double> tempo { 130.0 };
    std::atomic<double> loopLength { 0.0 };
    std::atomic<std::uint64_t> blocks { 0 };

    int selectedPattern = 0;
    double sampleRate = 44100.0;

    std::vector<float> leftScratch, rightScratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
} // namespace lynx::ios
