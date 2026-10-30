#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "LooperEngine.h"
#include "MidiController.h"
#include <atomic>

/**
    Shared audio processor used by both targets:
      - the VST3 plugin (aeLooperPlugin)
      - the standalone app (wrapped in juce::AudioProcessorPlayer)

    The processor is a thin shell: MIDI is translated to engine commands,
    then the engine processes the audio in place.
*/
class AeLooperAudioProcessor : public juce::AudioProcessor
{
public:
    AeLooperAudioProcessor();
    ~AeLooperAudioProcessor() override = default;

    //==== juce::AudioProcessor ====
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "aeLooper"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    //==== aeLooper ====
    LooperEngine& getEngine() noexcept { return engine; }
    /** Peak level of the output bus as it leaves processBlock. */
    float getBusLevel() const noexcept { return busLevel.load(std::memory_order_relaxed); }

private:
    LooperEngine engine;
    MidiController midiController;
    std::atomic<float> busLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeLooperAudioProcessor)
};
