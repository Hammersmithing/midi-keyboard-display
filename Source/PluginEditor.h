#pragma once

#include "PluginProcessor.h"

class VelocityDisplay : public juce::Component, public juce::Timer
{
public:
    VelocityDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    MidiKeyboardProcessor& processor;
};

class RoundRobinDisplay : public juce::Component, public juce::Timer
{
public:
    RoundRobinDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    MidiKeyboardProcessor& processor;
};

class KeyboardDisplay : public juce::Component, public juce::Timer
{
public:
    KeyboardDisplay(MidiKeyboardProcessor& p);
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

private:
    MidiKeyboardProcessor& processor;

    // Draw a single octave starting at the given MIDI note
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
    MidiKeyboardProcessor& processorRef;
    VelocityDisplay velocityDisplay;
    RoundRobinDisplay roundRobinDisplay;
    KeyboardDisplay keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiKeyboardEditor)
};
