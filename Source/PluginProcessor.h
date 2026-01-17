#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

class MidiKeyboardProcessor : public juce::AudioProcessor
{
public:
    MidiKeyboardProcessor();
    ~MidiKeyboardProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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
    int getNoteVelocity(int midiNote) const { return noteVelocities[midiNote]; }

    // Get the highest velocity tier currently active (0=none, 1=low 1-42, 2=mid 43-84, 3=high 85-127)
    int getActiveVelocityTier() const
    {
        int maxVel = 0;
        for (int v : noteVelocities)
            if (v > maxVel) maxVel = v;

        if (maxVel == 0) return 0;
        if (maxVel <= 42) return 1;
        if (maxVel <= 84) return 2;
        return 3;
    }

    // Check if a round-robin position (1, 2, or 3) is currently active
    bool isRoundRobinActive(int rrPosition) const
    {
        for (int rr : noteRoundRobin)
            if (rr == rrPosition) return true;
        return false;
    }

private:
    std::array<int, 128> noteVelocities{};
    std::array<int, 128> noteRoundRobin{};  // Which RR position each note triggered (0=none, 1-3)
    int currentRoundRobin = 1;  // Next RR position to assign (cycles 1->2->3->1)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardProcessor)
};
