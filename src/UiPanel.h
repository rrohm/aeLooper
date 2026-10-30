#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperEngine.h"

/**
    The shared looper UI: a header with transport/loop info and one strip per
    loop channel (Record/Overdub, Mute, Undo, Clear, state display).

    Used both by the plugin editor and by the standalone window, so both
    targets look and behave identically. All state changes are pushed to the
    engine as commands; the display is refreshed from the engine atomics in a
    timer callback (no locks, no audio-thread access from the UI).
*/
class UiPanel : public juce::Component,
               private juce::Timer
{
public:
    explicit UiPanel(LooperEngine& engineToUse);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct ChannelStrip
    {
        explicit ChannelStrip(int index);

        juce::TextButton recordButton;
        juce::ToggleButton muteButton { "Mute" };
        juce::TextButton undoButton { "Undo" };
        juce::TextButton clearButton { "Clear" };
        juce::Label stateLabel;
        int channelIndex;
    };

    LooperEngine& engine;
    juce::Label headerLabel;
    std::vector<std::unique_ptr<ChannelStrip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UiPanel)
};
