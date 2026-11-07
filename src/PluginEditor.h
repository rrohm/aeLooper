// aeLooper - A multichannel audio looper for Linux, built with JUCE.
// 
// Copyright (C) 2026 Robert Rohm
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your option) 
// any later version. This program is distributed in the hope that it will be useful, 
// but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more 
// details. 
// 
// You should have received a copy of the GNU General Public License along with 
// this program. If not, see <http://www.gnu.org/licenses/>.

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
