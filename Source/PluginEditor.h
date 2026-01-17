#pragma once

#include "PluginProcessor.h"

class KeyboardDisplay : public juce::Component, public juce::Timer
{
public:
    KeyboardDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

    void setBaseOctave(int octave) { baseOctave = octave; repaint(); }
    int getBaseOctave() const { return baseOctave; }

private:
    MidiKeyboardProcessor& processor;
    int baseOctave = 4;  // Middle C octave

    // Key layout: C, C#, D, D#, E, F, F#, G, G#, A, A#, B
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
    MidiKeyboardProcessor& processorRef;
    KeyboardDisplay keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardEditor)
};
