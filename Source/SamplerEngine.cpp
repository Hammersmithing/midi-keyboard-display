#include "SamplerEngine.h"
#include <algorithm>
#include <cmath>

SamplerEngine::SamplerEngine()
{
    formatManager.registerBasicFormats();
}

SamplerEngine::~SamplerEngine()
{
    // Wait for any loading thread to finish
    if (loadingThread && loadingThread->joinable())
    {
        loadingThread->join();
    }
}

void SamplerEngine::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
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
}

void SamplerEngine::setADSR(float attack, float decay, float sustain, float release)
{
    adsrParams.attack = juce::jmax(0.001f, attack);   // Minimum 1ms
    adsrParams.decay = juce::jmax(0.001f, decay);
    adsrParams.sustain = juce::jlimit(0.0f, 1.0f, sustain);
    adsrParams.release = juce::jmax(0.001f, release);
}

bool SamplerEngine::isNoteAvailable(int midiNote) const
{
    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return false;

    const auto& mapping = it->second;
    // Available if it has its own samples OR a valid fallback
    return !mapping.velocityLayers.empty() || mapping.fallbackNote >= 0;
}

bool SamplerEngine::noteHasOwnSamples(int midiNote) const
{
    auto it = noteMappings.find(midiNote);
    if (it == noteMappings.end())
        return false;

    return !it->second.velocityLayers.empty();
}

std::vector<int> SamplerEngine::getVelocityLayers(int midiNote) const
{
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
    for (int note = 0; note < 128; ++note)
    {
        if (noteHasOwnSamples(note))
            return note;
    }
    return -1;
}

int SamplerEngine::getHighestAvailableNote() const
{
    for (int note = 127; note >= 0; --note)
    {
        if (noteHasOwnSamples(note))
            return note;
    }
    return -1;
}

int SamplerEngine::getMaxVelocityLayers(int startNote, int endNote) const
{
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

    // Create a local format manager for this thread
    juce::AudioFormatManager threadFormatManager;
    threadFormatManager.registerBasicFormats();

    // Find all audio files
    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (!parseFileName(file.getFileName(), note, velocity, roundRobin))
            continue;

        // Load the audio file
        std::unique_ptr<juce::AudioFormatReader> reader(threadFormatManager.createReaderFor(file));
        if (!reader)
            continue;

        auto sample = std::make_shared<Sample>();
        sample->midiNote = note;
        sample->velocity = velocity;
        sample->roundRobin = roundRobin;
        sample->sampleRate = reader->sampleRate;

        // Read audio data
        sample->buffer.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
        reader->read(&sample->buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, true);

        // Add to temp mappings
        auto& noteMapping = tempMappings[note];
        noteMapping.midiNote = note;

        // Find or create velocity layer
        auto it = std::find_if(noteMapping.velocityLayers.begin(), noteMapping.velocityLayers.end(),
            [velocity](const VelocityLayer& layer) { return layer.velocityValue == velocity; });

        if (it == noteMapping.velocityLayers.end())
        {
            VelocityLayer newLayer;
            newLayer.velocityValue = velocity;
            newLayer.roundRobinSamples.fill(nullptr);
            noteMapping.velocityLayers.push_back(newLayer);
            it = noteMapping.velocityLayers.end() - 1;
        }

        // Add sample to round-robin slot
        if (roundRobin >= 1 && roundRobin <= 3)
        {
            it->roundRobinSamples[static_cast<size_t>(roundRobin)] = sample;
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
        std::lock_guard<std::mutex> lock(mappingsMutex);
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
    int actualSampleNote = midiNote;
    auto sample = findSample(midiNote, velocity, roundRobin, actualSampleNote);
    if (!sample)
        return;

    // Calculate pitch ratio: how much to shift to go from actualSampleNote to midiNote
    // If midiNote < actualSampleNote, we need to pitch DOWN (ratio < 1.0)
    // Formula: ratio = 2^((midiNote - actualSampleNote) / 12)
    int semitoneDiff = midiNote - actualSampleNote;
    double pitchRatio = std::pow(2.0, semitoneDiff / 12.0);

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
