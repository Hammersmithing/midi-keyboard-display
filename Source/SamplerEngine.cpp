#include "SamplerEngine.h"
#include <algorithm>
#include <cmath>

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

    for (size_t i = 0; i < actualMapping.velocityLayers.size(); ++i)
    {
        const auto& layer = actualMapping.velocityLayers[i];
        if (velocity >= layer.velocityRangeStart && velocity <= layer.velocityRangeEnd)
            return static_cast<int>(i);
    }

    return -1;
}

int SamplerEngine::parseNoteName(const juce::String& noteName) const
{
    if (noteName.isEmpty())
        return -1;

    juce::String upper = noteName.toUpperCase();
    int index = 0;

    char noteLetter = upper[0];
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
    if (roundRobin < 1 || roundRobin > 3)
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
    int64_t tempPreloadMemory = 0;

    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    engineDebugLog("Found " + juce::String(audioFiles.size()) + " audio files");

    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (!parseFileName(file.getFileName(), note, velocity, roundRobin))
            continue;

        tempTotalSize += file.getSize();

        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
        if (!reader)
            continue;

        StreamingSample ss;
        ss.midiNote = note;
        ss.velocity = velocity;
        ss.roundRobin = roundRobin;

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

        int bytesPerSample = 4;
        int preloadBytes = preloadSizeKB * 1024;
        ss.preload.preloadSizeFrames = preloadBytes / (ss.preload.numChannels * bytesPerSample);

        int framesToPreload = std::min(ss.preload.preloadSizeFrames,
                                        static_cast<int>(ss.preload.totalSampleFrames));

        ss.preload.preloadBuffer.setSize(ss.preload.numChannels, framesToPreload);
        reader->read(&ss.preload.preloadBuffer, 0, framesToPreload, 0, true, true);

        int64_t thisPreloadBytes = static_cast<int64_t>(framesToPreload) * ss.preload.numChannels * bytesPerSample;
        tempPreloadMemory += thisPreloadBytes;

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
    preloadMemoryBytes = tempPreloadMemory;

    engineDebugLog("Loaded " + juce::String(streamingSamples.size()) + " samples");
    engineDebugLog("Total file size: " + juce::String(tempTotalSize / (1024 * 1024)) + " MB");
    engineDebugLog("Preload memory: " + juce::String(tempPreloadMemory / 1024) + " KB");

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

    for (const auto& ss : streamingSamples)
    {
        if (ss.midiNote != actualNote)
            continue;

        auto noteIt = noteMappings.find(actualNote);
        if (noteIt == noteMappings.end())
            continue;

        for (const auto& layer : noteIt->second.velocityLayers)
        {
            if (velocity >= layer.velocityRangeStart && velocity <= layer.velocityRangeEnd)
            {
                if (layer.velocityValue == ss.velocity)
                {
                    if (ss.roundRobin == roundRobin)
                        return &ss;
                    return &ss;
                }
            }
        }
    }

    return nullptr;
}

void SamplerEngine::noteOn(int midiNote, int velocity, int roundRobin, int sampleOffset)
{
    // Find sample from offset note (for sample borrowing), but play at original midiNote pitch
    int sampleNote = juce::jlimit(0, 127, midiNote + sampleOffset);
    const StreamingSample* ss = findStreamingSample(sampleNote, velocity, roundRobin);
    if (!ss)
        return;

    // Same-note voice stealing with quick 10ms fadeout
    for (auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.getPlayingNote() == midiNote)
        {
            voice.startQuickFadeOut(currentSampleRate);
        }
    }

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
                                           static_cast<float>(velocity) / 127.0f, currentSampleRate);
            return;
        }
    }

    // No free voice - steal the first one
    juce::ADSR::Parameters adsrJuceParams;
    adsrJuceParams.attack = adsrParams.attack;
    adsrJuceParams.decay = adsrParams.decay;
    adsrJuceParams.sustain = adsrParams.sustain;
    adsrJuceParams.release = adsrParams.release;
    streamingVoices[0].setADSRParameters(adsrJuceParams);
    streamingVoices[0].stopVoice(false);
    streamingVoices[0].startVoice(&ss->preload, midiNote,
                                   static_cast<float>(velocity) / 127.0f, currentSampleRate);
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
