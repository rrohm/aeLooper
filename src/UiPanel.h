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

    /** Key commands: [1..4] toggle record of the channel, ALT+[1..4] toggles
        mute, SHIFT+[1..4] undoes. Unhandled keys bubble up further. */
    bool keyPressed(const juce::KeyPress&) override;
    void parentHierarchyChanged() override;

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

    /**
        Status bar at the bottom of the window.
        Free-running mode: rounded border (like the state label) with a
        loop-position progress bar as background and the info text on top.
        Quantize mode: bars x beatsPerBar rounded boxes, border only, with
        the current beat's box filled; the text is hidden.
    */
    struct StatusBar : juce::Component
    {
        struct Info
        {
            juce::String text;
            bool quantized = false;
            double progress = 0.0;      ///< 0..1 position within the loop
            int totalBoxes = 0;         ///< bars * beatsPerBar (quantize mode)
            int currentBox = -1;        ///< filled box (quantize mode)
            int beatsPerBar = 4;
        };

        void setState(const Info& newInfo) { info = newInfo; repaint(); }
        void paint(juce::Graphics& g) override;

        Info info;
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

        float fontSize = 16.0f;
    };

    StripLookAndFeel stripLookAndFeel;

    // Nominal design size of the panel, used as reference for font scaling
    // (and as fallback if the first layout is degenerate).
    static constexpr int designWidth = 1000;
    static constexpr int designHeight = 530;

    // Font size of the header controls at the design size (toggle-button font
    // formula min(15, height*0.75) at the 34 px controls row). The strips start
    // at this size; it is the minimum and the size they return to.
    static constexpr float baseFontSizeNominal = 15.0f;

    // Reference geometry, captured on the first sane resized() call.
    int baseWidth = 0;
    int baseHeight = 0;

    StatusBar statusBar;
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
