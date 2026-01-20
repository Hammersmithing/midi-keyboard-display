#include "SamplerEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

// Debug logging
static void engineDebugLog(const juce::String& msg)
{
    auto logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                       .getChildFile("sampler_streaming_debug.txt");
    auto timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    logFile.appendText("[" + timestamp + "] " + msg + "\n");
}

SamplerEngine::SamplerEngine()
{
    formatManager.registerBasicFormats();

    // Initialize disk streamer
    diskStreamer = std::make_unique<DiskStreamer>();
    diskStreamer->setAudioFormatManager(&formatManager);

    // Register streaming voices with disk streamer
    for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
    {
        diskStreamer->registerVoice(i, &streamingVoices[static_cast<size_t>(i)]);
    }
}

SamplerEngine::~SamplerEngine()
{
    // Stop disk streaming thread
    if (diskStreamer)
    {
        diskStreamer->stopThread();
    }

    // Wait for any loading thread to finish
    if (loadingThread && loadingThread->joinable())
    {
        loadingThread->join();
    }
}

void SamplerEngine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    // Prepare streaming voices
    for (auto& voice : streamingVoices)
    {
        voice.prepareToPlay(sampleRate, samplesPerBlock);
    }

    // Start disk streamer
    if (diskStreamer)
    {
        diskStreamer->startThread();
    }
}

void SamplerEngine::setADSR(float attack, float decay, float sustain, float release)
{
    adsrParams.attack = juce::jmax(0.001f, attack);   // Minimum 1ms
    adsrParams.decay = juce::jmax(0.001f, decay);
    adsrParams.sustain = juce::jlimit(0.0f, 1.0f, sustain);
    adsrParams.release = juce::jmax(0.001f, release);
}

bool SamplerEngine::isLoaded() const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
    return loadingState == LoadingState::Loaded && !noteMappings.empty();
}

bool SamplerEngine::isNoteAvailable(int midiNote) const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return false;

    const auto& mapping = it->second;
    return !mapping.velocityLayers.empty() || mapping.fallbackNote >= 0;
}

bool SamplerEngine::noteHasOwnSamples(int midiNote) const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return false;

    return !it->second.velocityLayers.empty();
}

std::vector<int> SamplerEngine::getVelocityLayers(int midiNote) const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    std::vector<int> velocities;

    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return velocities;

    const auto& mapping = it->second;
    int actualNote = (mapping.fallbackNote >= 0) ? mapping.fallbackNote : midiNote;

    auto actualIt = noteMappings.find(actualNote);
    if (actualIt == noteMappings.end())
        return velocities;

    for (const auto& layer : actualIt->second.velocityLayers)
    {
        velocities.push_back(layer.velocityValue);
    }

    return velocities;
}

int SamplerEngine::getLowestAvailableNote() const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    for (int note = 0; note < 128; ++note)
    {
        if (noteHasOwnSamples(note))
            return note;
    }
    return -1;
}

int SamplerEngine::getHighestAvailableNote() const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    for (int note = 127; note >= 0; --note)
    {
        if (noteHasOwnSamples(note))
            return note;
    }
    return -1;
}

int SamplerEngine::getMaxVelocityLayers(int startNote, int endNote) const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    int maxLayers = 0;
    for (int note = startNote; note < endNote; ++note)
    {
        auto layers = getVelocityLayers(note);
        if (static_cast<int>(layers.size()) > maxLayers)
            maxLayers = static_cast<int>(layers.size());
    }
    return maxLayers;
}

int SamplerEngine::getVelocityLayerIndex(int midiNote, int velocity) const
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return -1;

    const auto& mapping = it->second;
    int actualNote = (mapping.fallbackNote >= 0) ? mapping.fallbackNote : midiNote;

    auto actualIt = noteMappings.find(actualNote);
    if (actualIt == noteMappings.end())
        return -1;

    const auto& actualMapping = actualIt->second;
    int totalLayers = static_cast<int>(actualMapping.velocityLayers.size());
    if (totalLayers == 0)
        return -1;

    // Apply velocity layer limit (same logic as findStreamingSample)
    int effectiveLayers = std::min(velocityLayerLimit, totalLayers);

    // Map incoming velocity (1-127) to limited layer index evenly
    int layerIndex = ((velocity - 1) * effectiveLayers) / 127;
    layerIndex = juce::jlimit(0, effectiveLayers - 1, layerIndex);

    return layerIndex;
}

int SamplerEngine::parseNoteName(const juce::String& noteName) const
{
    if (noteName.isEmpty())
        return -1;

    juce::String upper = noteName.toUpperCase();
    int index = 0;

    char noteLetter = static_cast<char>(upper[0]);
    int noteBase = -1;
    switch (noteLetter)
    {
        case 'C': noteBase = 0; break;
        case 'D': noteBase = 2; break;
        case 'E': noteBase = 4; break;
        case 'F': noteBase = 5; break;
        case 'G': noteBase = 7; break;
        case 'A': noteBase = 9; break;
        case 'B': noteBase = 11; break;
        default: return -1;
    }
    index++;

    if (index < upper.length())
    {
        if (upper[index] == '#')
        {
            noteBase++;
            index++;
        }
        else if (upper[index] == 'B' && index + 1 < upper.length() && juce::CharacterFunctions::isDigit(upper[index + 1]))
        {
            noteBase--;
            index++;
        }
        else if (noteName[index] == 'b' && index + 1 < noteName.length() && juce::CharacterFunctions::isDigit(noteName[index + 1]))
        {
            noteBase--;
            index++;
        }
    }

    juce::String octaveStr = noteName.substring(index);
    if (octaveStr.isEmpty() || !octaveStr.containsOnly("0123456789-"))
        return -1;

    int octave = octaveStr.getIntValue();
    int midiNote = (octave + 1) * 12 + noteBase;

    if (midiNote < 0 || midiNote > 127)
        return -1;

    return midiNote;
}

bool SamplerEngine::parseFileName(const juce::String& fileName, int& note, int& velocity, int& roundRobin) const
{
    juce::String baseName = fileName.upToLastOccurrenceOf(".", false, false);

    juce::StringArray parts;
    parts.addTokens(baseName, "_", "");

    if (parts.size() < 3)
        return false;

    note = parseNoteName(parts[0]);
    if (note < 0)
        return false;

    juce::String velStr = parts[1];
    if (velStr.length() < 1 || !velStr.containsOnly("0123456789"))
        return false;
    velocity = velStr.getIntValue();
    if (velocity < 1 || velocity > 127)
        return false;

    juce::String rrStr = parts[2];
    if (rrStr.length() < 1 || !rrStr.containsOnly("0123456789"))
        return false;
    roundRobin = rrStr.getIntValue();
    if (roundRobin < 1)
        return false;

    return true;
}

void SamplerEngine::loadSamplesFromFolder(const juce::File& folder)
{
    // Wait for any existing loading to complete
    if (loadingThread && loadingThread->joinable())
    {
        loadingThread->join();
    }

    loadedFolderPath = folder.getFullPathName();

    if (!folder.isDirectory())
        return;

    // Start background loading
    loadingState = LoadingState::Loading;
    loadingThread = std::make_unique<std::thread>(&SamplerEngine::loadSamplesInBackground, this, folder.getFullPathName());
}

void SamplerEngine::loadSamplesInBackground(const juce::String& folderPath)
{
    engineDebugLog("Loading samples from: " + folderPath);

    // Reset underrun counter
    StreamingVoice::resetUnderrunCount();

    // Stop all streaming voices and unregister from DiskStreamer
    for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
    {
        streamingVoices[static_cast<size_t>(i)].stopVoice(false);
        if (diskStreamer)
            diskStreamer->unregisterVoice(i);
    }

    juce::Thread::sleep(20);

    juce::File folder(folderPath);

    std::vector<StreamingSample> tempSamples;

    totalInstrumentFileSize = 0;
    preloadMemoryBytes = 0;
    int64_t tempTotalSize = 0;
    int tempMaxRoundRobins = 1;

    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    engineDebugLog("Found " + juce::String(audioFiles.size()) + " audio files");

    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (!parseFileName(file.getFileName(), note, velocity, roundRobin))
            continue;

        // Track max round-robin found
        if (roundRobin > tempMaxRoundRobins)
            tempMaxRoundRobins = roundRobin;

        tempTotalSize += file.getSize();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader)
            continue;

        StreamingSample ss;
        ss.midiNote = note;
        ss.velocity = velocity;
        ss.roundRobin = roundRobin;
        ss.velocityLayerIndex = -1;  // Will be set after building noteMappings
        ss.isPreloaded = false;      // Don't preload yet - will be done by updatePreloadedSamples

        ss.preload.filePath = file.getFullPathName();
        ss.preload.sampleRate = reader->sampleRate;
        ss.preload.numChannels = static_cast<int>(reader->numChannels);
        ss.preload.totalSampleFrames = static_cast<int64_t>(reader->lengthInSamples);
        ss.preload.name = file.getFileNameWithoutExtension();
        ss.preload.rootNote = note;
        ss.preload.lowNote = note;
        ss.preload.highNote = note;
        ss.preload.lowVelocity = velocity;
        ss.preload.highVelocity = velocity;
        ss.preload.preloadSizeFrames = 0;  // Will be set when actually preloaded

        tempSamples.push_back(std::move(ss));
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
        streamingSamples = std::move(tempSamples);
    }

    // Build noteMappings for UI
    std::map<int, NoteMapping> tempMappings;
    for (const auto& ss : streamingSamples)
    {
        auto& noteMapping = tempMappings[ss.midiNote];
        noteMapping.midiNote = ss.midiNote;

        auto it = std::find_if(noteMapping.velocityLayers.begin(), noteMapping.velocityLayers.end(),
            [&ss](const VelocityLayer& layer) { return layer.velocityValue == ss.velocity; });

        if (it == noteMapping.velocityLayers.end())
        {
            VelocityLayer newLayer;
            newLayer.velocityValue = ss.velocity;
            noteMapping.velocityLayers.push_back(newLayer);
        }
    }

    // Build velocity ranges
    for (auto& [note, mapping] : tempMappings)
    {
        std::sort(mapping.velocityLayers.begin(), mapping.velocityLayers.end(),
            [](const VelocityLayer& a, const VelocityLayer& b) {
                return a.velocityValue < b.velocityValue;
            });

        for (size_t i = 0; i < mapping.velocityLayers.size(); ++i)
        {
            auto& layer = mapping.velocityLayers[i];
            if (i == 0)
                layer.velocityRangeStart = 1;
            else
                layer.velocityRangeStart = mapping.velocityLayers[i - 1].velocityValue + 1;
            layer.velocityRangeEnd = layer.velocityValue;
        }
    }

    // Build fallbacks
    for (int n = 0; n < 128; ++n)
    {
        if (tempMappings.find(n) == tempMappings.end())
        {
            int fallback = -1;
            for (int higher = n + 1; higher < 128; ++higher)
            {
                if (tempMappings.find(higher) != tempMappings.end())
                {
                    fallback = higher;
                    break;
                }
            }
            if (fallback >= 0)
            {
                tempMappings[n].midiNote = n;
                tempMappings[n].fallbackNote = fallback;
            }
        }
        else
        {
            tempMappings[n].fallbackNote = -1;
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
        noteMappings = std::move(tempMappings);
    }

    totalInstrumentFileSize = tempTotalSize;
    maxRoundRobins = tempMaxRoundRobins;

    // Calculate max velocity layers across all notes
    int tempMaxVelLayers = 1;
    for (const auto& [note, mapping] : noteMappings)
    {
        int layers = static_cast<int>(mapping.velocityLayers.size());
        if (layers > tempMaxVelLayers)
            tempMaxVelLayers = layers;
    }
    maxVelocityLayersGlobal = tempMaxVelLayers;
    velocityLayerLimit = maxVelocityLayersGlobal;  // Default to max
    roundRobinLimit = maxRoundRobins;  // Default to max

    // Calculate velocityLayerIndex for each sample based on its position in the note's sorted layers
    {
        std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
        for (auto& ss : streamingSamples)
        {
            auto noteIt = noteMappings.find(ss.midiNote);
            if (noteIt != noteMappings.end())
            {
                const auto& layers = noteIt->second.velocityLayers;
                for (size_t i = 0; i < layers.size(); ++i)
                {
                    if (layers[i].velocityValue == ss.velocity)
                    {
                        ss.velocityLayerIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
    }

    engineDebugLog("Loaded " + juce::String(streamingSamples.size()) + " samples (metadata only)");
    engineDebugLog("Max round-robins: " + juce::String(maxRoundRobins));
    engineDebugLog("Max velocity layers: " + juce::String(maxVelocityLayersGlobal));
    engineDebugLog("Total file size: " + juce::String(tempTotalSize / (1024 * 1024)) + " MB");

    // Preload samples that are within the current limits
    updatePreloadedSamples();

    // Re-register voices with DiskStreamer
    if (diskStreamer)
    {
        for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
        {
            diskStreamer->registerVoice(i, &streamingVoices[static_cast<size_t>(i)]);
        }
    }

    loadingState = LoadingState::Loaded;
}

const SamplerEngine::StreamingSample* SamplerEngine::findStreamingSample(int midiNote, int velocity, int roundRobin) const
{
    int actualNote = midiNote;
    auto it = noteMappings.find(midiNote);
    if (it != noteMappings.end() && it->second.fallbackNote >= 0)
    {
        actualNote = it->second.fallbackNote;
    }

    auto noteIt = noteMappings.find(actualNote);
    if (noteIt == noteMappings.end())
        return nullptr;

    const auto& layers = noteIt->second.velocityLayers;
    int totalLayers = static_cast<int>(layers.size());
    if (totalLayers == 0)
        return nullptr;

    // Apply velocity layer limit (use first N layers, redistribute velocity evenly)
    int effectiveLayers = std::min(velocityLayerLimit, totalLayers);

    // Map incoming velocity (1-127) to limited layer index
    // Evenly distribute: velocity 1-127 maps to layers 0 to (effectiveLayers-1)
    int layerIndex = ((velocity - 1) * effectiveLayers) / 127;
    layerIndex = juce::jlimit(0, effectiveLayers - 1, layerIndex);

    // Get the velocity value of the target layer
    int targetVelocity = layers[static_cast<size_t>(layerIndex)].velocityValue;

    // Find the sample with matching note, velocity, and round-robin
    // Only return preloaded samples
    const StreamingSample* fallbackSample = nullptr;
    for (const auto& ss : streamingSamples)
    {
        if (ss.midiNote == actualNote && ss.velocity == targetVelocity && ss.isPreloaded)
        {
            if (ss.roundRobin == roundRobin)
                return &ss;
            // Track a fallback in case exact RR not found
            if (fallbackSample == nullptr)
                fallbackSample = &ss;
        }
    }

    return fallbackSample;
}

void SamplerEngine::noteOn(int midiNote, int velocity, int roundRobin, int sampleOffset)
{
    // Find sample from offset note (for sample borrowing), but play at original midiNote pitch
    int sampleNote = juce::jlimit(0, 127, midiNote + sampleOffset);
    const StreamingSample* ss = findStreamingSample(sampleNote, velocity, roundRobin);
    if (!ss)
        return;

    // Polyphonic same-note: send existing voices to release phase (realistic piano behavior)
    // This lets the old sound decay naturally while the new attack plays
    for (auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.getPlayingNote() == midiNote && !voice.isQuickFadingOut())
        {
            voice.stopVoiceWithCustomRelease(sameNoteReleaseTime, currentSampleRate);
        }
    }

    // Count how many voices are currently playing this note
    int voicesForThisNote = 0;
    for (const auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.getPlayingNote() == midiNote)
        {
            voicesForThisNote++;
        }
    }

    // If we exceed the per-note limit, fade out the oldest voice with 10ms fade (no clicks)
    if (voicesForThisNote >= maxVoicesPerNote)
    {
        uint64_t oldestCounter = UINT64_MAX;
        StreamingVoice* oldestVoice = nullptr;

        for (auto& voice : streamingVoices)
        {
            if (voice.isActive() && voice.getPlayingNote() == midiNote)
            {
                if (voice.getVoiceStartCounter() < oldestCounter)
                {
                    oldestCounter = voice.getVoiceStartCounter();
                    oldestVoice = &voice;
                }
            }
        }

        if (oldestVoice != nullptr)
        {
            oldestVoice->startQuickFadeOut(currentSampleRate);
        }
    }

    // Increment global voice counter for age tracking
    ++voiceStartCounterGlobal;

    // Find a free streaming voice
    for (size_t i = 0; i < streamingVoices.size(); ++i)
    {
        if (!streamingVoices[i].isActive())
        {
            juce::ADSR::Parameters adsrJuceParams;
            adsrJuceParams.attack = adsrParams.attack;
            adsrJuceParams.decay = adsrParams.decay;
            adsrJuceParams.sustain = adsrParams.sustain;
            adsrJuceParams.release = adsrParams.release;
            streamingVoices[i].setADSRParameters(adsrJuceParams);

            streamingVoices[i].startVoice(&ss->preload, midiNote,
                                           static_cast<float>(velocity) / 127.0f, currentSampleRate,
                                           voiceStartCounterGlobal);
            return;
        }
    }

    // No free voice - steal the oldest voice globally (with 10ms fade)
    uint64_t oldestCounter = UINT64_MAX;
    size_t oldestIndex = 0;

    for (size_t i = 0; i < streamingVoices.size(); ++i)
    {
        if (streamingVoices[i].getVoiceStartCounter() < oldestCounter)
        {
            oldestCounter = streamingVoices[i].getVoiceStartCounter();
            oldestIndex = i;
        }
    }

    juce::ADSR::Parameters adsrJuceParams;
    adsrJuceParams.attack = adsrParams.attack;
    adsrJuceParams.decay = adsrParams.decay;
    adsrJuceParams.sustain = adsrParams.sustain;
    adsrJuceParams.release = adsrParams.release;
    streamingVoices[oldestIndex].setADSRParameters(adsrJuceParams);
    streamingVoices[oldestIndex].startQuickFadeOut(currentSampleRate);

    // Start the new voice after a brief delay would be ideal, but for simplicity
    // we find another free voice or use a different slot
    // Actually, let's just start it - the old voice will fade out
    for (size_t i = 0; i < streamingVoices.size(); ++i)
    {
        if (!streamingVoices[i].isActive())
        {
            streamingVoices[i].setADSRParameters(adsrJuceParams);
            streamingVoices[i].startVoice(&ss->preload, midiNote,
                                           static_cast<float>(velocity) / 127.0f, currentSampleRate,
                                           voiceStartCounterGlobal);
            return;
        }
    }

    // Still no free voice - force steal the oldest one immediately
    streamingVoices[oldestIndex].stopVoice(false);
    streamingVoices[oldestIndex].startVoice(&ss->preload, midiNote,
                                             static_cast<float>(velocity) / 127.0f, currentSampleRate,
                                             voiceStartCounterGlobal);
}

void SamplerEngine::noteOff(int midiNote)
{
    for (auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.getPlayingNote() == midiNote)
        {
            voice.stopVoice(true);  // Allow tail off
        }
    }
}

void SamplerEngine::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    juce::ADSR::Parameters adsrJuceParams;
    adsrJuceParams.attack = adsrParams.attack;
    adsrJuceParams.decay = adsrParams.decay;
    adsrJuceParams.sustain = adsrParams.sustain;
    adsrJuceParams.release = adsrParams.release;

    for (auto& voice : streamingVoices)
    {
        voice.setADSRParameters(adsrJuceParams);

        if (voice.isActive())
        {
            voice.renderNextBlock(buffer, 0, numSamples);
        }
    }
}

int SamplerEngine::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : streamingVoices)
    {
        if (voice.isActive())
            ++count;
    }
    return count;
}

int SamplerEngine::getStreamingVoiceCount() const
{
    int count = 0;
    for (const auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.needsMoreData())
            ++count;
    }
    return count;
}

float SamplerEngine::getDiskThroughputMBps() const
{
    if (!diskStreamer)
        return 0.0f;

    return diskStreamer->getThroughputMBps();
}

int SamplerEngine::getUnderrunCount() const
{
    return StreamingVoice::getUnderrunCount();
}

void SamplerEngine::resetUnderrunCount()
{
    StreamingVoice::resetUnderrunCount();
}

void SamplerEngine::reloadPreloadBuffers()
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    int64_t totalPreloadBytes = 0;
    int reloadedCount = 0;

    for (auto& ss : streamingSamples)
    {
        if (ss.isPreloaded)
        {
            // Reload this sample's preload buffer with new size
            loadSamplePreloadBuffer(ss);
            reloadedCount++;
        }

        if (ss.isPreloaded)
        {
            totalPreloadBytes += static_cast<int64_t>(ss.preload.preloadBuffer.getNumSamples()) *
                                 static_cast<int64_t>(ss.preload.numChannels) * static_cast<int64_t>(sizeof(float));
        }
    }

    preloadMemoryBytes = totalPreloadBytes;

    engineDebugLog("reloadPreloadBuffers: preloadSizeKB=" + juce::String(preloadSizeKB) +
                   " reloaded=" + juce::String(reloadedCount) +
                   " preloadMem=" + juce::String(totalPreloadBytes / 1024) + " KB");
}

void SamplerEngine::setVelocityLayerLimit(int limit)
{
    int newLimit = juce::jlimit(1, juce::jmax(1, maxVelocityLayersGlobal), limit);
    if (newLimit != velocityLayerLimit)
    {
        velocityLayerLimit = newLimit;
        updatePreloadedSamples();
    }
}

void SamplerEngine::setRoundRobinLimit(int limit)
{
    int newLimit = juce::jlimit(1, juce::jmax(1, maxRoundRobins), limit);
    if (newLimit != roundRobinLimit)
    {
        roundRobinLimit = newLimit;
        updatePreloadedSamples();
    }
}

bool SamplerEngine::shouldSampleBePreloaded(const StreamingSample& ss) const
{
    // Sample should be preloaded if:
    // 1. Its velocity layer index is within the limit (0 to velocityLayerLimit-1)
    // 2. Its round robin is within the limit (1 to roundRobinLimit)
    return (ss.velocityLayerIndex >= 0 &&
            ss.velocityLayerIndex < velocityLayerLimit &&
            ss.roundRobin >= 1 &&
            ss.roundRobin <= roundRobinLimit);
}

void SamplerEngine::loadSamplePreloadBuffer(StreamingSample& ss)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        formatManager.createReaderFor(juce::File(ss.preload.filePath)));
    if (!reader)
        return;

    int bytesPerSample = sizeof(float);
    int preloadBytes = preloadSizeKB * 1024;
    int framesToPreload = preloadBytes / (ss.preload.numChannels * bytesPerSample);
    framesToPreload = std::min(framesToPreload, static_cast<int>(ss.preload.totalSampleFrames));

    ss.preload.preloadBuffer.setSize(ss.preload.numChannels, framesToPreload);
    reader->read(&ss.preload.preloadBuffer, 0, framesToPreload, 0, true, true);
    ss.preload.preloadSizeFrames = framesToPreload;
}

void SamplerEngine::updatePreloadedSamples()
{
    std::lock_guard<std::recursive_mutex> lock(mappingsMutex);

    int64_t totalPreloadBytes = 0;
    int loadedCount = 0;
    int unloadedCount = 0;

    for (auto& ss : streamingSamples)
    {
        bool shouldBeLoaded = shouldSampleBePreloaded(ss);

        if (shouldBeLoaded && !ss.isPreloaded)
        {
            // Load this sample's preload buffer from disk
            loadSamplePreloadBuffer(ss);
            ss.isPreloaded = true;
            loadedCount++;
        }
        else if (!shouldBeLoaded && ss.isPreloaded)
        {
            // Unload this sample's preload buffer
            ss.preload.preloadBuffer.setSize(0, 0);  // Free memory
            ss.preload.preloadSizeFrames = 0;
            ss.isPreloaded = false;
            unloadedCount++;
        }

        if (ss.isPreloaded)
        {
            totalPreloadBytes += static_cast<int64_t>(ss.preload.preloadBuffer.getNumSamples()) *
                                 static_cast<int64_t>(ss.preload.numChannels) * static_cast<int64_t>(sizeof(float));
        }
    }

    preloadMemoryBytes = totalPreloadBytes;

    engineDebugLog("updatePreloadedSamples: velLimit=" + juce::String(velocityLayerLimit) +
                   " rrLimit=" + juce::String(roundRobinLimit) +
                   " loaded=" + juce::String(loadedCount) +
                   " unloaded=" + juce::String(unloadedCount) +
                   " preloadMem=" + juce::String(totalPreloadBytes / 1024) + " KB");
}
