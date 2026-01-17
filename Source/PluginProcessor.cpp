#include "PluginProcessor.h"
#include "PluginEditor.h"

MidiKeyboardProcessor::MidiKeyboardProcessor()
    : AudioProcessor(BusesProperties())
{
    noteVelocities.fill(0);
}

void MidiKeyboardProcessor::prepareToPlay(double, int)
{
    noteVelocities.fill(0);
}

void MidiKeyboardProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            noteVelocities[static_cast<size_t>(message.getNoteNumber())] = message.getVelocity();
        }
        else if (message.isNoteOff())
        {
            noteVelocities[static_cast<size_t>(message.getNoteNumber())] = 0;
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
