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

    static constexpr int startNote = 21;  // A0
    static constexpr int endNote = 109;   // C8+1 (exclusive, so 88 notes)
    static constexpr int numNotes = 88;
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

class MidiKeyboardEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit MidiKeyboardEditor(MidiKeyboardProcessor&);
    ~MidiKeyboardEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void loadSamplesClicked();

    MidiKeyboardProcessor& processorRef;
    NoteGridDisplay noteGrid;
    KeyboardDisplay keyboard;

    juce::TextButton loadButton{"Load Samples..."};
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    // Instrument info
    juce::Label fileSizeLabel;
    juce::Label preloadMemLabel;
    juce::Label voiceActivityLabel;
    juce::Label throughputLabel;

    // Preload size control
    juce::Slider preloadSlider;
    juce::Label preloadLabel{"", "Preload"};
    void preloadSliderChanged();

    // ADSR controls
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label attackLabel{"", "A"}, decayLabel{"", "D"}, sustainLabel{"", "S"}, releaseLabel{"", "R"};

    void updateADSR();

    // Transpose control
    juce::Slider transposeSlider;
    juce::Label transposeLabel{"", "Transpose"};
    void updateTranspose();

    // Sample offset control (borrow samples, pitch-correct back)
    juce::Slider sampleOffsetSlider;
    juce::Label sampleOffsetLabel{"", "Sample Ofs"};
    void updateSampleOffset();

    // Async loading state
    juce::String pendingLoadFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardEditor)
};
