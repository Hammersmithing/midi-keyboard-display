#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include "DiskStreaming.h"

/**
 * StreamingVoice implements a voice that plays audio from a ring buffer
 * that is filled by a background disk thread.
 *
 * Lock-free communication pattern:
 * - Audio thread: reads from ring buffer, updates readPosition (release)
 * - Disk thread: writes to ring buffer, updates writePosition (release)
 * - Both threads: read the other's position with acquire semantics
 */
class StreamingVoice
{
public:
    StreamingVoice();
    ~StreamingVoice();

    // Voice lifecycle (called from audio thread)
    void startVoice(const PreloadedSample* sample, int midiNote, float velocity, double hostSampleRate);
    void stopVoice(bool allowTailOff);
    void reset();

    // Audio thread interface
    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples);
    bool isActive() const { return active.load(std::memory_order_acquire); }
    int getPlayingNote() const { return playingNote; }

    // Sustain pedal support
    void noteReleasedWithPedal(bool pedalDown);
    void setSustainPedal(bool isDown);
    bool isSustainedByPedal() const { return sustainedByPedal; }

    // ADSR
    void setADSRParameters(const juce::ADSR::Parameters& params);
    void prepareToPlay(double sampleRate, int samplesPerBlock);

    // Ring buffer access for disk thread (thread-safe)
    int samplesAvailable() const;
    int spaceAvailable() const;
    bool needsMoreData() const { return needsData.load(std::memory_order_acquire); }
    void clearNeedsData() { needsData.store(false, std::memory_order_release); }

    // Disk thread fills buffer here
    float* getWritePointer(int channel);
    int getWritePosition() const { return static_cast<int>(writePosition.load(std::memory_order_acquire) % StreamingConstants::ringBufferFrames); }
    void advanceWritePosition(int frames);

    // File position tracking for disk thread
    int64_t getFileReadPosition() const { return fileReadPosition.load(std::memory_order_acquire); }
    void setFileReadPosition(int64_t pos) { fileReadPosition.store(pos, std::memory_order_release); }

    // Status flags for disk thread
    void setEndOfFile(bool eof) { endOfFile.store(eof, std::memory_order_release); }
    bool hasReachedEndOfFile() const { return endOfFile.load(std::memory_order_acquire); }
    void setReadError(bool error) { readError.store(error, std::memory_order_release); }
    bool hasReadError() const { return readError.load(std::memory_order_acquire); }

    // Underrun tracking (shared across all voices)
    static int getUnderrunCount() { return underrunCount.load(std::memory_order_relaxed); }
    static void resetUnderrunCount() { underrunCount.store(0, std::memory_order_relaxed); }

    // Sample info for disk thread
    const PreloadedSample* getCurrentSample() const { return currentSample; }

private:
    // Current sample being played (set at voice start, read by disk thread)
    const PreloadedSample* currentSample = nullptr;

    // Ring buffer for streaming audio (stereo capable)
    juce::AudioBuffer<float> ringBuffer;

    // Lock-free SPSC (Single Producer Single Consumer) positions
    std::atomic<int64_t> readPosition{0};   // Audio thread owns writes
    std::atomic<int64_t> writePosition{0};  // Disk thread owns writes

    // Position in source file (for disk thread to know where to read from)
    std::atomic<int64_t> fileReadPosition{0};

    // Status flags
    std::atomic<bool> active{false};
    std::atomic<bool> needsData{false};
    std::atomic<bool> endOfFile{false};
    std::atomic<bool> readError{false};

    // Voice state
    int playingNote = -1;
    float velocity = 0.0f;
    double pitchRatio = 1.0;
    double sourceSamplePosition = 0.0;  // Fractional position for interpolation

    // Envelope
    juce::ADSR adsr;
    bool sustainedByPedal = false;

    // Underrun protection
    bool isUnderrunning = false;
    int underrunFadePosition = 0;

    // Static underrun counter (shared across all voices)
    static std::atomic<int> underrunCount;

    // Internal helpers
    void checkAndRequestData();
    float readFromRingBuffer(int channel, int ringPos);
};
