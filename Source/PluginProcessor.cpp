#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiKeyboardProcessor::MidiKeyboardProcessor()
    : AudioProcessor(BusesProperties())
{
    noteVelocities.fill(0);
    noteRoundRobin.fill(0);
}

void MidiKeyboardProcessor::prepareToPlay(double, int)
{
    noteVelocities.fill(0);
    noteRoundRobin.fill(0);
    currentRoundRobin = 1;
}

void MidiKeyboardProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        auto noteIndex = static_cast<size_t>(message.getNoteNumber());

        if (message.isNoteOn())
        {
            noteVelocities[noteIndex] = message.getVelocity();
            noteRoundRobin[noteIndex] = currentRoundRobin;

            // Advance round-robin: 1 -> 2 -> 3 -> 1
            currentRoundRobin = (currentRoundRobin % 3) + 1;
        }
        else if (message.isNoteOff())
        {
            noteVelocities[noteIndex] = 0;
            noteRoundRobin[noteIndex] = 0;
        }
    }
}

juce::AudioProcessorEditor* MidiKeyboardProcessor::createEditor()
{
    return new MidiKeyboardEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiKeyboardProcessor();
}
