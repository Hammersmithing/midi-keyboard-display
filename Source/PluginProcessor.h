#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
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
    juce::String getLoadedFolderPath() const { return samplerEngine.getLoadedFolderPath(); }

    // ADSR controls
    void setADSR(float attack, float decay, float sustain, float release);
    ADSRParams getADSR() const { return samplerEngine.getADSR(); }

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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Check if a note is currently pressed
    bool isNoteOn(int midiNote) const { return noteVelocities[midiNote] > 0; }

    // Get velocity of a note (0 if not pressed)
    int getNoteVelocity(int midiNote) const { return noteVelocities[static_cast<size_t>(midiNote)]; }

    // Get velocity tier for a specific note (0=not pressed, 1=low, 2=mid, 3=high)
    int getNoteVelocityTier(int midiNote) const
    {
        int v = noteVelocities[static_cast<size_t>(midiNote)];
        if (v == 0) return 0;
        if (v <= 42) return 1;
        if (v <= 84) return 2;
        return 3;
    }

    // Get round-robin position for a specific note (0=not pressed, 1-3)
    int getNoteRoundRobin(int midiNote) const { return noteRoundRobin[static_cast<size_t>(midiNote)]; }

    // Check if a specific note has activated a velocity tier (for sustained tier persistence)
    bool isNoteTierActivated(int midiNote, int tier) const
    {
        auto idx = static_cast<size_t>(midiNote);
        return noteTiersActivated[idx][static_cast<size_t>(tier)];
    }

    // Check if a specific note has activated an RR position (for sustained RR persistence)
    bool isNoteRRActivated(int midiNote, int rrPosition) const
    {
        auto idx = static_cast<size_t>(midiNote);
        return noteRRActivated[idx][static_cast<size_t>(rrPosition)];
    }

private:
    std::array<int, 128> noteVelocities{};
    std::array<int, 128> noteRoundRobin{};  // Which RR position each note triggered (0=none, 1-3)
    std::array<bool, 128> noteSustained{};  // Notes held by sustain pedal
    std::array<std::array<bool, 4>, 128> noteTiersActivated{};  // Per-note: which tiers activated (index 1-3)
    std::array<std::array<bool, 4>, 128> noteRRActivated{};     // Per-note: which RR positions activated (index 1-3)
    int currentRoundRobin = 1;  // Next RR position to assign (cycles 1->2->3->1)
    bool sustainPedalDown = false;

    SamplerEngine samplerEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardProcessor)
};
