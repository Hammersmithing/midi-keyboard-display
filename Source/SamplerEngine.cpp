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
    streamingFormatManager.registerBasicFormats();

    // Initialize disk streamer
    diskStreamer = std::make_unique<DiskStreamer>();
    diskStreamer->setAudioFormatManager(&streamingFormatManager);

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

    // Clear all voices
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.sample = nullptr;
        voice.position = 0.0;
        voice.envStage = EnvelopeStage::Idle;
        voice.envLevel = 0.0f;
    }

    // Prepare streaming voices
    for (auto& voice : streamingVoices)
    {
        voice.prepareToPlay(sampleRate, samplesPerBlock);
    }

    // Start disk streamer if streaming is enabled
    if (streamingEnabled && diskStreamer)
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
    // Available if it has its own samples OR a valid fallback
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

    // If this note uses a fallback, get velocities from the fallback note
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

    // If this note uses a fallback, get layers from the fallback note
    int actualNote = (mapping.fallbackNote >= 0) ? mapping.fallbackNote : midiNote;

    auto actualIt = noteMappings.find(actualNote);
    if (actualIt == noteMappings.end())
        return -1;

    const auto& actualMapping = actualIt->second;

    // Find which velocity layer the velocity falls into
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

    // Parse note letter (C, D, E, F, G, A, B)
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

    // Parse accidental (# or b)
    if (index < upper.length())
    {
        if (upper[index] == '#')
        {
            noteBase++;
            index++;
        }
        else if (upper[index] == 'B' && index + 1 < upper.length() && juce::CharacterFunctions::isDigit(upper[index + 1]))
        {
            // This 'B' is actually a flat (lowercase 'b' in original)
            noteBase--;
            index++;
        }
        else if (noteName[index] == 'b' && index + 1 < noteName.length() && juce::CharacterFunctions::isDigit(noteName[index + 1]))
        {
            noteBase--;
            index++;
        }
    }

    // Parse octave number
    juce::String octaveStr = noteName.substring(index);
    if (octaveStr.isEmpty() || !octaveStr.containsOnly("0123456789-"))
        return -1;

    int octave = octaveStr.getIntValue();

    // MIDI note: C4 = 60, so C0 = 12
    int midiNote = (octave + 1) * 12 + noteBase;

    if (midiNote < 0 || midiNote > 127)
        return -1;

    return midiNote;
}

bool SamplerEngine::parseFileName(const juce::String& fileName, int& note, int& velocity, int& roundRobin) const
{
    // Expected format: NoteName_Velocity_RoundRobin[_OptionalSuffix...].ext
    // Examples: C4_001_02.wav, G#6_033_01.wav, Db3_127_03_soft.wav, A0_040_01_piano.wav

    juce::String baseName = fileName.upToLastOccurrenceOf(".", false, false);

    juce::StringArray parts;
    parts.addTokens(baseName, "_", "");

    if (parts.size() < 3)
        return false;

    // Parse note name (first part)
    note = parseNoteName(parts[0]);
    if (note < 0)
        return false;

    // Parse velocity (second part)
    juce::String velStr = parts[1];
    if (velStr.length() < 1 || !velStr.containsOnly("0123456789"))
        return false;
    velocity = velStr.getIntValue();
    if (velocity < 1 || velocity > 127)
        return false;

    // Parse round-robin (third part)
    juce::String rrStr = parts[2];
    if (rrStr.length() < 1 || !rrStr.containsOnly("0123456789"))
        return false;
    roundRobin = rrStr.getIntValue();
    if (roundRobin < 1 || roundRobin > 3)
        return false;

    // Any additional parts (like "_piano") are ignored as optional suffixes

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
    juce::File folder(folderPath);

    // Create temporary mappings
    std::map<int, NoteMapping> tempMappings;

    // Reset file size counter
    totalInstrumentFileSize = 0;
    int64_t tempTotalSize = 0;

    // Find all audio files
    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    // Structure to hold loaded sample data
    struct LoadedSample
    {
        std::shared_ptr<Sample> sample;
        int note;
        int velocity;
        int roundRobin;
    };

    // Parse filenames first (fast) and prepare load tasks
    std::vector<std::pair<juce::File, std::tuple<int, int, int>>> filesToLoad;
    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (parseFileName(file.getFileName(), note, velocity, roundRobin))
        {
            filesToLoad.push_back({file, {note, velocity, roundRobin}});
            tempTotalSize += file.getSize();
        }
    }

    // Store total file size
    totalInstrumentFileSize = tempTotalSize;

    // Load samples in parallel using std::async
    std::vector<std::future<LoadedSample>> futures;
    futures.reserve(filesToLoad.size());

    for (const auto& [file, params] : filesToLoad)
    {
        auto [note, velocity, roundRobin] = params;

        futures.push_back(std::async(std::launch::async, [file, note, velocity, roundRobin]() -> LoadedSample {
            // Each thread gets its own format manager
            juce::AudioFormatManager localFormatManager;
            localFormatManager.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader(localFormatManager.createReaderFor(file));
            if (!reader)
                return {nullptr, note, velocity, roundRobin};

            auto sample = std::make_shared<Sample>();
            sample->midiNote = note;
            sample->velocity = velocity;
            sample->roundRobin = roundRobin;
            sample->sampleRate = reader->sampleRate;

            // Read audio data
            sample->buffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
            reader->read(&sample->buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

            return {sample, note, velocity, roundRobin};
        }));
    }

    // Collect results from all futures
    for (auto& future : futures)
    {
        auto loaded = future.get();
        if (!loaded.sample)
            continue;

        // Add to temp mappings
        auto& noteMapping = tempMappings[loaded.note];
        noteMapping.midiNote = loaded.note;

        // Find or create velocity layer
        auto it = std::find_if(noteMapping.velocityLayers.begin(), noteMapping.velocityLayers.end(),
            [&loaded](const VelocityLayer& layer) { return layer.velocityValue == loaded.velocity; });

        if (it == noteMapping.velocityLayers.end())
        {
            VelocityLayer newLayer;
            newLayer.velocityValue = loaded.velocity;
            newLayer.roundRobinSamples.fill(nullptr);
            noteMapping.velocityLayers.push_back(newLayer);
            it = noteMapping.velocityLayers.end() - 1;
        }

        // Add sample to round-robin slot
        if (loaded.roundRobin >= 1 && loaded.roundRobin <= 3)
        {
            it->roundRobinSamples[static_cast<size_t>(loaded.roundRobin)] = loaded.sample;
        }
    }

    // Build velocity ranges for temp mappings
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
    for (int note = 0; note < 128; ++note)
    {
        if (tempMappings.find(note) != tempMappings.end())
        {
            tempMappings[note].fallbackNote = -1;
        }
        else
        {
            int fallback = -1;
            for (int higher = note + 1; higher < 128; ++higher)
            {
                if (tempMappings.find(higher) != tempMappings.end())
                {
                    fallback = higher;
                    break;
                }
            }
            if (fallback >= 0)
            {
                tempMappings[note].midiNote = note;
                tempMappings[note].fallbackNote = fallback;
            }
        }
    }

    // Swap in the new mappings (thread-safe)
    {
        std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
        noteMappings = std::move(tempMappings);
    }

    loadingState = LoadingState::Loaded;
}

void SamplerEngine::buildVelocityRanges()
{
    for (auto& [note, mapping] : noteMappings)
    {
        // Sort velocity layers by velocity value ascending
        std::sort(mapping.velocityLayers.begin(), mapping.velocityLayers.end(),
            [](const VelocityLayer& a, const VelocityLayer& b) {
                return a.velocityValue < b.velocityValue;
            });

        // Compute velocity ranges
        // Each velocity covers from (previous velocity + 1) to (this velocity)
        for (size_t i = 0; i < mapping.velocityLayers.size(); ++i)
        {
            auto& layer = mapping.velocityLayers[i];

            if (i == 0)
            {
                layer.velocityRangeStart = 1;
            }
            else
            {
                layer.velocityRangeStart = mapping.velocityLayers[i - 1].velocityValue + 1;
            }

            layer.velocityRangeEnd = layer.velocityValue;
        }
    }
}

void SamplerEngine::buildNoteFallbacks()
{
    // For each possible MIDI note 0-127, find the fallback if it has no samples
    for (int note = 0; note < 128; ++note)
    {
        if (noteMappings.find(note) != noteMappings.end())
        {
            // This note has samples, no fallback needed
            noteMappings[note].fallbackNote = -1;
        }
        else
        {
            // Find the next higher note that has samples
            int fallback = -1;
            for (int higher = note + 1; higher < 128; ++higher)
            {
                if (noteMappings.find(higher) != noteMappings.end())
                {
                    fallback = higher;
                    break;
                }
            }

            // Create a placeholder mapping with the fallback
            if (fallback >= 0)
            {
                noteMappings[note].midiNote = note;
                noteMappings[note].fallbackNote = fallback;
            }
        }
    }
}

std::shared_ptr<Sample> SamplerEngine::findSample(int midiNote, int velocity, int roundRobin, int& actualSampleNote) const
{
    actualSampleNote = midiNote;

    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return nullptr;

    const auto& mapping = it->second;

    // If this note has a fallback, use the fallback note's samples
    int actualNote = (mapping.fallbackNote >= 0) ? mapping.fallbackNote : midiNote;
    actualSampleNote = actualNote;

    auto actualIt = noteMappings.find(actualNote);
    if (actualIt == noteMappings.end())
        return nullptr;

    const auto& actualMapping = actualIt->second;

    // Find the velocity layer that matches
    for (const auto& layer : actualMapping.velocityLayers)
    {
        if (velocity >= layer.velocityRangeStart && velocity <= layer.velocityRangeEnd)
        {
            // Try to get the requested round-robin
            if (roundRobin >= 1 && roundRobin <= 3)
            {
                auto sample = layer.roundRobinSamples[static_cast<size_t>(roundRobin)];
                if (sample)
                    return sample;

                // Fallback: try other round-robin positions
                for (int rr = 1; rr <= 3; ++rr)
                {
                    sample = layer.roundRobinSamples[static_cast<size_t>(rr)];
                    if (sample)
                        return sample;
                }
            }
            break;
        }
    }

    return nullptr;
}

void SamplerEngine::noteOn(int midiNote, int velocity, int roundRobin)
{
    // Route to streaming if enabled
    if (streamingEnabled)
    {
        noteOnStreaming(midiNote, velocity, roundRobin);
        return;
    }

    int actualSampleNote = midiNote;
    auto sample = findSample(midiNote, velocity, roundRobin, actualSampleNote);
    if (!sample)
        return;

    // Calculate pitch ratio: combines sample rate conversion and pitch shift
    double sampleRateRatio = sample->sampleRate / currentSampleRate;
    int semitoneDiff = midiNote - actualSampleNote;
    double pitchRatio = sampleRateRatio * std::pow(2.0, semitoneDiff / 12.0);

    // Find a free voice or steal the oldest one
    Voice* voiceToUse = nullptr;

    // First, look for an inactive voice
    for (auto& voice : voices)
    {
        if (!voice.active)
        {
            voiceToUse = &voice;
            break;
        }
    }

    // If no inactive voice, steal the one with the most progress
    if (!voiceToUse)
    {
        double maxPosition = -1.0;
        for (auto& voice : voices)
        {
            if (voice.position > maxPosition)
            {
                maxPosition = voice.position;
                voiceToUse = &voice;
            }
        }
    }

    if (voiceToUse)
    {
        voiceToUse->sample = sample;
        voiceToUse->position = 0.0;
        voiceToUse->pitchRatio = pitchRatio;
        voiceToUse->midiNote = midiNote;
        voiceToUse->active = true;

        // Start attack phase
        voiceToUse->envStage = EnvelopeStage::Attack;
        voiceToUse->envLevel = 0.0f;
        float attackSamples = adsrParams.attack * static_cast<float>(currentSampleRate);
        voiceToUse->envIncrement = 1.0f / attackSamples;
    }
}

void SamplerEngine::noteOff(int midiNote)
{
    // Route to streaming if enabled
    if (streamingEnabled)
    {
        noteOffStreaming(midiNote);
        return;
    }

    // Find voices playing this note and trigger release
    for (auto& voice : voices)
    {
        if (voice.active && voice.midiNote == midiNote && voice.envStage != EnvelopeStage::Release)
        {
            voice.envStage = EnvelopeStage::Release;
            float releaseSamples = adsrParams.release * static_cast<float>(currentSampleRate);
            // Decrement from current level to 0
            voice.envIncrement = -voice.envLevel / releaseSamples;
        }
    }
}

void SamplerEngine::processBlock(juce::AudioBuffer<float>& buffer)
{
    // Route to streaming if enabled
    if (streamingEnabled)
    {
        processBlockStreaming(buffer);
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    for (auto& voice : voices)
    {
        if (!voice.active || !voice.sample)
            continue;

        const auto& sampleBuffer = voice.sample->buffer;
        const int sampleLength = sampleBuffer.getNumSamples();
        const int sampleChannels = sampleBuffer.getNumChannels();

        // Process each output sample with pitch-shifted interpolation
        for (int i = 0; i < numSamples; ++i)
        {
            // Check if we've reached the end of the sample or envelope finished
            if (voice.position >= static_cast<double>(sampleLength - 1) ||
                voice.envStage == EnvelopeStage::Idle)
            {
                voice.active = false;
                voice.sample = nullptr;
                voice.envStage = EnvelopeStage::Idle;
                break;
            }

            // Process envelope
            voice.envLevel += voice.envIncrement;

            switch (voice.envStage)
            {
                case EnvelopeStage::Attack:
                    if (voice.envLevel >= 1.0f)
                    {
                        voice.envLevel = 1.0f;
                        voice.envStage = EnvelopeStage::Decay;
                        float decaySamples = adsrParams.decay * static_cast<float>(currentSampleRate);
                        voice.envIncrement = (adsrParams.sustain - 1.0f) / decaySamples;
                    }
                    break;

                case EnvelopeStage::Decay:
                    if (voice.envLevel <= adsrParams.sustain)
                    {
                        voice.envLevel = adsrParams.sustain;
                        voice.envStage = EnvelopeStage::Sustain;
                        voice.envIncrement = 0.0f;
                    }
                    break;

                case EnvelopeStage::Sustain:
                    // Stay at sustain level until note off
                    voice.envLevel = adsrParams.sustain;
                    break;

                case EnvelopeStage::Release:
                    if (voice.envLevel <= 0.0f)
                    {
                        voice.envLevel = 0.0f;
                        voice.envStage = EnvelopeStage::Idle;
                        voice.active = false;
                        voice.sample = nullptr;
                        continue;
                    }
                    break;

                case EnvelopeStage::Idle:
                    break;
            }

            // Linear interpolation between two sample points
            int pos0 = static_cast<int>(voice.position);
            int pos1 = pos0 + 1;
            double frac = voice.position - static_cast<double>(pos0);

            // Clamp pos1 to valid range
            if (pos1 >= sampleLength)
                pos1 = sampleLength - 1;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                int srcCh = juce::jmin(ch, sampleChannels - 1);
                const float* src = sampleBuffer.getReadPointer(srcCh);

                // Linear interpolation
                float sample0 = src[pos0];
                float sample1 = src[pos1];
                float interpolated = static_cast<float>(sample0 + (sample1 - sample0) * frac);

                // Apply envelope
                interpolated *= voice.envLevel;

                buffer.addSample(ch, i, interpolated);
            }

            // Advance position by pitch ratio
            voice.position += voice.pitchRatio;
        }
    }
}

// ==================== Streaming Mode Implementation ====================

void SamplerEngine::setStreamingEnabled(bool enabled)
{
    engineDebugLog(">>> setStreamingEnabled(" + juce::String(enabled ? "true" : "false") + ")");

    if (streamingEnabled == enabled)
        return;

    streamingEnabled = enabled;

    if (enabled)
    {
        engineDebugLog("Starting disk streaming thread...");
        if (diskStreamer)
        {
            diskStreamer->startThread();
        }
    }
    else
    {
        engineDebugLog("Stopping disk streaming thread...");
        if (diskStreamer)
        {
            diskStreamer->stopThread();
        }

        for (auto& voice : streamingVoices)
        {
            voice.reset();
        }
    }
}

void SamplerEngine::loadSamplesStreamingFromFolder(const juce::File& folder)
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
    loadingThread = std::make_unique<std::thread>(&SamplerEngine::loadSamplesStreamingInBackground, this, folder.getFullPathName());
}

void SamplerEngine::loadSamplesStreamingInBackground(const juce::String& folderPath)
{
    engineDebugLog("Loading samples in STREAMING mode from: " + folderPath);

    // Reset underrun counter for fresh start
    StreamingVoice::resetUnderrunCount();

    // IMPORTANT: Stop all streaming voices and unregister from DiskStreamer
    // before replacing sample data to prevent race condition crashes
    for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
    {
        streamingVoices[static_cast<size_t>(i)].stopVoice(false);
        if (diskStreamer)
            diskStreamer->unregisterVoice(i);
    }

    // Give DiskStreamer time to finish any in-progress operations
    juce::Thread::sleep(20);

    juce::File folder(folderPath);

    // Clear previous streaming samples
    std::vector<StreamingSample> tempSamples;

    // Reset file size and preload memory counters
    totalInstrumentFileSize = 0;
    preloadMemoryBytes = 0;
    int64_t tempTotalSize = 0;
    int64_t tempPreloadMemory = 0;

    // Find all audio files
    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    engineDebugLog("Found " + juce::String(audioFiles.size()) + " audio files");

    size_t totalPreloadBytes = 0;
    size_t totalFullBytes = 0;

    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (!parseFileName(file.getFileName(), note, velocity, roundRobin))
            continue;

        // Track file size
        tempTotalSize += file.getSize();

        // Create reader
        std::unique_ptr<juce::AudioFormatReader> reader(streamingFormatManager.createReaderFor(file));
        if (!reader)
            continue;

        StreamingSample ss;
        ss.midiNote = note;
        ss.velocity = velocity;
        ss.roundRobin = roundRobin;

        // Fill preload info
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

        // Calculate preload size in frames (preloadSizeKB * 1024 / (channels * 4 bytes))
        int bytesPerSample = 4;
        int preloadBytes = preloadSizeKB * 1024;
        ss.preload.preloadSizeFrames = preloadBytes / (ss.preload.numChannels * bytesPerSample);

        // Cap preload to total sample length
        int framesToPreload = std::min(ss.preload.preloadSizeFrames,
                                        static_cast<int>(ss.preload.totalSampleFrames));

        // Load the preload buffer
        ss.preload.preloadBuffer.setSize(ss.preload.numChannels, framesToPreload);
        reader->read(&ss.preload.preloadBuffer, 0, framesToPreload, 0, true, true);

        // Track memory usage
        int64_t thisPreloadBytes = static_cast<int64_t>(framesToPreload) * ss.preload.numChannels * bytesPerSample;
        tempPreloadMemory += thisPreloadBytes;
        totalPreloadBytes += static_cast<size_t>(thisPreloadBytes);
        totalFullBytes += static_cast<size_t>(ss.preload.totalSampleFrames * ss.preload.numChannels * bytesPerSample);

        double durationSec = static_cast<double>(ss.preload.totalSampleFrames) / ss.preload.sampleRate;
        engineDebugLog("  Loaded: " + ss.preload.name
                      + " | " + juce::String(durationSec, 2) + "s"
                      + " | preload=" + juce::String(framesToPreload) + " frames"
                      + " | streaming=" + juce::String(ss.preload.needsStreaming() ? "YES" : "no"));

        tempSamples.push_back(std::move(ss));
    }

    // Swap in new samples (thread-safe)
    {
        std::lock_guard<std::recursive_mutex> lock(mappingsMutex);
        streamingSamples = std::move(tempSamples);
    }

    // Also build regular noteMappings for UI compatibility
    std::map<int, NoteMapping> tempMappings;
    for (const auto& ss : streamingSamples)
    {
        auto& noteMapping = tempMappings[ss.midiNote];
        noteMapping.midiNote = ss.midiNote;

        // Find or create velocity layer
        auto it = std::find_if(noteMapping.velocityLayers.begin(), noteMapping.velocityLayers.end(),
            [&ss](const VelocityLayer& layer) { return layer.velocityValue == ss.velocity; });

        if (it == noteMapping.velocityLayers.end())
        {
            VelocityLayer newLayer;
            newLayer.velocityValue = ss.velocity;
            newLayer.roundRobinSamples.fill(nullptr);
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

    // Store total file size and preload memory
    totalInstrumentFileSize = tempTotalSize;
    preloadMemoryBytes = tempPreloadMemory;

    engineDebugLog("=== STREAMING MODE SUMMARY ===");
    engineDebugLog("  Total samples: " + juce::String(streamingSamples.size()));
    engineDebugLog("  Total file size: " + juce::String(tempTotalSize / (1024 * 1024)) + " MB");
    int streamingCount = 0;
    for (const auto& ss : streamingSamples)
        if (ss.preload.needsStreaming()) streamingCount++;
    engineDebugLog("  Samples needing streaming: " + juce::String(streamingCount));
    engineDebugLog("  Preload memory: " + juce::String(totalPreloadBytes / 1024) + " KB");
    engineDebugLog("  Full memory (RAM mode): " + juce::String(totalFullBytes / 1024) + " KB");
    engineDebugLog("  Memory savings: " + juce::String((totalFullBytes - totalPreloadBytes) / 1024) + " KB");
    engineDebugLog("==============================");

    // Re-register voices with DiskStreamer after loading
    if (diskStreamer)
    {
        for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
        {
            diskStreamer->registerVoice(i, &streamingVoices[static_cast<size_t>(i)]);
        }
        engineDebugLog("Re-registered " + juce::String(StreamingConstants::maxStreamingVoices) + " voices with DiskStreamer");
    }

    loadingState = LoadingState::Loaded;
}

const SamplerEngine::StreamingSample* SamplerEngine::findStreamingSample(int midiNote, int velocity, int roundRobin) const
{
    // First check if we need to use a fallback note
    int actualNote = midiNote;
    auto it = noteMappings.find(midiNote);
    if (it != noteMappings.end() && it->second.fallbackNote >= 0)
    {
        actualNote = it->second.fallbackNote;
    }

    // Find matching streaming sample
    for (const auto& ss : streamingSamples)
    {
        if (ss.midiNote != actualNote)
            continue;

        // Check velocity range
        auto noteIt = noteMappings.find(actualNote);
        if (noteIt == noteMappings.end())
            continue;

        for (const auto& layer : noteIt->second.velocityLayers)
        {
            if (velocity >= layer.velocityRangeStart && velocity <= layer.velocityRangeEnd)
            {
                if (layer.velocityValue == ss.velocity)
                {
                    // Check round-robin
                    if (ss.roundRobin == roundRobin)
                        return &ss;

                    // Fallback: return any matching sample
                    return &ss;
                }
            }
        }
    }

    return nullptr;
}

void SamplerEngine::noteOnStreaming(int midiNote, int velocity, int roundRobin)
{
    const StreamingSample* ss = findStreamingSample(midiNote, velocity, roundRobin);
    if (!ss)
    {
        engineDebugLog("noteOnStreaming: No sample found for note=" + juce::String(midiNote)
                      + " vel=" + juce::String(velocity) + " rr=" + juce::String(roundRobin));
        return;
    }

    // Find a free streaming voice
    for (size_t i = 0; i < streamingVoices.size(); ++i)
    {
        if (!streamingVoices[i].isActive())
        {
            // Set ADSR parameters
            juce::ADSR::Parameters adsrJuceParams;
            adsrJuceParams.attack = adsrParams.attack;
            adsrJuceParams.decay = adsrParams.decay;
            adsrJuceParams.sustain = adsrParams.sustain;
            adsrJuceParams.release = adsrParams.release;
            streamingVoices[i].setADSRParameters(adsrJuceParams);

            streamingVoices[i].startVoice(&ss->preload, midiNote,
                                           static_cast<float>(velocity) / 127.0f, currentSampleRate);

            engineDebugLog("noteOnStreaming: Started voice " + juce::String(i)
                          + " for note=" + juce::String(midiNote)
                          + " sample=" + ss->preload.name
                          + " streaming=" + juce::String(ss->preload.needsStreaming() ? "YES" : "no"));
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

void SamplerEngine::noteOffStreaming(int midiNote)
{
    for (auto& voice : streamingVoices)
    {
        if (voice.isActive() && voice.getPlayingNote() == midiNote)
        {
            voice.stopVoice(true);  // Allow tail off
        }
    }
}

void SamplerEngine::processBlockStreaming(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    // Update ADSR for all streaming voices
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
    if (!streamingEnabled)
    {
        int count = 0;
        for (const auto& voice : voices)
        {
            if (voice.active)
                ++count;
        }
        return count;
    }

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
    if (!streamingEnabled)
        return 0;

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
    if (!streamingEnabled || !diskStreamer)
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
