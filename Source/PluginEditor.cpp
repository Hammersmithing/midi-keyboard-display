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
    const float boxGap = 1.0f;

    // Get the maximum number of velocity layers across all displayed notes
    int maxLayers = processor.getMaxVelocityLayers(startNote, endNote);
    if (maxLayers == 0)
        maxLayers = 1;  // Avoid division by zero, show at least one row

    const float layerHeight = bounds.getHeight() / static_cast<float>(maxLayers);

    for (int noteOffset = 0; noteOffset < numNotes; ++noteOffset)
    {
        int midiNote = startNote + noteOffset;
        float noteX = bounds.getX() + noteOffset * noteWidth;

        // Get velocity layers for this note
        auto velocityLayers = processor.getVelocityLayers(midiNote);
        int numLayers = static_cast<int>(velocityLayers.size());

        // Get current state for this note
        int currentLayerIdx = processor.getNoteVelocityLayerIndex(midiNote);
        int currentRR = processor.getNoteRoundRobin(midiNote);
        bool noteAvailable = processor.isNoteAvailable(midiNote);

        // Draw velocity layer rows (top = highest velocity, bottom = lowest)
        for (int layerIdx = 0; layerIdx < maxLayers; ++layerIdx)
        {
            // Reverse index so highest velocity is on top
            int actualLayerIdx = numLayers - 1 - layerIdx;
            float layerY = bounds.getY() + layerIdx * layerHeight;

            // Check if this layer exists for this note
            bool layerExists = (actualLayerIdx >= 0 && actualLayerIdx < numLayers);

            // Check if this layer is active for this note
            bool layerActive = layerExists &&
                ((currentLayerIdx == actualLayerIdx) || processor.isNoteLayerActivated(midiNote, actualLayerIdx));

            // Draw the 3 RR boxes within this layer cell
            float boxWidth = (noteWidth - 4 * boxGap) / 3.0f;
            float boxHeight = layerHeight - 2 * boxGap;

            for (int rr = 1; rr <= 3; ++rr)
            {
                float boxX = noteX + boxGap + (rr - 1) * (boxWidth + boxGap);
                float boxY = layerY + boxGap;
                juce::Rectangle<float> box(boxX, boxY, boxWidth, boxHeight);

                // Check if this RR is active for this note in this layer
                bool rrActive = false;
                if (layerActive)
                {
                    rrActive = (currentRR == rr && currentLayerIdx == actualLayerIdx) ||
                               (processor.isNoteLayerActivated(midiNote, actualLayerIdx) &&
                                processor.isNoteRRActivated(midiNote, rr));
                }

                if (!noteAvailable || !layerExists)
                    g.setColour(juce::Colour(0xff252525));  // Very dark gray for unavailable
                else if (rrActive)
                    g.setColour(juce::Colour(0xff4a9eff));  // Blue when active
                else if (layerActive)
                    g.setColour(juce::Colour(0xff2a5a8f));  // Dimmer blue for active layer
                else
                    g.setColour(juce::Colour(0xff3d3d3d));  // Dark gray

                g.fillRect(box);
                g.setColour(juce::Colour(0xff222222));
                g.drawRect(box, 0.5f);

                // Draw RR number (only if layer exists for this note)
                if (layerExists)
                {
                    g.setColour(rrActive ? juce::Colours::white : juce::Colour(0xff666666));
                    g.setFont(boxHeight * 0.4f);
                    g.drawText(juce::String(rr), box, juce::Justification::centred);
                }
            }
        }

        // Draw vertical separator between notes
        g.setColour(juce::Colour(0xff222222));
        g.drawLine(noteX + noteWidth, bounds.getY(), noteX + noteWidth, bounds.getBottom(), 0.5f);
    }

    // Draw horizontal separators between layers
    for (int i = 1; i < maxLayers; ++i)
    {
        float y = bounds.getY() + i * layerHeight;
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
    // This function is no longer used - kept for compatibility
    juce::ignoreUnused(g, bounds, startNote);
}

void KeyboardDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Reserve space for labels at the bottom
    const float labelHeight = 15.0f;
    auto keyboardBounds = bounds.withTrimmedBottom(labelHeight);
    auto labelBounds = bounds.removeFromBottom(labelHeight);

    // 88 keys: A0 (21) to C8 (108)
    // White keys: A0, B0, then C1-B7 (7 octaves), then C8 = 2 + 49 + 1 = 52 white keys
    const int totalWhiteKeys = 52;
    const float whiteKeyWidth = keyboardBounds.getWidth() / static_cast<float>(totalWhiteKeys);
    const float whiteKeyHeight = keyboardBounds.getHeight();
    const float blackKeyWidth = whiteKeyWidth * 0.65f;
    const float blackKeyHeight = whiteKeyHeight * 0.6f;

    // Build a map of MIDI note to white key index (for white keys only)
    // and track which white key index each note falls after (for black keys)

    // White keys pattern in an octave: C, D, E, F, G, A, B (notes 0, 2, 4, 5, 7, 9, 11)
    auto isWhiteKey = [](int midiNote) {
        int noteInOctave = midiNote % 12;
        return noteInOctave == 0 || noteInOctave == 2 || noteInOctave == 4 ||
               noteInOctave == 5 || noteInOctave == 7 || noteInOctave == 9 || noteInOctave == 11;
    };

    // Calculate white key index for a given MIDI note
    auto getWhiteKeyIndex = [](int midiNote) -> int {
        // A0 = 21 is white key 0, B0 = 23 is white key 1
        // C1 = 24 is white key 2, etc.
        int whiteIndex = 0;
        for (int n = 21; n < midiNote; ++n) {
            int noteInOctave = n % 12;
            if (noteInOctave == 0 || noteInOctave == 2 || noteInOctave == 4 ||
                noteInOctave == 5 || noteInOctave == 7 || noteInOctave == 9 || noteInOctave == 11)
                ++whiteIndex;
        }
        return whiteIndex;
    };

    // Draw white keys first
    int whiteKeyIdx = 0;
    for (int midiNote = 21; midiNote <= 108; ++midiNote)
    {
        if (isWhiteKey(midiNote))
        {
            float x = keyboardBounds.getX() + whiteKeyIdx * whiteKeyWidth;
            juce::Rectangle<float> keyRect(x, keyboardBounds.getY(), whiteKeyWidth - 1, whiteKeyHeight);

            bool isPressed = processor.isNoteOn(midiNote);
            bool isAvailable = processor.isNoteAvailable(midiNote);
            bool hasOwnSamples = processor.noteHasOwnSamples(midiNote);

            if (isPressed)
                g.setColour(juce::Colour(0xff4a9eff));  // Blue when pressed
            else if (!isAvailable)
                g.setColour(juce::Colour(0xff555555));  // Dark grey - unavailable
            else if (!hasOwnSamples)
                g.setColour(juce::Colour(0xffcccccc));  // Light grey - uses fallback
            else
                g.setColour(juce::Colours::white);      // White - has own samples

            g.fillRect(keyRect);
            g.setColour(juce::Colours::black);
            g.drawRect(keyRect, 1.0f);

            ++whiteKeyIdx;
        }
    }

    // Draw black keys on top
    for (int midiNote = 21; midiNote <= 108; ++midiNote)
    {
        if (!isWhiteKey(midiNote))
        {
            // Find position: black key sits between white keys
            int whiteKeyBefore = getWhiteKeyIndex(midiNote);
            float x = keyboardBounds.getX() + whiteKeyBefore * whiteKeyWidth + whiteKeyWidth - blackKeyWidth / 2;
            juce::Rectangle<float> keyRect(x, keyboardBounds.getY(), blackKeyWidth, blackKeyHeight);

            bool isPressed = processor.isNoteOn(midiNote);
            bool isAvailable = processor.isNoteAvailable(midiNote);
            bool hasOwnSamples = processor.noteHasOwnSamples(midiNote);

            if (isPressed)
                g.setColour(juce::Colour(0xff4a9eff));  // Blue when pressed
            else if (!isAvailable)
                g.setColour(juce::Colour(0xff333333));  // Very dark grey - unavailable
            else if (!hasOwnSamples)
                g.setColour(juce::Colour(0xff444444));  // Dark grey - uses fallback
            else
                g.setColour(juce::Colours::black);      // Black - has own samples

            g.fillRect(keyRect);

            if (isPressed)
            {
                g.setColour(juce::Colours::white);
                g.drawRect(keyRect, 1.0f);
            }
        }
    }

    // Draw C labels (C1 through C8)
    g.setColour(juce::Colours::lightgrey);
    g.setFont(11.0f);

    for (int octave = 1; octave <= 8; ++octave)
    {
        int cNote = 12 + octave * 12;  // C1=24, C2=36, ..., C8=108
        if (cNote >= 21 && cNote <= 108)
        {
            int whiteIdx = getWhiteKeyIndex(cNote);
            float x = keyboardBounds.getX() + whiteIdx * whiteKeyWidth;
            juce::Rectangle<float> labelRect(x, labelBounds.getY(), whiteKeyWidth, labelHeight);
            g.drawText("C" + juce::String(octave), labelRect, juce::Justification::centred);
        }
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

    // Setup streaming toggle
    addAndMakeVisible(streamingToggle);
    streamingToggle.setToggleState(processorRef.isStreamingEnabled(), juce::dontSendNotification);
    streamingToggle.onClick = [this] { streamingToggleChanged(); };

    addAndMakeVisible(streamingLabel);
    streamingLabel.setFont(juce::FontOptions(12.0f));
    streamingLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    streamingLabel.setText(processorRef.isStreamingEnabled() ? "Mode: STREAMING" : "Mode: RAM", juce::dontSendNotification);

    // File size label
    addAndMakeVisible(fileSizeLabel);
    fileSizeLabel.setFont(juce::FontOptions(12.0f));
    fileSizeLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    fileSizeLabel.setJustificationType(juce::Justification::centredRight);

    // Preload memory label
    addAndMakeVisible(preloadMemLabel);
    preloadMemLabel.setFont(juce::FontOptions(12.0f));
    preloadMemLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    preloadMemLabel.setJustificationType(juce::Justification::centredRight);

    // Voice activity label
    addAndMakeVisible(voiceActivityLabel);
    voiceActivityLabel.setFont(juce::FontOptions(12.0f));
    voiceActivityLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    voiceActivityLabel.setJustificationType(juce::Justification::centredRight);

    // Throughput label (for streaming mode)
    addAndMakeVisible(throughputLabel);
    throughputLabel.setFont(juce::FontOptions(12.0f));
    throughputLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    throughputLabel.setJustificationType(juce::Justification::centredRight);

    // Preload size slider (only shown for streaming mode)
    preloadSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    preloadSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 15);
    preloadSlider.setRange(32, 1024, 1);  // 32KB to 1024KB (1MB)
    preloadSlider.setValue(processorRef.getPreloadSizeKB());
    preloadSlider.setTextValueSuffix(" KB");
    preloadSlider.onValueChange = [this] { preloadSliderChanged(); };
    addAndMakeVisible(preloadSlider);

    preloadLabel.setJustificationType(juce::Justification::centred);
    preloadLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(preloadLabel);

    // Start timer for async loading status updates
    startTimerHz(10);

    setSize(1400, 650);  // Wider for 88 keys, taller for ADSR controls
}

void MidiKeyboardEditor::timerCallback()
{
    // Check if we're waiting for loading to complete
    if (pendingLoadFolder.isNotEmpty())
    {
        if (processorRef.areSamplesLoaded())
        {
            juce::String modeStr = pendingLoadStreaming ? " [STREAMING]" : " [RAM]";
            statusLabel.setText("Loaded: " + pendingLoadFolder + modeStr, juce::dontSendNotification);
            pendingLoadFolder.clear();

            // Update file size display
            int64_t totalBytes = processorRef.getTotalInstrumentFileSize();
            juce::String sizeStr;
            if (totalBytes >= 1024 * 1024 * 1024)
                sizeStr = juce::String(totalBytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
            else if (totalBytes >= 1024 * 1024)
                sizeStr = juce::String(totalBytes / (1024.0 * 1024.0), 1) + " MB";
            else if (totalBytes >= 1024)
                sizeStr = juce::String(totalBytes / 1024.0, 1) + " KB";
            else
                sizeStr = juce::String(totalBytes) + " B";
            fileSizeLabel.setText("Size: " + sizeStr, juce::dontSendNotification);

            // Update preload memory display (only for streaming mode)
            if (pendingLoadStreaming)
            {
                int64_t preloadBytes = processorRef.getPreloadMemoryBytes();
                juce::String preloadStr;
                if (preloadBytes >= 1024 * 1024 * 1024)
                    preloadStr = juce::String(preloadBytes / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
                else if (preloadBytes >= 1024 * 1024)
                    preloadStr = juce::String(preloadBytes / (1024.0 * 1024.0), 1) + " MB";
                else if (preloadBytes >= 1024)
                    preloadStr = juce::String(preloadBytes / 1024.0, 1) + " KB";
                else
                    preloadStr = juce::String(preloadBytes) + " B";
                preloadMemLabel.setText("RAM: " + preloadStr, juce::dontSendNotification);
            }
            else
            {
                preloadMemLabel.setText("", juce::dontSendNotification);
            }
        }
        else if (!processorRef.areSamplesLoading())
        {
            // Loading finished but no samples found
            statusLabel.setText("No valid samples found", juce::dontSendNotification);
            fileSizeLabel.setText("", juce::dontSendNotification);
            pendingLoadFolder.clear();
        }
        // else still loading, keep showing "Loading..."
    }

    // Always update voice activity (real-time)
    if (processorRef.areSamplesLoaded())
    {
        int activeVoices = processorRef.getActiveVoiceCount();
        if (processorRef.isStreamingEnabled())
        {
            int streamingVoices = processorRef.getStreamingVoiceCount();
            int underruns = processorRef.getUnderrunCount();
            voiceActivityLabel.setText("Voices: " + juce::String(activeVoices) + " | Disk: " + juce::String(streamingVoices), juce::dontSendNotification);

            // Show disk throughput and underrun count
            float throughput = processorRef.getDiskThroughputMBps();
            juce::String throughputText = juce::String(throughput, 1) + " MB/s";
            if (underruns > 0)
                throughputText += " (" + juce::String(underruns) + " drop)";
            throughputLabel.setText(throughputText, juce::dontSendNotification);
        }
        else
        {
            voiceActivityLabel.setText("Voices: " + juce::String(activeVoices), juce::dontSendNotification);
            throughputLabel.setText("", juce::dontSendNotification);
        }
    }
    else
    {
        voiceActivityLabel.setText("", juce::dontSendNotification);
        throughputLabel.setText("", juce::dontSendNotification);
    }
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

void MidiKeyboardEditor::streamingToggleChanged()
{
    bool streaming = streamingToggle.getToggleState();
    processorRef.setStreamingEnabled(streaming);
    streamingLabel.setText(streaming ? "Mode: STREAMING" : "Mode: RAM", juce::dontSendNotification);

    // Update status to indicate reload needed
    if (processorRef.areSamplesLoaded())
    {
        statusLabel.setText("Reload samples to apply mode change", juce::dontSendNotification);
    }
}

void MidiKeyboardEditor::preloadSliderChanged()
{
    processorRef.setPreloadSizeKB(static_cast<int>(preloadSlider.getValue()));

    // Auto-reload samples if streaming mode is enabled and samples are loaded
    if (processorRef.areSamplesLoaded() && processorRef.isStreamingEnabled())
    {
        juce::String folderPath = processorRef.getLoadedFolderPath();
        if (folderPath.isNotEmpty())
        {
            juce::File folder(folderPath);
            if (folder.isDirectory())
            {
                processorRef.loadSamplesStreamingFromFolder(folder);
                statusLabel.setText("Reloading with " + juce::String(static_cast<int>(preloadSlider.getValue())) + " KB preload...", juce::dontSendNotification);
                pendingLoadFolder = folder.getFileName();
                pendingLoadStreaming = true;
            }
        }
    }
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
            bool streaming = processorRef.isStreamingEnabled();
            juce::String modeStr = streaming ? " [STREAMING]" : " [RAM]";

            if (streaming)
                processorRef.loadSamplesStreamingFromFolder(folder);
            else
                processorRef.loadSamplesFromFolder(folder);

            // Show loading status - actual completion handled by timer in NoteGridDisplay
            statusLabel.setText("Loading: " + folder.getFileName() + modeStr + "...", juce::dontSendNotification);
            pendingLoadFolder = folder.getFileName();
            pendingLoadStreaming = streaming;
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
    const int keyboardHeight = 135;  // Increased for C labels
    const int gap = 10;

    // Top controls row
    auto controlsArea = bounds.removeFromTop(controlsHeight);
    loadButton.setBounds(controlsArea.removeFromLeft(120));
    controlsArea.removeFromLeft(10);

    // Voice activity, throughput, preload RAM, and file size on the right
    throughputLabel.setBounds(controlsArea.removeFromRight(110));
    controlsArea.removeFromRight(5);
    voiceActivityLabel.setBounds(controlsArea.removeFromRight(110));
    controlsArea.removeFromRight(5);
    preloadMemLabel.setBounds(controlsArea.removeFromRight(85));
    controlsArea.removeFromRight(5);
    fileSizeLabel.setBounds(controlsArea.removeFromRight(80));
    controlsArea.removeFromRight(10);

    // Streaming toggle
    auto streamingArea = controlsArea.removeFromRight(200);
    streamingLabel.setBounds(streamingArea.removeFromRight(110));
    streamingToggle.setBounds(streamingArea);

    controlsArea.removeFromRight(10);
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

    // Add some spacing before preload knob
    adsrArea.removeFromLeft(20);

    // Preload knob (wider to fit "Preload" label and "XXX KB" text)
    auto preloadArea = adsrArea.removeFromLeft(70);
    preloadLabel.setBounds(preloadArea.removeFromTop(labelHeight));
    preloadSlider.setBounds(preloadArea);

    bounds.removeFromTop(gap);

    // Bottom keyboard
    keyboard.setBounds(bounds.removeFromBottom(keyboardHeight));
    bounds.removeFromBottom(gap);

    // Middle grid
    noteGrid.setBounds(bounds);
}
