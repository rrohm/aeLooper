#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperEngine.h"
#include <functional>

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
    /**
        @param engineToUse          the engine to display and control
        @param outLevelProvider    optional callback returning the peak level
                                   of the final output bus (for the meter)
    */
    UiPanel(LooperEngine& engineToUse, std::function<float()> outLevelProvider = {});

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** Minimal peak meter, fed from the engine atomics by the timer. */
    struct LevelMeter : juce::Component
    {
        void setLevel(float newLevel) noexcept { level = newLevel; }
        void paint(juce::Graphics&) override;
        float level = 0.0f;
    };

    /**
        LED that flashes on each beat of the engine's beat clock, accented on
        the bar downbeat. Shows the grid that quantized takes snap to.
    */
    struct BeatLed : juce::Component
    {
        void setLevel(float newLevel, bool isAccent) noexcept
        {
            level = newLevel;
            accent = isAccent;
        }
        void paint(juce::Graphics&) override;
        float level = 0.0f;
        bool accent = false;
    };

    /**
        Passive state display: a rounded, dimly bordered box with the same
        corner radius as the buttons, text padded left by the strip gap.
        Not clickable.
    */
    struct StateLabel : juce::Component
    {
        void setText(const juce::String& newText) { text = newText; repaint(); }
        void setFontSize(float size)
        {
            font = juce::Font(juce::FontOptions().withHeight(size));
            repaint();
        }

        void paint(juce::Graphics& g) override;

        static constexpr float cornerRadius = 6.0f;  // matches TextButton (LookAndFeel_V4)
        juce::String text;
        juce::Font font { juce::FontOptions().withHeight(14.0f) };
    };

    struct ChannelStrip
    {
        explicit ChannelStrip(int index);

        juce::TextButton recordButton;
        juce::TextButton muteButton { "Mute" };   // color marks the state (see timerCallback)
        juce::TextButton undoButton { "Undo" };
        juce::TextButton clearButton { "Clear" };
        StateLabel stateLabel;
        LevelMeter meter;
        int channelIndex;
    };

    LooperEngine& engine;
    std::function<float()> outLevelProvider;

    static constexpr int stripGap = 6;   ///< horizontal gap between strip controls (also the label's left padding)

    /** LookAndFeel for the strip buttons with a scalable font size. */
    struct StripLookAndFeel : juce::LookAndFeel_V4
    {
        juce::Font getTextButtonFont(juce::TextButton&, int /*buttonHeight*/) override
        {
            return juce::Font(juce::FontOptions().withHeight(fontSize));
        }

        float fontSize = 16.0f;   // JUCE default button font cap at the initial size
    };

    StripLookAndFeel stripLookAndFeel;

    // Initial geometry, captured on the first resized() call; strip control
    // fonts scale with min(width, height) relative to it, never below the
    // original font size.
    int baseWidth = 0;
    int baseHeight = 0;
    float baseFontSize = 0.0f;

    juce::Label headerLabel;
    juce::ToggleButton quantizeButton { "Quantize to bars" };
    juce::ToggleButton metronomeButton { "Metronome" };
    juce::ToggleButton countInButton { "Count-in" };
    juce::Slider bpmSlider;
    juce::Slider latencySlider;
    juce::Label tempoSourceLabel;
    BeatLed beatLed;
    LevelMeter outMeter;   ///< final output bus level (left meter)
    LevelMeter inputMeter; ///< engine input level (right meter)
    std::vector<std::unique_ptr<ChannelStrip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UiPanel)
};
