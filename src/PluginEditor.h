#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "UiPanel.h"
#include "PluginProcessor.h"

/** Thin wrapper that embeds the shared UiPanel into the plugin. */
class AeLooperAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AeLooperAudioProcessorEditor(AeLooperAudioProcessor&);

    void resized() override;

private:
    AeLooperAudioProcessor& processor;
    UiPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AeLooperAudioProcessorEditor)
};
