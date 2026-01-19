#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiKeyboardProcessor::MidiKeyboardProcessor()
    : AudioProcessor(BusesProperties()
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    noteVelocities.fill(0);
    noteVelocityLayerIdx.fill(-1);
    noteRoundRobin.fill(0);
    noteSustained.fill(false);
    for (auto& arr : noteLayersActivated) arr.fill(false);
    for (auto& arr : noteRRActivated) arr.fill(false);
}

void MidiKeyboardProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    noteVelocities.fill(0);
    noteVelocityLayerIdx.fill(-1);
    noteRoundRobin.fill(0);
    noteSustained.fill(false);
    for (auto& arr : noteLayersActivated) arr.fill(false);
    for (auto& arr : noteRRActivated) arr.fill(false);
    currentRoundRobin = 1;
    sustainPedalDown = false;

    samplerEngine.prepareToPlay(sampleRate, samplesPerBlock);
}

void MidiKeyboardProcessor::loadSamplesFromFolder(const juce::File& folder)
{
    samplerEngine.loadSamplesFromFolder(folder);
}

void MidiKeyboardProcessor::setADSR(float attack, float decay, float sustain, float release)
{
    samplerEngine.setADSR(attack, decay, sustain, release);
}

void MidiKeyboardProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        // Handle sustain pedal (CC64)
        if (message.isController() && message.getControllerNumber() == 64)
        {
            bool pedalNowDown = message.getControllerValue() >= 64;

            if (!pedalNowDown && sustainPedalDown)
            {
                // Pedal released - clear all sustained notes and per-note activations
                for (size_t i = 0; i < 128; ++i)
                {
                    if (noteSustained[i])
                    {
                        noteVelocities[i] = 0;
                        noteVelocityLayerIdx[i] = -1;
                        noteRoundRobin[i] = 0;
                        noteSustained[i] = false;
                        samplerEngine.noteOff(static_cast<int>(i));
                    }
                    noteLayersActivated[i].fill(false);
                    noteRRActivated[i].fill(false);
                }
            }
            sustainPedalDown = pedalNowDown;
            continue;
        }

        int originalNote = message.getNoteNumber();
        int midiNote = juce::jlimit(0, 127, originalNote + transposeAmount);
        auto noteIndex = static_cast<size_t>(midiNote);

        if (message.isNoteOn())
        {
            noteSustained[noteIndex] = false;  // Clear sustained state on new note
            int velocity = message.getVelocity();
            noteVelocities[noteIndex] = velocity;
            noteRoundRobin[noteIndex] = currentRoundRobin;

            // Get the velocity layer index for this note/velocity combo
            int layerIdx = samplerEngine.getVelocityLayerIndex(midiNote, velocity);
            noteVelocityLayerIdx[noteIndex] = layerIdx;

            // Mark per-note velocity layer and RR as activated if pedal is down
            if (sustainPedalDown && layerIdx >= 0 && layerIdx < maxVelocityLayers)
            {
                noteLayersActivated[noteIndex][static_cast<size_t>(layerIdx)] = true;
                noteRRActivated[noteIndex][static_cast<size_t>(currentRoundRobin)] = true;
            }

            // Trigger sample playback
            samplerEngine.noteOn(midiNote, velocity, currentRoundRobin, sampleOffsetAmount);

            // Advance round-robin: 1 -> 2 -> 3 -> 1
            currentRoundRobin = (currentRoundRobin % 3) + 1;
        }
        else if (message.isNoteOff())
        {
            if (sustainPedalDown)
            {
                // Pedal is down - sustain the note visually
                noteSustained[noteIndex] = true;
            }
            else
            {
                // No pedal - release immediately
                noteVelocities[noteIndex] = 0;
                noteVelocityLayerIdx[noteIndex] = -1;
                noteRoundRobin[noteIndex] = 0;
                samplerEngine.noteOff(midiNote);
            }
        }
    }

    // Generate audio from sampler
    samplerEngine.processBlock(buffer);
}

juce::AudioProcessorEditor* MidiKeyboardProcessor::createEditor()
{
    return new MidiKeyboardEditor(*this);
}

void MidiKeyboardProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Save plugin state as XML
    juce::XmlElement xml("MidiKeyboardState");

    // Save sample folder path
    xml.setAttribute("sampleFolder", getLoadedFolderPath());

    // Save ADSR parameters
    auto adsr = getADSR();
    xml.setAttribute("attack", adsr.attack);
    xml.setAttribute("decay", adsr.decay);
    xml.setAttribute("sustain", adsr.sustain);
    xml.setAttribute("release", adsr.release);

    // Save preload size
    xml.setAttribute("preloadSizeKB", getPreloadSizeKB());

    // Save transpose
    xml.setAttribute("transpose", transposeAmount);

    // Save sample offset
    xml.setAttribute("sampleOffset", sampleOffsetAmount);

    copyXmlToBinary(xml, destData);
}

void MidiKeyboardProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // Restore plugin state from XML
    auto xml = getXmlFromBinary(data, sizeInBytes);

    if (xml != nullptr && xml->hasTagName("MidiKeyboardState"))
    {
        // Restore ADSR parameters
        float attack = static_cast<float>(xml->getDoubleAttribute("attack", 0.01));
        float decay = static_cast<float>(xml->getDoubleAttribute("decay", 0.1));
        float sustain = static_cast<float>(xml->getDoubleAttribute("sustain", 0.7));
        float release = static_cast<float>(xml->getDoubleAttribute("release", 0.3));
        setADSR(attack, decay, sustain, release);

        // Restore preload size
        int preloadSizeKB = xml->getIntAttribute("preloadSizeKB", 64);
        setPreloadSizeKB(preloadSizeKB);

        // Restore transpose
        int transpose = xml->getIntAttribute("transpose", 0);
        setTranspose(transpose);

        // Restore sample offset
        int sampleOffset = xml->getIntAttribute("sampleOffset", 0);
        setSampleOffset(sampleOffset);

        // Restore sample folder
        juce::String folderPath = xml->getStringAttribute("sampleFolder", "");
        if (folderPath.isNotEmpty())
        {
            juce::File folder(folderPath);
            if (folder.exists() && folder.isDirectory())
            {
                loadSamplesFromFolder(folder);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiKeyboardProcessor();
}
