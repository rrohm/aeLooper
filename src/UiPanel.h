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

#include <juce_gui_basics/juce_gui_basics.h>
#include "LooperEngine.h"
#include <array>
#include <cstring>
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
        mute, SHIFT+[1..4] undoes, CTRL+[1..4] redoes. Unhandled keys bubble
        up further. */
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

        /** Copy in the channel's waveform thumbnail (call from the message
            thread; the engine-side buffer may be recycled at any time). */
        void setWaveform(const float* minV, const float* maxV, int columns, bool isRedo)
        {
            waveColumns = juce::jmin(columns, (int) waveMin.size());
            if (waveColumns > 0)
            {
                std::memcpy(waveMin.data(), minV, (size_t) waveColumns * sizeof(float));
                std::memcpy(waveMax.data(), maxV, (size_t) waveColumns * sizeof(float));
            }
            waveIsRedo = isRedo;
            repaint();
        }

        void paint(juce::Graphics& g) override;

        static constexpr float cornerRadius = 6.0f;  // matches TextButton (LookAndFeel_V4)
        juce::String text;
        juce::Font font { juce::FontOptions().withHeight(14.0f) };
        std::array<float, 256> waveMin {};
        std::array<float, 256> waveMax {};
        int waveColumns = 0;
        bool waveIsRedo = false;
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

    /**
        First field of a strip: status-label look (same fill and rounded
        border), width approx. a checkbox, showing the channel number in an
        oversized font that is clipped by the field on the sides and a bit at
        the top.
    */
    struct ChannelNumberField : juce::Component
    {
        explicit ChannelNumberField(int number) : channelNumber(number) {}

        void paint(juce::Graphics& g) override;

        static constexpr float cornerRadius = StateLabel::cornerRadius;
        int channelNumber;
    };

    struct ChannelStrip
    {
        explicit ChannelStrip(int index);

        ChannelNumberField numberField;
        juce::TextButton recordButton;
        juce::TextButton muteButton { "Mute" };   // color marks the state (see timerCallback)
        juce::TextButton undoButton { "Undo" };
        juce::TextButton redoButton { "Redo" };
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
