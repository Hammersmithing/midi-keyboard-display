#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

struct Sample
{
    juce::AudioBuffer<float> buffer;
    int midiNote = 0;
    int velocity = 0;
    int roundRobin = 0;
    double sampleRate = 44100.0;
};

struct ADSRParams
{
    float attack = 0.01f;   // seconds
    float decay = 0.1f;     // seconds
    float sustain = 0.7f;   // level 0-1
    float release = 0.3f;   // seconds
};

enum class EnvelopeStage { Idle, Attack, Decay, Sustain, Release };

struct VelocityLayer
{
    int velocityValue;      // The actual velocity value from the file name
    int velocityRangeStart; // Computed: lowest velocity that triggers this layer
    int velocityRangeEnd;   // Computed: highest velocity that triggers this layer
    std::array<std::shared_ptr<Sample>, 4> roundRobinSamples; // Index 1-3 used
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

    bool isLoaded() const { return loadingState == LoadingState::Loaded && !noteMappings.empty(); }
    bool isLoading() const { return loadingState == LoadingState::Loading; }
    LoadingState getLoadingState() const { return loadingState; }
    juce::String getLoadedFolderPath() const { return loadedFolderPath; }

    // ADSR controls
    void setADSR(float attack, float decay, float sustain, float release);
    ADSRParams getADSR() const { return adsrParams; }

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

    // Build velocity ranges after all samples are loaded
    void buildVelocityRanges();

    // Build note fallbacks for missing notes
    void buildNoteFallbacks();

    // Find the sample to play for a given note/velocity/roundRobin
    // Returns the sample and the actual MIDI note of the sample (for pitch calculation)
    std::shared_ptr<Sample> findSample(int midiNote, int velocity, int roundRobin, int& actualSampleNote) const;

    std::map<int, NoteMapping> noteMappings; // Key: MIDI note number
    juce::AudioFormatManager formatManager;

    // Active voices
    struct Voice
    {
        std::shared_ptr<Sample> sample;
        double position = 0.0;      // Fractional position for pitch shifting
        double pitchRatio = 1.0;    // Playback rate (< 1.0 = lower pitch)
        int midiNote = 0;
        bool active = false;

        // Envelope state
        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envLevel = 0.0f;      // Current envelope level (0-1)
        float envIncrement = 0.0f;  // Per-sample increment for current stage
    };
    static constexpr int maxVoices = 32;
    std::array<Voice, maxVoices> voices;

    ADSRParams adsrParams;

    double currentSampleRate = 44100.0;
    juce::String loadedFolderPath;

    // Async loading
    std::atomic<LoadingState> loadingState{LoadingState::Idle};
    std::unique_ptr<std::thread> loadingThread;
    std::mutex mappingsMutex;
    void loadSamplesInBackground(const juce::String& folderPath);
};
