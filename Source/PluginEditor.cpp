#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// NoteGridDisplay
//==============================================================================

NoteGridDisplay::NoteGridDisplay(MidiKeyboardProcessor& p)
    : processor(p)
{
    startTimerHz(60);
}

void NoteGridDisplay::timerCallback()
{
    repaint();
}

void NoteGridDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float noteWidth = bounds.getWidth() / static_cast<float>(numNotes);
    const float tierHeight = bounds.getHeight() / 3.0f;
    const float boxGap = 1.0f;

    for (int noteOffset = 0; noteOffset < numNotes; ++noteOffset)
    {
        int midiNote = startNote + noteOffset;
        float noteX = bounds.getX() + noteOffset * noteWidth;

        // Get current state for this note
        int currentTier = processor.getNoteVelocityTier(midiNote);
        int currentRR = processor.getNoteRoundRobin(midiNote);

        // Draw 3 velocity tier rows (top = high/3, middle = mid/2, bottom = low/1)
        for (int tierIdx = 0; tierIdx < 3; ++tierIdx)
        {
            int tier = 3 - tierIdx;  // top row = tier 3, bottom row = tier 1
            float tierY = bounds.getY() + tierIdx * tierHeight;

            // Check if this tier is active for this note
            bool tierActive = (currentTier == tier) || processor.isNoteTierActivated(midiNote, tier);

            // Draw the 3 RR boxes within this tier cell
            float boxWidth = (noteWidth - 4 * boxGap) / 3.0f;
            float boxHeight = tierHeight - 2 * boxGap;

            for (int rr = 1; rr <= 3; ++rr)
            {
                float boxX = noteX + boxGap + (rr - 1) * (boxWidth + boxGap);
                float boxY = tierY + boxGap;
                juce::Rectangle<float> box(boxX, boxY, boxWidth, boxHeight);

                // Check if this RR is active for this note in this tier
                bool rrActive = false;
                if (tierActive)
                {
                    rrActive = (currentRR == rr && currentTier == tier) ||
                               (processor.isNoteTierActivated(midiNote, tier) &&
                                processor.isNoteRRActivated(midiNote, rr));
                }

                if (rrActive)
                    g.setColour(juce::Colour(0xff4a9eff));  // Blue when active
                else if (tierActive)
                    g.setColour(juce::Colour(0xff2a5a8f));  // Dimmer blue for active tier
                else
                    g.setColour(juce::Colour(0xff3d3d3d));  // Dark gray

                g.fillRect(box);
                g.setColour(juce::Colour(0xff222222));
                g.drawRect(box, 0.5f);

                // Draw RR number
                g.setColour(rrActive ? juce::Colours::white : juce::Colour(0xff666666));
                g.setFont(boxHeight * 0.4f);
                g.drawText(juce::String(rr), box, juce::Justification::centred);
            }
        }

        // Draw vertical separator between notes
        g.setColour(juce::Colour(0xff222222));
        g.drawLine(noteX + noteWidth, bounds.getY(), noteX + noteWidth, bounds.getBottom(), 0.5f);
    }

    // Draw horizontal separators between tiers
    for (int i = 1; i < 3; ++i)
    {
        float y = bounds.getY() + i * tierHeight;
        g.setColour(juce::Colour(0xff222222));
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 0.5f);
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
    : AudioProcessorEditor(&p), processorRef(p), noteGrid(p), keyboard(p)
{
    addAndMakeVisible(noteGrid);
    addAndMakeVisible(keyboard);

    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { loadSamplesClicked(); };

    addAndMakeVisible(statusLabel);
    statusLabel.setFont(juce::FontOptions(14.0f));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);

    if (processorRef.areSamplesLoaded())
        statusLabel.setText("Loaded: " + processorRef.getLoadedFolderPath(), juce::dontSendNotification);
    else
        statusLabel.setText("No samples loaded", juce::dontSendNotification);

    // Setup ADSR sliders
    auto setupSlider = [this](juce::Slider& slider, juce::Label& label, double min, double max, double value)
    {
        slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
        slider.setRange(min, max, 0.001);
        slider.setValue(value);
        slider.onValueChange = [this] { updateADSR(); };
        addAndMakeVisible(slider);

        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    };

    auto adsr = processorRef.getADSR();
    setupSlider(attackSlider, attackLabel, 0.001, 2.0, adsr.attack);
    setupSlider(decaySlider, decayLabel, 0.001, 2.0, adsr.decay);
    setupSlider(sustainSlider, sustainLabel, 0.0, 1.0, adsr.sustain);
    setupSlider(releaseSlider, releaseLabel, 0.001, 3.0, adsr.release);

    setSize(1200, 650);  // Taller for ADSR controls
}

void MidiKeyboardEditor::updateADSR()
{
    processorRef.setADSR(
        static_cast<float>(attackSlider.getValue()),
        static_cast<float>(decaySlider.getValue()),
        static_cast<float>(sustainSlider.getValue()),
        static_cast<float>(releaseSlider.getValue())
    );
}

void MidiKeyboardEditor::loadSamplesClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Sample Folder",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*");

    auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    fileChooser->launchAsync(folderChooserFlags, [this](const juce::FileChooser& fc)
    {
        auto folder = fc.getResult();
        if (folder.isDirectory())
        {
            processorRef.loadSamplesFromFolder(folder);

            if (processorRef.areSamplesLoaded())
                statusLabel.setText("Loaded: " + folder.getFileName(), juce::dontSendNotification);
            else
                statusLabel.setText("No valid samples found", juce::dontSendNotification);
        }
    });
}

void MidiKeyboardEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff2d2d2d));
}

void MidiKeyboardEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    const int controlsHeight = 30;
    const int adsrHeight = 70;
    const int keyboardHeight = 120;
    const int gap = 10;

    // Top controls row
    auto controlsArea = bounds.removeFromTop(controlsHeight);
    loadButton.setBounds(controlsArea.removeFromLeft(120));
    controlsArea.removeFromLeft(10);
    statusLabel.setBounds(controlsArea);

    bounds.removeFromTop(gap);

    // ADSR row
    auto adsrArea = bounds.removeFromTop(adsrHeight);
    const int knobWidth = 60;
    const int labelHeight = 15;

    auto layoutKnob = [&](juce::Slider& slider, juce::Label& label)
    {
        auto knobArea = adsrArea.removeFromLeft(knobWidth);
        label.setBounds(knobArea.removeFromTop(labelHeight));
        slider.setBounds(knobArea);
        adsrArea.removeFromLeft(5);
    };

    layoutKnob(attackSlider, attackLabel);
    layoutKnob(decaySlider, decayLabel);
    layoutKnob(sustainSlider, sustainLabel);
    layoutKnob(releaseSlider, releaseLabel);

    bounds.removeFromTop(gap);

    // Bottom keyboard
    keyboard.setBounds(bounds.removeFromBottom(keyboardHeight));
    bounds.removeFromBottom(gap);

    // Middle grid
    noteGrid.setBounds(bounds);
}
