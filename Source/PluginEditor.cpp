#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// VelocityDisplay
//==============================================================================

VelocityDisplay::VelocityDisplay(MidiKeyboardProcessor& p)
    : processor(p)
{
    startTimerHz(60);
}

void VelocityDisplay::timerCallback()
{
    repaint();
}

void VelocityDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float rectHeight = bounds.getHeight() / 3.0f;
    const float gap = 2.0f;

    int activeTier = processor.getActiveVelocityTier();

    // Draw 3 rectangles from top to bottom: high (3), mid (2), low (1)
    for (int i = 0; i < 3; ++i)
    {
        int tier = 3 - i;  // top = tier 3, middle = tier 2, bottom = tier 1
        float y = bounds.getY() + i * rectHeight;
        juce::Rectangle<float> rect(bounds.getX(), y + gap / 2, bounds.getWidth(), rectHeight - gap);

        if (activeTier == tier)
            g.setColour(juce::Colour(0xff4a9eff));  // Blue when active
        else
            g.setColour(juce::Colour(0xff3d3d3d));  // Dark gray when inactive

        g.fillRect(rect);
        g.setColour(juce::Colours::black);
        g.drawRect(rect, 1.0f);
    }
}

//==============================================================================
// KeyboardDisplay
//==============================================================================

KeyboardDisplay::KeyboardDisplay(MidiKeyboardProcessor& p)
    : processor(p)
{
    startTimerHz(60);
}

void KeyboardDisplay::timerCallback()
{
    repaint();
}

void KeyboardDisplay::drawOctave(juce::Graphics& g, juce::Rectangle<float> bounds, int startNote)
{
    const int numWhiteKeys = 7;
    const float whiteKeyWidth = bounds.getWidth() / static_cast<float>(numWhiteKeys);
    const float whiteKeyHeight = bounds.getHeight();
    const float blackKeyWidth = whiteKeyWidth * 0.65f;
    const float blackKeyHeight = whiteKeyHeight * 0.6f;

    // White key note offsets from C: C=0, D=2, E=4, F=5, G=7, A=9, B=11
    const int whiteKeyOffsets[] = {0, 2, 4, 5, 7, 9, 11};

    // Draw white keys
    for (int i = 0; i < numWhiteKeys; ++i)
    {
        float x = bounds.getX() + i * whiteKeyWidth;
        juce::Rectangle<float> keyRect(x, bounds.getY(), whiteKeyWidth - 1, whiteKeyHeight);

        int midiNote = startNote + whiteKeyOffsets[i];
        bool isPressed = processor.isNoteOn(midiNote);

        if (isPressed)
            g.setColour(juce::Colour(0xff4a9eff));
        else
            g.setColour(juce::Colours::white);

        g.fillRect(keyRect);
        g.setColour(juce::Colours::black);
        g.drawRect(keyRect, 1.0f);
    }

    // Black key positions and offsets
    const int blackKeyWhiteIndex[] = {0, 1, 3, 4, 5};
    const int blackKeyOffsets[] = {1, 3, 6, 8, 10};

    // Draw black keys
    for (int i = 0; i < 5; ++i)
    {
        float x = bounds.getX() + (blackKeyWhiteIndex[i] + 1) * whiteKeyWidth - blackKeyWidth / 2;
        juce::Rectangle<float> keyRect(x, bounds.getY(), blackKeyWidth, blackKeyHeight);

        int midiNote = startNote + blackKeyOffsets[i];
        bool isPressed = processor.isNoteOn(midiNote);

        if (isPressed)
            g.setColour(juce::Colour(0xff4a9eff));
        else
            g.setColour(juce::Colours::black);

        g.fillRect(keyRect);

        if (isPressed)
        {
            g.setColour(juce::Colours::white);
            g.drawRect(keyRect, 1.0f);
        }
    }
}

void KeyboardDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 3 octaves side by side: C3, C4, C5
    const int numOctaves = 3;
    const float octaveWidth = bounds.getWidth() / static_cast<float>(numOctaves);

    // C3 = 48, C4 = 60, C5 = 72
    const int startNotes[] = {48, 60, 72};

    for (int i = 0; i < numOctaves; ++i)
    {
        juce::Rectangle<float> octaveBounds(
            bounds.getX() + i * octaveWidth,
            bounds.getY(),
            octaveWidth,
            bounds.getHeight()
        );
        drawOctave(g, octaveBounds, startNotes[i]);
    }
}

//==============================================================================
// MidiKeyboardEditor
//==============================================================================

MidiKeyboardEditor::MidiKeyboardEditor(MidiKeyboardProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p), velocityDisplay(p), keyboard(p)
{
    addAndMakeVisible(velocityDisplay);
    addAndMakeVisible(keyboard);
    setSize(600, 200);
}

void MidiKeyboardEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2d2d2d));
}

void MidiKeyboardEditor::resized()
{
    auto bounds = getLocalBounds().reduced(5);

    // Velocity display takes top portion, keyboard takes bottom
    const int velocityHeight = 75;  // 3 rectangles ~25px each

    velocityDisplay.setBounds(bounds.removeFromTop(velocityHeight));
    bounds.removeFromTop(5);  // gap between velocity and keyboard
    keyboard.setBounds(bounds);
}
