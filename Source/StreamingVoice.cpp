#include "StreamingVoice.h"

// Static underrun counter definition
std::atomic<int> StreamingVoice::underrunCount{0};

// Debug logging to file
static void voiceDebugLog(const juce::String& msg)
{
    auto logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                       .getChildFile("sampler_streaming_debug.txt");
    auto timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    logFile.appendText("[" + timestamp + "] " + msg + "\n");
}

StreamingVoice::StreamingVoice()
{
    // Allocate ring buffer for stereo audio
    ringBuffer.setSize(2, StreamingConstants::ringBufferFrames);
    ringBuffer.clear();
}

StreamingVoice::~StreamingVoice() = default;

void StreamingVoice::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    adsr.setSampleRate(sampleRate);
}

void StreamingVoice::setADSRParameters(const juce::ADSR::Parameters& params)
{
    adsr.setParameters(params);
}

void StreamingVoice::startVoice(const PreloadedSample* sample, int midiNote, float vel, double hostSampleRate)
{
    if (sample == nullptr || !sample->isValid())
        return;

    currentSample = sample;
    playingNote = midiNote;
    velocity = vel;

    // Calculate pitch ratio based on root note
    double frequencyOfNote = juce::MidiMessage::getMidiNoteInHertz(midiNote);
    double frequencyOfRoot = juce::MidiMessage::getMidiNoteInHertz(sample->rootNote);
    pitchRatio = frequencyOfNote / frequencyOfRoot;

    // Adjust for sample rate difference
    pitchRatio *= sample->sampleRate / hostSampleRate;

    // Reset positions
    sourceSamplePosition = 0.0;
    readPosition.store(0, std::memory_order_release);
    writePosition.store(0, std::memory_order_release);
    fileReadPosition.store(0, std::memory_order_release);

    // Reset flags
    endOfFile.store(false, std::memory_order_release);
    readError.store(false, std::memory_order_release);
    isUnderrunning = false;
    underrunFadePosition = 0;
    sustainedByPedal = false;

    // Copy preload buffer into beginning of ring buffer
    const auto& preload = sample->preloadBuffer;
    int preloadFrames = preload.getNumSamples();
    int framesToCopy = std::min(preloadFrames, StreamingConstants::ringBufferFrames);

    ringBuffer.clear();
    for (int ch = 0; ch < std::min(preload.getNumChannels(), ringBuffer.getNumChannels()); ++ch)
    {
        ringBuffer.copyFrom(ch, 0, preload, ch, 0, framesToCopy);
    }

    // Set initial write position after preloaded data
    writePosition.store(framesToCopy, std::memory_order_release);
    fileReadPosition.store(framesToCopy, std::memory_order_release);

    // Start envelope
    adsr.noteOn();

    // Signal that we need more data (disk thread will start filling)
    if (sample->needsStreaming())
    {
        needsData.store(true, std::memory_order_release);
    }

    // Mark voice as active last (ensures all state is visible to disk thread)
    active.store(true, std::memory_order_release);

    voiceDebugLog("StreamingVoice::startVoice - note=" + juce::String(midiNote)
                 + " sample=" + sample->name
                 + " totalFrames=" + juce::String(sample->totalSampleFrames)
                 + " preloadFrames=" + juce::String(sample->preloadSizeFrames)
                 + " needsStreaming=" + juce::String(sample->needsStreaming() ? "YES" : "no")
                 + " pitchRatio=" + juce::String(pitchRatio, 4));
}

void StreamingVoice::stopVoice(bool allowTailOff)
{
    if (allowTailOff)
    {
        adsr.noteOff();
    }
    else
    {
        reset();
    }
}

void StreamingVoice::reset()
{
    active.store(false, std::memory_order_release);
    needsData.store(false, std::memory_order_release);
    adsr.reset();
    playingNote = -1;
    sustainedByPedal = false;
    currentSample = nullptr;
}

void StreamingVoice::noteReleasedWithPedal(bool pedalDown)
{
    if (pedalDown)
    {
        sustainedByPedal = true;
    }
    else
    {
        adsr.noteOff();
    }
}

void StreamingVoice::setSustainPedal(bool isDown)
{
    if (!isDown && sustainedByPedal)
    {
        sustainedByPedal = false;
        adsr.noteOff();
    }
}

int StreamingVoice::samplesAvailable() const
{
    int64_t read = readPosition.load(std::memory_order_acquire);
    int64_t write = writePosition.load(std::memory_order_acquire);
    return static_cast<int>(write - read);
}

int StreamingVoice::spaceAvailable() const
{
    return StreamingConstants::ringBufferFrames - samplesAvailable();
}

float* StreamingVoice::getWritePointer(int channel)
{
    return ringBuffer.getWritePointer(channel);
}

void StreamingVoice::advanceWritePosition(int frames)
{
    writePosition.fetch_add(frames, std::memory_order_release);
}

void StreamingVoice::checkAndRequestData()
{
    if (currentSample == nullptr || !currentSample->needsStreaming())
        return;

    if (hasReachedEndOfFile() || hasReadError())
        return;

    int available = samplesAvailable();
    if (available < StreamingConstants::lowWatermarkFrames)
    {
        needsData.store(true, std::memory_order_release);
    }
}

float StreamingVoice::readFromRingBuffer(int channel, int ringPos)
{
    // Wrap position within ring buffer
    int wrappedPos = ringPos % StreamingConstants::ringBufferFrames;
    if (wrappedPos < 0)
        wrappedPos += StreamingConstants::ringBufferFrames;

    return ringBuffer.getSample(channel, wrappedPos);
}

void StreamingVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!active.load(std::memory_order_acquire) || currentSample == nullptr)
        return;

    const int numOutputChannels = outputBuffer.getNumChannels();
    const int numSourceChannels = currentSample->numChannels;
    const int64_t totalSourceFrames = currentSample->totalSampleFrames;
    const bool isStreaming = currentSample->needsStreaming();

    int64_t currentReadPos = readPosition.load(std::memory_order_acquire);
    int64_t currentWritePos = writePosition.load(std::memory_order_acquire);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Check if we've reached end of sample
        if (sourceSamplePosition >= totalSourceFrames)
        {
            reset();
            return;
        }

        // Get envelope value
        float envelopeValue = adsr.getNextSample();
        if (!adsr.isActive())
        {
            reset();
            return;
        }

        // Handle underrun
        if (isStreaming)
        {
            int available = static_cast<int>(currentWritePos - currentReadPos);
            if (available <= 2 && !hasReachedEndOfFile())
            {
                // Buffer underrun - fade out to avoid click
                if (!isUnderrunning)
                {
                    isUnderrunning = true;
                    underrunFadePosition = 0;
                    underrunCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        // Calculate underrun fade
        float underrunFade = 1.0f;
        if (isUnderrunning)
        {
            underrunFade = 1.0f - (static_cast<float>(underrunFadePosition) / StreamingConstants::underrunFadeOutSamples);
            if (underrunFade <= 0.0f)
            {
                // Fully faded out due to underrun - stop voice
                reset();
                return;
            }
            underrunFadePosition++;
        }

        // Calculate interpolated sample position
        int64_t pos0 = static_cast<int64_t>(sourceSamplePosition);
        int64_t pos1 = pos0 + 1;
        float frac = static_cast<float>(sourceSamplePosition - static_cast<double>(pos0));

        // Clamp pos1 to valid range
        if (pos1 >= totalSourceFrames)
            pos1 = pos0;

        // Read from appropriate source (preload buffer or ring buffer)
        for (int ch = 0; ch < numOutputChannels; ++ch)
        {
            float sample0, sample1;

            int sourceChannel = std::min(ch, numSourceChannels - 1);

            if (!isStreaming)
            {
                // Small sample - read directly from preload buffer
                const auto& preload = currentSample->preloadBuffer;
                sample0 = preload.getSample(sourceChannel, static_cast<int>(pos0));
                sample1 = preload.getSample(sourceChannel, static_cast<int>(pos1));
            }
            else
            {
                // Streaming - read from ring buffer with wraparound
                int ringPos0 = static_cast<int>(pos0 % StreamingConstants::ringBufferFrames);
                int ringPos1 = static_cast<int>(pos1 % StreamingConstants::ringBufferFrames);

                sample0 = ringBuffer.getSample(sourceChannel, ringPos0);
                sample1 = ringBuffer.getSample(sourceChannel, ringPos1);
            }

            // Linear interpolation
            float interpolated = sample0 + frac * (sample1 - sample0);

            // Apply velocity, envelope, and underrun fade
            outputBuffer.addSample(ch, startSample + sample,
                                    interpolated * velocity * envelopeValue * underrunFade);
        }

        // Advance source position
        sourceSamplePosition += pitchRatio;

        // Update read position for ring buffer (whole frames consumed)
        if (isStreaming)
        {
            int64_t newReadFrame = static_cast<int64_t>(sourceSamplePosition);
            if (newReadFrame > currentReadPos)
            {
                currentReadPos = newReadFrame;
            }
        }
    }

    // Update atomic read position after processing block
    if (isStreaming)
    {
        readPosition.store(static_cast<int64_t>(sourceSamplePosition), std::memory_order_release);
        checkAndRequestData();

        // Periodic debug logging of ring buffer state
        static int debugBlockCounter = 0;
        if (++debugBlockCounter % 100 == 0)  // Every ~2 seconds at 512 samples/block
        {
            voiceDebugLog("Voice render: readPos=" + juce::String(readPosition.load())
                         + " writePos=" + juce::String(writePosition.load())
                         + " available=" + juce::String(samplesAvailable())
                         + " sourcePos=" + juce::String(static_cast<int64_t>(sourceSamplePosition))
                         + " / " + juce::String(totalSourceFrames)
                         + " needsData=" + juce::String(needsData.load() ? "yes" : "no"));
        }
    }
}
