#include "DiskStreamer.h"

// Debug logging to file (same as PluginProcessor)
static void streamDebugLog(const juce::String& msg)
{
    auto logFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                       .getChildFile("sampler_streaming_debug.txt");
    auto timestamp = juce::Time::getCurrentTime().toString(true, true, true, true);
    logFile.appendText("[" + timestamp + "] " + msg + "\n");
}

DiskStreamer::DiskStreamer()
    : juce::Thread("DiskStreamer")
{
    // Initialize all voice pointers to null
    for (auto& voice : voices)
        voice.store(nullptr, std::memory_order_relaxed);

    // Allocate temporary buffer for disk reads (stereo)
    tempReadBuffer.setSize(2, StreamingConstants::diskReadFrames);
}

DiskStreamer::~DiskStreamer()
{
    stopThread();
}

void DiskStreamer::startThread()
{
    juce::Thread::startThread();
}

void DiskStreamer::stopThread()
{
    signalThreadShouldExit();
    notify();  // Wake up the thread if it's waiting
    juce::Thread::stopThread(1000);

    // Clean up readers
    for (auto& reader : readers)
        reader.reset();
}

void DiskStreamer::registerVoice(int voiceIndex, StreamingVoice* voice)
{
    if (voiceIndex >= 0 && voiceIndex < StreamingConstants::maxStreamingVoices)
    {
        voices[static_cast<size_t>(voiceIndex)].store(voice, std::memory_order_release);
    }
}

void DiskStreamer::unregisterVoice(int voiceIndex)
{
    if (voiceIndex >= 0 && voiceIndex < StreamingConstants::maxStreamingVoices)
    {
        voices[static_cast<size_t>(voiceIndex)].store(nullptr, std::memory_order_release);
        closeReader(voiceIndex);
    }
}

void DiskStreamer::run()
{
    streamDebugLog(">>> DiskStreamer thread STARTED");
    int loopCount = 0;
    int lastActiveVoices = 0;
    lastThroughputTime = juce::Time::getMillisecondCounterHiRes();

    while (!threadShouldExit())
    {
        loopCount++;
        int activeVoices = 0;

        // Poll all registered voices
        for (int i = 0; i < StreamingConstants::maxStreamingVoices; ++i)
        {
            if (threadShouldExit())
                break;

            StreamingVoice* voice = voices[static_cast<size_t>(i)].load(std::memory_order_acquire);
            if (voice != nullptr && voice->isActive())
            {
                activeVoices++;
                if (voice->needsMoreData())
                {
                    fillVoiceBuffer(i);
                }
            }
        }

        // Calculate throughput every ~1 second (200 loops at 5ms each)
        if (loopCount % 200 == 0)
        {
            double currentTime = juce::Time::getMillisecondCounterHiRes();
            double elapsedMs = currentTime - lastThroughputTime;

            if (elapsedMs > 0)
            {
                int64_t bytesInWindow = bytesReadInWindow.exchange(0, std::memory_order_relaxed);
                float mbps = static_cast<float>(static_cast<double>(bytesInWindow) / (elapsedMs * 1000.0));  // bytes/ms -> MB/s
                currentThroughputMBps.store(mbps, std::memory_order_relaxed);
            }

            lastThroughputTime = currentTime;

            streamDebugLog("DiskStreamer heartbeat: loop=" + juce::String(loopCount)
                          + " activeVoices=" + juce::String(activeVoices)
                          + " throughput=" + juce::String(currentThroughputMBps.load(), 2) + " MB/s");
        }
        else if (activeVoices != lastActiveVoices)
        {
            streamDebugLog("DiskStreamer: activeVoices changed " + juce::String(lastActiveVoices)
                          + " -> " + juce::String(activeVoices));
            lastActiveVoices = activeVoices;
        }

        // Wait before polling again (but can be woken early)
        wait(StreamingConstants::diskThreadPollMs);
    }

    streamDebugLog(">>> DiskStreamer thread STOPPED");
}

void DiskStreamer::fillVoiceBuffer(int voiceIndex)
{
    StreamingVoice* voice = voices[static_cast<size_t>(voiceIndex)].load(std::memory_order_acquire);
    if (voice == nullptr)
        return;

    const PreloadedSample* sample = voice->getCurrentSample();
    if (sample == nullptr || !sample->isValid())
        return;

    streamDebugLog("fillVoiceBuffer[" + juce::String(voiceIndex) + "] ENTER - sample=" + sample->name);

    // Check if we need to open or reopen the file reader
    auto& reader = readers[static_cast<size_t>(voiceIndex)];
    if (reader == nullptr || readerFilePaths[static_cast<size_t>(voiceIndex)] != sample->filePath)
    {
        reader = openReader(sample->filePath);
        readerFilePaths[static_cast<size_t>(voiceIndex)] = sample->filePath;

        if (reader == nullptr)
        {
            voice->setReadError(true);
            voice->clearNeedsData();
            return;
        }
    }

    // Get current file position and available space
    int64_t filePos = voice->getFileReadPosition();
    int64_t totalFrames = static_cast<int64_t>(reader->lengthInSamples);

    // Check for end of file
    if (filePos >= totalFrames)
    {
        voice->setEndOfFile(true);
        voice->clearNeedsData();
        return;
    }

    // Calculate how much we can read
    int space = voice->spaceAvailable();
    if (space < StreamingConstants::diskReadFrames)
    {
        // Ring buffer is nearly full - clear needsData and wait
        voice->clearNeedsData();
        return;
    }

    // Fill the buffer in chunks
    int totalFramesFilled = 0;
    while (space >= StreamingConstants::diskReadFrames && filePos < totalFrames && !threadShouldExit())
    {
        int framesToRead = static_cast<int>(std::min(static_cast<int64_t>(StreamingConstants::diskReadFrames),
                                                      totalFrames - filePos));
        framesToRead = std::min(framesToRead, space);

        if (framesToRead <= 0)
            break;

        // Clear temp buffer
        tempReadBuffer.clear();

        // Read from disk
        bool success = reader->read(&tempReadBuffer, 0, framesToRead,
                                    filePos, true, true);

        if (!success)
        {
            voice->setReadError(true);
            break;
        }

        // Track bytes read for throughput calculation
        int64_t bytesRead = static_cast<int64_t>(framesToRead) * static_cast<int64_t>(sample->numChannels) * static_cast<int64_t>(sizeof(float));
        bytesReadInWindow.fetch_add(bytesRead, std::memory_order_relaxed);
        totalBytesRead.fetch_add(bytesRead, std::memory_order_relaxed);

        // Copy to voice's ring buffer
        int writePos = voice->getWritePosition();
        int numChannels = std::min(tempReadBuffer.getNumChannels(),
                                    static_cast<int>(sample->numChannels));

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* ringBuffer = voice->getWritePointer(ch);
            const float* sourceData = tempReadBuffer.getReadPointer(ch);

            for (int frame = 0; frame < framesToRead; ++frame)
            {
                int ringPos = (writePos + frame) % StreamingConstants::ringBufferFrames;
                ringBuffer[ringPos] = sourceData[frame];
            }
        }

        // Handle mono source -> stereo ring buffer
        if (numChannels == 1 && sample->numChannels < 2)
        {
            float* ringBuffer = voice->getWritePointer(1);
            const float* sourceData = tempReadBuffer.getReadPointer(0);

            for (int frame = 0; frame < framesToRead; ++frame)
            {
                int ringPos = (writePos + frame) % StreamingConstants::ringBufferFrames;
                ringBuffer[ringPos] = sourceData[frame];
            }
        }

        // Update positions
        voice->advanceWritePosition(framesToRead);
        filePos += framesToRead;
        voice->setFileReadPosition(filePos);
        totalFramesFilled += framesToRead;

        space = voice->spaceAvailable();
    }

    // Check if we reached end of file
    if (filePos >= totalFrames)
    {
        voice->setEndOfFile(true);
    }

    streamDebugLog("fillVoiceBuffer[" + juce::String(voiceIndex) + "] EXIT - filled "
                  + juce::String(totalFramesFilled) + " frames, filePos="
                  + juce::String(filePos) + "/" + juce::String(totalFrames)
                  + " EOF=" + juce::String(voice->hasReachedEndOfFile() ? "yes" : "no"));

    // Clear the needs data flag
    voice->clearNeedsData();
}

std::unique_ptr<juce::AudioFormatReader> DiskStreamer::openReader(const juce::String& filePath)
{
    if (formatManager == nullptr)
        return nullptr;

    juce::File file(filePath);
    if (!file.existsAsFile())
        return nullptr;

    return std::unique_ptr<juce::AudioFormatReader>(formatManager->createReaderFor(file));
}

void DiskStreamer::closeReader(int voiceIndex)
{
    if (voiceIndex >= 0 && voiceIndex < StreamingConstants::maxStreamingVoices)
    {
        readers[static_cast<size_t>(voiceIndex)].reset();
        readerFilePaths[static_cast<size_t>(voiceIndex)].clear();
    }
}
