#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <vector>
#include <array>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include "DiskStreaming.h"
#include "StreamingVoice.h"
#include "DiskStreamer.h"

struct ADSRParams
{
    float attack = 0.01f;   // seconds
    float decay = 0.1f;     // seconds
    float sustain = 0.7f;   // level 0-1
    float release = 0.3f;   // seconds
};

struct VelocityLayer
{
    int velocityValue;      // The actual velocity value from the file name
    int velocityRangeStart; // Computed: lowest velocity that triggers this layer
    int velocityRangeEnd;   // Computed: highest velocity that triggers this layer
};

struct NoteMapping
{
    int midiNote;
    std::vector<VelocityLayer> velocityLayers; // Sorted by velocity ascending
    int fallbackNote = -1; // If this note has no samples, use this note instead
};

enum class LoadingState { Idle, Loading, Loaded };

class SamplerEngine
{
public:
    SamplerEngine();
    ~SamplerEngine();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void loadSamplesFromFolder(const juce::File& folder);
    void noteOn(int midiNote, int velocity, int roundRobin);
    void noteOff(int midiNote);
    void processBlock(juce::AudioBuffer<float>& buffer);

    bool isLoaded() const;
    bool isLoading() const { return loadingState == LoadingState::Loading; }
    LoadingState getLoadingState() const { return loadingState; }
    juce::String getLoadedFolderPath() const { return loadedFolderPath; }
    int64_t getTotalInstrumentFileSize() const { return totalInstrumentFileSize.load(); }
    int64_t getPreloadMemoryBytes() const { return preloadMemoryBytes.load(); }

    // ADSR controls
    void setADSR(float attack, float decay, float sustain, float release);
    ADSRParams getADSR() const { return adsrParams; }

    // Preload size control (in KB, range 32-1024)
    int getPreloadSizeKB() const { return preloadSizeKB; }
    void setPreloadSizeKB(int sizeKB) { preloadSizeKB = juce::jlimit(32, 1024, sizeKB); }

    // Streaming activity info (for UI)
    int getActiveVoiceCount() const;
    int getStreamingVoiceCount() const;  // Voices actively reading from disk
    float getDiskThroughputMBps() const; // Current disk throughput in MB/s
    int getUnderrunCount() const;        // Total buffer underruns
    void resetUnderrunCount();           // Reset underrun counter

    // Query sample configuration for UI
    bool isNoteAvailable(int midiNote) const;  // Has samples or valid fallback
    bool noteHasOwnSamples(int midiNote) const;  // Has its own samples (not fallback)
    std::vector<int> getVelocityLayers(int midiNote) const;  // Get velocity values for a note
    int getLowestAvailableNote() const;
    int getHighestAvailableNote() const;
    int getMaxVelocityLayers(int startNote, int endNote) const;  // Max layers in range
    int getVelocityLayerIndex(int midiNote, int velocity) const;  // Index of layer for velocity (0-based)

private:
    // Parse note name to MIDI note number (e.g., "C4" -> 60, "G#6" -> 104)
    int parseNoteName(const juce::String& noteName) const;

    // Parse file name: returns true if valid, fills out note, velocity, roundRobin
    bool parseFileName(const juce::String& fileName, int& note, int& velocity, int& roundRobin) const;

    std::map<int, NoteMapping> noteMappings; // Key: MIDI note number

    ADSRParams adsrParams;

    double currentSampleRate = 44100.0;
    juce::String loadedFolderPath;
    std::atomic<int64_t> totalInstrumentFileSize{0};  // Total file size in bytes
    std::atomic<int64_t> preloadMemoryBytes{0};       // RAM used by preload buffers

    // Async loading
    std::atomic<LoadingState> loadingState{LoadingState::Idle};
    std::unique_ptr<std::thread> loadingThread;
    mutable std::recursive_mutex mappingsMutex;  // mutable + recursive for nested const method calls

    // Preload size
    int preloadSizeKB = 64;  // Default 64KB, configurable 32-1024KB

    // Streaming voices
    std::array<StreamingVoice, StreamingConstants::maxStreamingVoices> streamingVoices;

    // Background disk streaming thread
    std::unique_ptr<DiskStreamer> diskStreamer;

    // Preloaded samples for streaming
    struct StreamingSample
    {
        PreloadedSample preload;
        int midiNote = 0;
        int velocity = 0;
        int roundRobin = 0;
    };
    std::vector<StreamingSample> streamingSamples;

    // Format manager for streaming
    juce::AudioFormatManager formatManager;

    // Internal methods
    void loadSamplesInBackground(const juce::String& folderPath);
    const StreamingSample* findStreamingSample(int midiNote, int velocity, int roundRobin) const;
};
