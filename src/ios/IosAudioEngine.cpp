#include "ios/IosAudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>

#include "WavReader.h" // tools/: the sample decoder renderwav uses (WAV, WavPack, Ogg Vorbis, MP3)
#include "core/AlsImport.h"
#include "core/FlmImport.h"
#include "core/FlpImport.h"
#include "flp/Flp.h"
#include "engine/ProjectFile.h"

namespace lynx::ios
{
namespace
{
std::vector<std::uint8_t> readAllBytes (const juce::File& file)
{
    juce::MemoryBlock block;

    if (! file.existsAsFile() || ! file.loadFileAsData (block) || block.getSize() == 0)
        return {};

    const auto* bytes = static_cast<const std::uint8_t*> (block.getData());
    return { bytes, bytes + block.getSize() };
}

int bestPatternOf (const lynx::Song& song)
{
    int best = 0;

    for (int i = 0; i < (int) song.patterns.size(); ++i)
        if (song.patterns[(std::size_t) i].notes.size() > song.patterns[(std::size_t) best].notes.size())
            best = i;

    return best;
}
} // namespace

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    alive->store (false);
    close();

    // the renderer may still be referenced by a block the device was rendering: stop it first
    deviceManager.removeAudioCallback (this);
    renderer.reset();
    workers.reset();
}

bool AudioEngine::open()
{
    if (deviceManager.getCurrentAudioDevice() != nullptr)
        return true;

    const auto error = deviceManager.initialise (0, 2, nullptr, true);

    if (error.isNotEmpty())
    {
        statusText = error;
        return false;
    }

    // the worker pool must exist before the device calls audioDeviceAboutToStart (addAudioCallback starts it)
    workers = std::make_unique<lynx::WorkerPool> (std::max (1, juce::SystemStats::getNumCpus() - 1), [] {});

    deviceManager.addAudioCallback (this);

    if (auto* device = deviceManager.getCurrentAudioDevice())
        sampleRate = device->getCurrentSampleRate();

    rebuild();
    return true;
}

void AudioEngine::close()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

// ---------------------------------------------------------------------------
// Projects

bool AudioEngine::loadProject (const juce::File& file, juce::String& error)
{
    stop();
    rewind();

    const auto bytes = readAllBytes (file);

    if (bytes.empty())
    {
        error = "Could not read " + file.getFileName();
        return false;
    }

    const auto extension = file.getFileExtension().toLowerCase();
    const auto folder = file.getParentDirectory();
    juce::String importError;
    std::optional<lynx::Song> loaded;

    auto fileExists = [this] (const std::string& path) { return findSampleFile (path).existsAsFile(); };
    auto readFile = [this] (const std::string& path) { return readAllBytes (findSampleFile (path)); };

    try
    {
        if (extension == ".lynx")
        {
            loaded = lynx::ProjectFile::load (file, importError);
        }
        else if (extension == ".flp")
        {
            lynx::FlpImportOptions options;
            options.projectFolder = folder.getFullPathName().toStdString();
            options.fileExists = fileExists;
            options.readFile = readFile;
            loaded = lynx::importFlp (flp::parseProject (bytes), options);
        }
        else if (extension == ".als")
        {
            lynx::AlsImportOptions options;
            options.common.projectFolder = folder.getFullPathName().toStdString();
            options.common.fileExists = fileExists;
            options.common.readFile = readFile;
            std::string message;
            loaded = lynx::importAls (bytes, options, &message);

            if (! loaded)
                importError = message;
        }
        else if (extension == ".flm")
        {
            lynx::FlmImportOptions options;
            options.common.projectFolder = folder.getFullPathName().toStdString();
            options.common.fileExists = fileExists;
            options.common.readFile = readFile;
            std::string message;
            loaded = lynx::importFlm (bytes, options, &message);

            if (! loaded)
                importError = message;
        }
        else
        {
            error = "Cannot open " + extension + " files yet (use .lynx, .flp, .als, .flm)";
            return false;
        }
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
    catch (...)
    {
        error = "The project could not be imported";
        return false;
    }

    if (! loaded.has_value())
    {
        error = importError.isNotEmpty() ? importError : "The project could not be imported";
        return false;
    }

    projectFolder = folder;
    projectTitle = file.getFileNameWithoutExtension();
    sampleCache.clear();

    editSong = std::move (*loaded);

    if (editSong.patterns.empty())
        editSong.patterns.emplace_back();

    selectedPattern = bestPatternOf (editSong);
    editSong.mode = lynx::PlayMode::pattern;
    editSong.currentPattern = selectedPattern;
    tempo.store (editSong.bpm);

    updateLoopLength();
    rebuild();

    statusText = projectTitle + " - " + juce::String ((int) editSong.channels.size()) + " channels, "
               + juce::String ((int) editSong.patterns.size()) + " patterns";
    return true;
}

bool AudioEngine::loadProjectURL (const juce::URL& url, juce::String& error)
{
    const auto documents = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    // a project that already lives in the app's folder keeps its sibling samples addressable
    if (url.isLocalFile() && url.getLocalFile().isAChildOf (documents))
        return loadProject (url.getLocalFile(), error);

    std::unique_ptr<juce::InputStream> stream (
        url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)));
    juce::MemoryBlock block;

    if (stream != nullptr)
        stream->readIntoMemoryBlock (block);

    if (block.getSize() == 0)
    {
        error = "Could not read "
              + (url.getFileName().isNotEmpty() ? url.getFileName() : juce::String ("the project"))
              + ". Copy it into LYNX in the Files app and open it from there.";
        return false;
    }

    // the URL's security scope ends when the picker closes: materialise a copy inside our sandbox and load that
    const auto imported = documents.getChildFile ("Imported");
    imported.createDirectory();

    auto name = url.getFileName();

    if (name.isEmpty() || name.startsWithChar ('.'))
        name = "project.lynx";

    auto target = imported.getChildFile (name);
    target.deleteFile();

    if (! target.replaceWithData (block.getData(), block.getSize()))
    {
        error = "Could not copy " + name + " into the app's Documents folder";
        return false;
    }

    return loadProject (target, error);
}

juce::String AudioEngine::channelName (int channel) const
{
    if (channel < 0 || channel >= channelCount())
        return {};

    const auto& name = editSong.channels[(std::size_t) channel].name;
    return name.empty() ? "Channel " + juce::String (channel + 1) : juce::String (name);
}

bool AudioEngine::channelMuted (int channel) const
{
    return channel >= 0 && channel < channelCount() && editSong.channels[(std::size_t) channel].muted;
}

void AudioEngine::setChannelMuted (int channel, bool muted)
{
    if (channel < 0 || channel >= channelCount() || editSong.channels[(std::size_t) channel].muted == muted)
        return;

    editSong.channels[(std::size_t) channel].muted = muted;
    rebuild();
}

juce::String AudioEngine::patternName (int index) const
{
    if (index < 0 || index >= patternCount())
        return {};

    const auto& name = editSong.patterns[(std::size_t) index].name;
    return name.empty() ? "Pattern " + juce::String (index + 1) : juce::String (name);
}

void AudioEngine::selectPattern (int index)
{
    if (patternCount() == 0)
        return;

    selectedPattern = juce::jlimit (0, patternCount() - 1, index);
    editSong.currentPattern = selectedPattern;
    rewind();
    updateLoopLength();
    rebuild();
}

void AudioEngine::nextPattern (int delta)
{
    if (patternCount() > 1)
        selectPattern ((selectedPattern + delta + patternCount()) % patternCount());
}

bool AudioEngine::stepOn (int channel, int step) const
{
    return selectedPattern >= 0 && selectedPattern < patternCount() && channel >= 0 && channel < channelCount()
        && step >= 0 && step < visibleSteps()
        && editSong.patterns[(std::size_t) selectedPattern].hasStep (channel, step);
}

void AudioEngine::toggleStep (int channel, int step)
{
    if (selectedPattern < 0 || selectedPattern >= patternCount() || channel < 0 || channel >= channelCount()
        || step < 0 || step >= visibleSteps())
        return;

    editSong.patterns[(std::size_t) selectedPattern].toggleStep (channel, step);
    updateLoopLength();
    rebuild();
}

// ---------------------------------------------------------------------------
// Transport

void AudioEngine::play()
{
    if (live.load (std::memory_order_acquire) != nullptr)
        playing.store (true);
}

void AudioEngine::stop()
{
    playing.store (false);
    tick.store (0.0);
}

void AudioEngine::togglePlay()
{
    if (isPlaying())
        stop();
    else
        play();
}

void AudioEngine::rewind()
{
    tick.store (0.0);
}

void AudioEngine::nudgeBpm (double delta)
{
    const auto next = juce::jlimit (20.0, 999.0, tempo.load() + delta);
    tempo.store (next);
    editSong.bpm = next;
    rebuild();
}

int AudioEngine::playheadStep() const
{
    const auto length = loopLength.load (std::memory_order_relaxed);

    if (length <= 0.0)
        return 0;

    const auto step = (int) (std::fmod (std::max (0.0, positionTicks()), length) / lynx::ticksPerStep);
    return juce::jlimit (0, visibleSteps() - 1, step);
}

// ---------------------------------------------------------------------------
// Snapshot publishing

void AudioEngine::updateLoopLength()
{
    auto length = 0.0;

    if (selectedPattern >= 0 && selectedPattern < patternCount())
        length = (double) editSong.patterns[(std::size_t) selectedPattern].effectiveLength (editSong.meter());

    loopLength.store (length);
}

void AudioEngine::publish (std::unique_ptr<const lynx::RackState> next)
{
    auto* raw = next.get();

    retained.push_back ({ std::move (next), blocks.load (std::memory_order_relaxed) });
    live.store (raw, std::memory_order_release);
    pruneRetained();
}

void AudioEngine::pruneRetained()
{
    // the audio callback may hold a pointer for a block or two; 64 blocks of margin is minutes of audio at any
    // real buffer size, so a replaced snapshot is never freed under a callback
    const auto now = blocks.load (std::memory_order_relaxed);
    const auto* current = live.load (std::memory_order_acquire);

    retained.erase (std::remove_if (retained.begin(), retained.end(),
                                    [now, current] (const Retained& entry) {
                                        return entry.state.get() != current && entry.publishedAtBlock + 64 < now;
                                    }),
                    retained.end());
}

void AudioEngine::rebuild()
{
    if (editSong.channels.empty())
    {
        live.store (nullptr, std::memory_order_release);
        return;
    }

    lynx::RackState::Factories factories;
    factories.samples = [this] (const std::string& path) { return loadSample (path); };

    try
    {
        auto next = lynx::RackState::build (editSong, live.load (std::memory_order_acquire), sampleRate, factories);

        if (next != nullptr)
            publish (std::move (next));
    }
    catch (const std::exception& e)
    {
        statusText = juce::String ("Engine: ") + e.what();
    }
    catch (...)
    {
        statusText = "Engine: the project could not be played";
    }
}

// ---------------------------------------------------------------------------
// Samples

juce::File AudioEngine::findSampleFile (const std::string& path) const
{
    auto normalised = path;
    std::replace (normalised.begin(), normalised.end(), '\\', '/');

    const juce::File direct { juce::String (normalised) };

    if (direct.existsAsFile())
        return direct;

    // imported desktop projects keep their Windows/macOS paths: try next to the project, then a shared Samples folder
    const auto name = direct.getFileName();

    if (name.isNotEmpty())
    {
        const auto besideProject = projectFolder.getChildFile (name);

        if (besideProject.existsAsFile())
            return besideProject;

        const auto shared = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                .getChildFile ("Samples")
                                .getChildFile (name);

        if (shared.existsAsFile())
            return shared;
    }

    return direct;
}

std::shared_ptr<const lynx::SampleData> AudioEngine::loadSample (const std::string& path)
{
    if (const auto found = sampleCache.find (path); found != sampleCache.end())
        return found->second;

    std::shared_ptr<const lynx::SampleData> sample;
    const auto file = findSampleFile (path);

    if (file.existsAsFile())
        sample = lynx::tools::readWav (file.getFullPathName().toStdString());

    sampleCache[path] = sample;
    return sample;
}

// ---------------------------------------------------------------------------
// Device

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device != nullptr ? device->getCurrentSampleRate() : 44100.0;

    if (renderer == nullptr)
        renderer = std::make_unique<lynx::RackRenderer>();

    if (workers != nullptr)
        renderer->setWorkerPool (workers.get());

    renderer->prepare (sampleRate, 4096);
    leftScratch.assign (4096, 0.0f);
    rightScratch.assign (4096, 0.0f);
    lynx::setOfflineRendering (false);

    // the snapshot was built at the previous rate: rebuild it (message thread, allocates)
    auto flag = alive;
    juce::MessageManager::callAsync ([this, flag] {
        if (flag->load())
            rebuild();
    });
}

void AudioEngine::audioDeviceStopped()
{
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputChannelData,
                                                    int numOutputChannels, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    blocks.fetch_add (1, std::memory_order_relaxed);

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    const auto* state = live.load (std::memory_order_acquire);

    if (state == nullptr || renderer == nullptr || numOutputChannels < 1 || outputChannelData[0] == nullptr)
        return;

    if (numSamples > (int) leftScratch.size())
        return;

    const auto isPlaying = playing.load (std::memory_order_relaxed);
    const auto start = tick.load (std::memory_order_relaxed);

    renderer->process (state, isPlaying, start, numSamples, leftScratch.data(), rightScratch.data());

    juce::FloatVectorOperations::copy (outputChannelData[0], leftScratch.data(), numSamples);

    if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
        juce::FloatVectorOperations::copy (outputChannelData[1], rightScratch.data(), numSamples);

    if (isPlaying)
    {
        auto next = start + (double) numSamples * lynx::ticksPerSample (state->song.bpm, sampleRate);
        const auto length = loopLength.load (std::memory_order_relaxed);

        if (length > 0.0 && next >= length)
            next = std::fmod (next, length);

        tick.store (next, std::memory_order_relaxed);
    }
}
} // namespace lynx::ios
