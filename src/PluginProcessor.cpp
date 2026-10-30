#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

AeLooperAudioProcessor::AeLooperAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void AeLooperAudioProcessor::prepareToPlay(double sampleRate, int)
{
    engine.prepare(sampleRate);
}

bool AeLooperAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::stereo()
        || in == juce::AudioChannelSet::mono();
}

void AeLooperAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midiMessages)
{
    // Bar quantization: prefer the host tempo when one is provided
    // (VST3 in a DAW). The standalone player's playhead carries no tempo,
    // so the engine falls back to the user-set BPM. The host transport
    // position (when running) aligns the engine's bar grid to the host's
    // bars; time signature and transport state come with it.
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto tempo = position->getBpm())
                engine.setHostTempo(*tempo);
            else
                engine.clearHostTempo();

            if (auto hostTime = position->getTimeInSamples())
            {
                int numerator = 4;
                if (auto signature = position->getTimeSignature())
                    numerator = juce::jmax(1, signature->numerator);

                engine.updateHostGrid(*hostTime, numerator, position->getIsPlaying());
            }
            else
            {
                engine.clearHostGrid();
            }
        }
    }

    midiController.process(midiMessages, engine);
    engine.processBlock(buffer);

    // Diagnostics: level of the output bus as it leaves this call.
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = juce::jmax(peak, std::fabs(buffer.getSample(ch, i)));

    busLevel.store(peak, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* AeLooperAudioProcessor::createEditor()
{
    return new AeLooperAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AeLooperAudioProcessor();
}
