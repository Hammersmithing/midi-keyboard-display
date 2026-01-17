#include "SamplerEngine.h"
#include <algorithm>
#include <cmath>

SamplerEngine::SamplerEngine()
{
    formatManager.registerBasicFormats();
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
    // Expected format: NoteName_Velocity_RoundRobin[_OptionalSuffix].ext
    // Examples: C4_001_02.wav, G#6_033_01.wav, Db3_127_03_soft.wav

    juce::String baseName = fileName.upToLastOccurrenceOf(".", false, false);

    juce::StringArray parts;
    parts.addTokens(baseName, "_", "");

    if (parts.size() < 3)
        return false;

    // Parse note name (first part)
    note = parseNoteName(parts[0]);
    if (note < 0)
        return false;

    // Parse velocity (second part, 3 digits)
    juce::String velStr = parts[1];
    if (velStr.length() < 1 || !velStr.containsOnly("0123456789"))
        return false;
    velocity = velStr.getIntValue();
    if (velocity < 1 || velocity > 127)
        return false;

    // Parse round-robin (third part, 2 digits)
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
    noteMappings.clear();
    loadedFolderPath = folder.getFullPathName();

    if (!folder.isDirectory())
        return;

    // Find all audio files
    juce::Array<juce::File> audioFiles;
    folder.findChildFiles(audioFiles, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3");

    for (const auto& file : audioFiles)
    {
        int note, velocity, roundRobin;
        if (!parseFileName(file.getFileName(), note, velocity, roundRobin))
            continue;

        // Load the audio file
        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
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

        // Add to note mappings
        auto& noteMapping = noteMappings[note];
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

    // Sort velocity layers and compute ranges
    buildVelocityRanges();

    // Build fallbacks for missing notes
    buildNoteFallbacks();
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
