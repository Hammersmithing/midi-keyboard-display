#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "SamplerEngine.h"

class MidiKeyboardProcessor : public juce::AudioProcessor
{
public:
    MidiKeyboardProcessor();
    ~MidiKeyboardProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Sample loading
    void loadSamplesFromFolder(const juce::File& folder);
    bool areSamplesLoaded() const { return samplerEngine.isLoaded(); }
    bool areSamplesLoading() const { return samplerEngine.isLoading(); }
    juce::String getLoadedFolderPath() const { return samplerEngine.getLoadedFolderPath(); }

    // ADSR controls
    void setADSR(float attack, float decay, float sustain, float release);
    ADSRParams getADSR() const { return samplerEngine.getADSR(); }

    // Sample configuration queries for UI
    bool isNoteAvailable(int midiNote) const { return samplerEngine.isNoteAvailable(midiNote); }
    bool noteHasOwnSamples(int midiNote) const { return samplerEngine.noteHasOwnSamples(midiNote); }
    std::vector<int> getVelocityLayers(int midiNote) const { return samplerEngine.getVelocityLayers(midiNote); }
    int getLowestAvailableNote() const { return samplerEngine.getLowestAvailableNote(); }
    int getHighestAvailableNote() const { return samplerEngine.getHighestAvailableNote(); }
    int getMaxVelocityLayers(int startNote, int endNote) const { return samplerEngine.getMaxVelocityLayers(startNote, endNote); }
    int getVelocityLayerIndex(int midiNote, int velocity) const { return samplerEngine.getVelocityLayerIndex(midiNote, velocity); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Check if a note is currently pressed
    bool isNoteOn(int midiNote) const { return noteVelocities[midiNote] > 0; }

    // Get velocity of a note (0 if not pressed)
    int getNoteVelocity(int midiNote) const { return noteVelocities[static_cast<size_t>(midiNote)]; }

    // Get velocity layer index for a specific note (-1=not pressed, 0+ = layer index)
    int getNoteVelocityLayerIndex(int midiNote) const
    {
        return noteVelocityLayerIdx[static_cast<size_t>(midiNote)];
    }

    // Get round-robin position for a specific note (0=not pressed, 1-3)
    int getNoteRoundRobin(int midiNote) const { return noteRoundRobin[static_cast<size_t>(midiNote)]; }

    // Check if a specific velocity layer has been activated for this note (for sustain pedal persistence)
    bool isNoteLayerActivated(int midiNote, int layerIndex) const
    {
        auto idx = static_cast<size_t>(midiNote);
        if (layerIndex < 0 || layerIndex >= maxVelocityLayers) return false;
        return noteLayersActivated[idx][static_cast<size_t>(layerIndex)];
    }

    // Check if a specific note has activated an RR position (for sustained RR persistence)
    bool isNoteRRActivated(int midiNote, int rrPosition) const
    {
        auto idx = static_cast<size_t>(midiNote);
        return noteRRActivated[idx][static_cast<size_t>(rrPosition)];
    }

private:
    static constexpr int maxVelocityLayers = 8;  // Max velocity layers we support in UI

    std::array<int, 128> noteVelocities{};
    std::array<int, 128> noteVelocityLayerIdx{};  // Which velocity layer index each note triggered (-1=none)
    std::array<int, 128> noteRoundRobin{};  // Which RR position each note triggered (0=none, 1-3)
    std::array<bool, 128> noteSustained{};  // Notes held by sustain pedal
    std::array<std::array<bool, maxVelocityLayers>, 128> noteLayersActivated{};  // Per-note: which layers activated
    std::array<std::array<bool, 4>, 128> noteRRActivated{};     // Per-note: which RR positions activated (index 1-3)
    int currentRoundRobin = 1;  // Next RR position to assign (cycles 1->2->3->1)
    bool sustainPedalDown = false;

    SamplerEngine samplerEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardProcessor)
};
