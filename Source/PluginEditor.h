#pragma once

#include "PluginProcessor.h"

class NoteGridDisplay : public juce::Component, public juce::Timer
{
public:
    NoteGridDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    MidiKeyboardProcessor& processor;

    static constexpr int startNote = 48;  // C3
    static constexpr int endNote = 84;    // B5 (exclusive, so 36 notes)
    static constexpr int numNotes = 36;
};

class KeyboardDisplay : public juce::Component, public juce::Timer
{
public:
    KeyboardDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    MidiKeyboardProcessor& processor;

    void drawOctave(juce::Graphics& g, juce::Rectangle<float> bounds, int startNote);

    bool isBlackKey(int noteInOctave) const
    {
        return noteInOctave == 1 || noteInOctave == 3 ||
               noteInOctave == 6 || noteInOctave == 8 || noteInOctave == 10;
    }
};

class MidiKeyboardEditor : public juce::AudioProcessorEditor
{
public:
    explicit MidiKeyboardEditor(MidiKeyboardProcessor&);
    ~MidiKeyboardEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void loadSamplesClicked();

    MidiKeyboardProcessor& processorRef;
    NoteGridDisplay noteGrid;
    KeyboardDisplay keyboard;

    juce::TextButton loadButton{"Load Samples..."};
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // ADSR controls
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label attackLabel{"", "A"}, decayLabel{"", "D"}, sustainLabel{"", "S"}, releaseLabel{"", "R"};

    void updateADSR();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardEditor)
};
