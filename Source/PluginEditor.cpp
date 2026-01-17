#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// KeyboardDisplay
//==============================================================================

KeyboardDisplay::KeyboardDisplay(MidiKeyboardProcessor& p)
    : processor(p)
{
    startTimerHz(60);  // Update display 60 times per second
}

void KeyboardDisplay::timerCallback()
{
    repaint();
}

void KeyboardDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds();

    // Calculate dimensions
    const int numWhiteKeys = 7;  // C, D, E, F, G, A, B
    const float whiteKeyWidth = bounds.getWidth() / static_cast<float>(numWhiteKeys);
    const float whiteKeyHeight = bounds.getHeight();
    const float blackKeyWidth = whiteKeyWidth * 0.6f;
    const float blackKeyHeight = whiteKeyHeight * 0.6f;

    // Base MIDI note for this octave (C)
    int baseNote = (baseOctave + 1) * 12;  // C4 = 60

    // White key positions: C=0, D=1, E=2, F=3, G=4, A=5, B=6
    // Note offsets from C: C=0, D=2, E=4, F=5, G=7, A=9, B=11
    const int whiteKeyOffsets[] = {0, 2, 4, 5, 7, 9, 11};

    // Draw white keys first
    for (int i = 0; i < numWhiteKeys; ++i)
    {
        float x = i * whiteKeyWidth;
        juce::Rectangle<float> keyRect(x, 0, whiteKeyWidth - 1, whiteKeyHeight);

        int midiNote = baseNote + whiteKeyOffsets[i];
        bool isPressed = processor.isNoteOn(midiNote);

        // Key color
        if (isPressed)
            g.setColour(juce::Colour(0xff4a9eff));  // Blue when pressed
        else
            g.setColour(juce::Colours::white);

        g.fillRect(keyRect);

        // Key border
        g.setColour(juce::Colours::black);
        g.drawRect(keyRect, 1.0f);
    }

    // Draw black keys on top
    // Black keys: C#=1, D#=3, F#=6, G#=8, A#=10
    // Position after white keys: C#->after C, D#->after D, F#->after F, G#->after G, A#->after A
    const int blackKeyWhiteIndex[] = {0, 1, 3, 4, 5};  // Which white key they're after
    const int blackKeyOffsets[] = {1, 3, 6, 8, 10};    // Note offset from C

    for (int i = 0; i < 5; ++i)
    {
        float x = (blackKeyWhiteIndex[i] + 1) * whiteKeyWidth - blackKeyWidth / 2;
        juce::Rectangle<float> keyRect(x, 0, blackKeyWidth, blackKeyHeight);

        int midiNote = baseNote + blackKeyOffsets[i];
        bool isPressed = processor.isNoteOn(midiNote);

        // Key color
        if (isPressed)
            g.setColour(juce::Colour(0xff4a9eff));  // Blue when pressed
        else
            g.setColour(juce::Colours::black);

        g.fillRect(keyRect);

        // Border for pressed black keys
        if (isPressed)
        {
            g.setColour(juce::Colours::white);
            g.drawRect(keyRect, 1.0f);
        }
    }
}

//==============================================================================
// MidiKeyboardEditor
//==============================================================================

MidiKeyboardEditor::MidiKeyboardEditor(MidiKeyboardProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), keyboard(p)
{
    addAndMakeVisible(keyboard);
    setSize(400, 150);
}

void MidiKeyboardEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2d2d2d));
}

void MidiKeyboardEditor::resized()
{
    keyboard.setBounds(getLocalBounds().reduced(10));
}
