#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

/**
 * DFD (Direct From Disk) Streaming Core Types
 *
 * This header defines the fundamental data structures for disk streaming:
 * - PreloadedSample: Sample with only initial data loaded, metadata for streaming
 * - StreamRequest: Communication between audio thread and disk thread
 */

/**
 * PreloadedSample represents a sample where only the first portion is loaded into RAM.
 * The rest is streamed from disk on demand.
 */
struct PreloadedSample
{
    juce::AudioBuffer<float> preloadBuffer;  // First 64KB only
    juce::String filePath;                    // Full path for streaming
    int64_t totalSampleFrames = 0;            // Total frames in the file
    double sampleRate = 44100.0;
    int numChannels = 2;

    // Sample zone mapping info
    int rootNote = 60;
    int lowNote = 0;
    int highNote = 127;
    int lowVelocity = 1;
    int highVelocity = 127;
    juce::String name;

    // Preload configuration
    static constexpr int preloadSizeBytes = 65536;  // 64KB preload
    int preloadSizeFrames = 0;                       // Calculated based on channels/bit depth

    bool isValid() const { return totalSampleFrames > 0 && filePath.isNotEmpty(); }

    /** Returns true if this sample is large enough to require streaming */
    bool needsStreaming() const { return totalSampleFrames > preloadSizeFrames; }

    /** Check if a MIDI note falls within this sample's range */
    bool containsNote(int midiNote) const
    {
        return midiNote >= lowNote && midiNote <= highNote;
    }

    /** Check if velocity falls within this sample's range */
    bool containsVelocity(int velocity) const
    {
        return velocity >= lowVelocity && velocity <= highVelocity;
    }

    /** Check if both note and velocity match this sample */
    bool matches(int midiNote, int velocity) const
    {
        return containsNote(midiNote) && containsVelocity(velocity);
    }
};

/**
 * StreamRequest is used to communicate between the audio thread and disk thread.
 * The audio thread sets these atomically, and the disk thread polls them.
 */
struct StreamRequest
{
    std::atomic<bool> active{false};           // Is this voice currently streaming?
    std::atomic<int64_t> filePosition{0};      // Current position in source file (frames)
    std::atomic<bool> needsData{false};        // Signal from audio thread to disk thread
    std::atomic<bool> endOfFile{false};        // Disk thread signals EOF reached
    std::atomic<bool> readError{false};        // Disk thread signals read error

    void reset()
    {
        active.store(false, std::memory_order_release);
        filePosition.store(0, std::memory_order_release);
        needsData.store(false, std::memory_order_release);
        endOfFile.store(false, std::memory_order_release);
        readError.store(false, std::memory_order_release);
    }
};

/**
 * StreamingConstants provides shared configuration values.
 */
namespace StreamingConstants
{
    // Ring buffer size in frames (~743ms at 44.1kHz)
    constexpr int ringBufferFrames = 32768;

    // Request more data when available falls below this threshold
    constexpr int lowWatermarkFrames = 8192;  // ~185ms at 44.1kHz

    // Batch read size for disk operations
    constexpr int diskReadFrames = 4096;  // ~93ms at 44.1kHz

    // Maximum number of streaming voices
    constexpr int maxStreamingVoices = 180;

    // Disk thread polling interval in milliseconds
    constexpr int diskThreadPollMs = 5;

    // Fade out duration in samples for underrun protection
    constexpr int underrunFadeOutSamples = 64;
}
