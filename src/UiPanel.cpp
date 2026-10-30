#include "UiPanel.h"

void UiPanel::StateLabel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(juce::Colour(0xff23262c));
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Waveform as background, anchored at the bottom edge (zero line one
    // half-height above the bottom, so the shape occupies the lower half).
    // Dimmed like a disabled button; extra-dim when showing the redoable
    // layer rather than what is currently heard.
    if (waveColumns > 0)
    {
        g.setColour(juce::Colours::lightgrey.withAlpha(waveIsRedo ? 0.22f : 0.45f));

        const float bottom = bounds.getBottom() - 2.0f;
        const float halfH = juce::jmax(1.0f, (bounds.getHeight() - 4.0f) * 0.5f);
        const float zeroY = bottom - halfH;

        for (int c = 0; c < waveColumns; ++c)
        {
            const float x = bounds.getX() + (c + 0.5f) * bounds.getWidth() / (float) waveColumns;
            const float y1 = zeroY - juce::jlimit(-1.0f, 1.0f, waveMax[(size_t) c]) * halfH;
            const float y2 = zeroY - juce::jlimit(-1.0f, 1.0f, waveMin[(size_t) c]) * halfH;
            g.drawLine(x, y1, x, juce::jmax(y1 + 1.0f, y2), 1.0f);
        }
    }

    g.setColour(juce::Colours::darkgrey);
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    // Text centered like the button captions.
    g.setColour(juce::Colours::lightgrey);
    g.setFont(font);
    g.drawFittedText(text, bounds.reduced(2.0f).toNearestInt(), juce::Justification::centred, 1);
}

void UiPanel::StatusBar::paint(juce::Graphics& g)
{
    const auto baseColour = juce::Colour(0xff2a3f5f);   // progress-bar blue as the base

    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(juce::Colour(0xff23262c));
    g.fillRoundedRectangle(bounds, 6.0f);

    if (info.quantized)
    {
        // Bars x beats as rounded boxes up to the border; the current beat is
        // filled, downbeats get a brighter border than the other beats.
        auto inner = bounds.reduced(1.0f, 1.0f);   // just inside the 1 px border

        const int n = juce::jmax(0, info.totalBoxes);
        if (n > 0 && inner.getWidth() > 0.0f)
        {
            constexpr float boxGap = 4.0f;
            const float boxW = (inner.getWidth() - boxGap * (n - 1)) / (float) n;
            const float boxH = inner.getHeight();

            for (int i = 0; i < n; ++i)
            {
                juce::Rectangle<float> box(inner.getX() + i * (boxW + boxGap),
                                           inner.getY(), boxW, boxH);
                const bool isDownbeat = (i % info.beatsPerBar) == 0;

                if (i == info.currentBox)
                {
                    g.setColour(baseColour.withMultipliedBrightness(1.8f));
                    g.fillRoundedRectangle(box, 3.0f);
                }
                else
                {
                    g.setColour(isDownbeat ? baseColour.withMultipliedBrightness(1.4f)
                                           : baseColour.withMultipliedBrightness(0.7f));
                    g.drawRoundedRectangle(box, 3.0f, 1.0f);
                }
            }
        }

        g.setColour(baseColour.withMultipliedBrightness(0.7f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
    else
    {
        // Progress fill runs right up to the border (no gap or padding).
        if (info.progress > 0.0)
        {
            g.setColour(baseColour);
            g.fillRoundedRectangle(bounds.withWidth(bounds.getWidth() * (float) info.progress), 6.0f);
        }

        g.setColour(baseColour.withMultipliedBrightness(0.7f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

        // Info text with a half-em left padding.
        g.setColour(juce::Colours::lightgrey);
        g.setFont(font);
        const float leftPad = font.getHeight() * 0.5f;
        g.drawFittedText(info.text, bounds.reduced(leftPad, 0.0f).toNearestInt(),
                         juce::Justification::centredLeft, 1);
    }
}

UiPanel::UiPanel(LooperEngine& engineToUse, std::function<float()> outLevelProvider)
    : engine(engineToUse),
      outLevelProvider(std::move(outLevelProvider))
{
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outMeter);
    addAndMakeVisible(statusBar);

    // ---- bar quantization controls ----
    quantizeButton.onClick = [this]
    {
        engine.setQuantizeEnabled(quantizeButton.getToggleState());
    };
    addAndMakeVisible(quantizeButton);

    metronomeButton.onClick = [this]
    {
        engine.setMetronomeEnabled(metronomeButton.getToggleState());
    };
    addAndMakeVisible(metronomeButton);

    countInButton.onClick = [this]
    {
        engine.setMetronomeCountInOnly(countInButton.getToggleState());
    };
    countInButton.setTooltip("Metronome only before the first take (count-in), not permanently");
    addAndMakeVisible(countInButton);

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    bpmSlider.setRange(30.0, 300.0, 0.1);
    bpmSlider.setValue(engine.getBpm(), juce::dontSendNotification);
    bpmSlider.setTextValueSuffix(" BPM");
    bpmSlider.onValueChange = [this] { engine.setBpm(bpmSlider.getValue()); };
    addAndMakeVisible(bpmSlider);

    latencySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    latencySlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    latencySlider.setRange(0.0, 8192.0, 1.0);
    latencySlider.setValue((double) engine.getLatency(), juce::dontSendNotification);
    latencySlider.setTextValueSuffix(" smp");
    latencySlider.onValueChange = [this]
    {
        engine.setLatency((int64_t) latencySlider.getValue());
    };
    latencySlider.setTooltip("Round-trip latency compensation (samples)");
    addAndMakeVisible(latencySlider);

    tempoSourceLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(tempoSourceLabel);
    addAndMakeVisible(beatLed);

    for (auto* toggle : { &quantizeButton, &metronomeButton, &countInButton })
        toggle->setMouseCursor(juce::MouseCursor::PointingHandCursor);

    for (int i = 0; i < engine.getNumChannels(); ++i)
    {
        auto strip = std::make_unique<ChannelStrip>(i);
        auto* s = strip.get();

        s->recordButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::toggleRecord); };
        s->muteButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::toggleMute); };
        s->undoButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::undo); };
        s->redoButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::redo); };
        s->clearButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::clear); };

        // Hand cursor over the clickable controls.
        for (auto* button : { &s->recordButton, &s->muteButton, &s->undoButton, &s->redoButton, &s->clearButton })
        {
            button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
            button->setLookAndFeel(&stripLookAndFeel);
        }

        const auto keyName = juce::String(s->channelIndex + 1);
        s->recordButton.setTooltip("Toggle record/overdub (key " + keyName + ")");
        s->muteButton.setTooltip("Toggle mute (Alt+" + keyName + ")");
        s->undoButton.setTooltip("Undo last overdub (Shift+" + keyName + ")");
        s->redoButton.setTooltip("Redo (Ctrl+" + keyName + ")");
        s->clearButton.setTooltip("Clear channel");

        addAndMakeVisible(s->numberField);
        addAndMakeVisible(s->recordButton);
        addAndMakeVisible(s->muteButton);
        addAndMakeVisible(s->undoButton);
        addAndMakeVisible(s->redoButton);
        addAndMakeVisible(s->clearButton);
        addAndMakeVisible(s->stateLabel);
        addAndMakeVisible(s->meter);

        strips.push_back(std::move(strip));
    }

    startTimerHz(20);

    setWantsKeyboardFocus(true);
}

void UiPanel::parentHierarchyChanged()
{
    // Take keyboard focus when the panel is placed in a window, so the key
    // commands work immediately. (Clicks on buttons move focus to them, but
    // unhandled keys still bubble up to this panel.)
    if (getParentComponent() != nullptr)
        grabKeyboardFocus();
}

bool UiPanel::keyPressed(const juce::KeyPress& key)
{
    const int keyCode = key.getKeyCode();

    // Accept the digit both from the main row and from the numpad.
    int digit = 0;
    if (keyCode >= '1' && keyCode <= '9')
        digit = keyCode - '0';
    else if (keyCode >= juce::KeyPress::numberPad0 && keyCode <= juce::KeyPress::numberPad9)
        digit = keyCode - juce::KeyPress::numberPad0;

    if (digit >= 1 && digit <= engine.getNumChannels())
    {
        const int channel = digit - 1;
        const auto mods = key.getModifiers();

        if (mods.isAltDown())
            engine.pushCommand(channel, LooperChannel::Command::toggleMute);
        else if (mods.isShiftDown())
            engine.pushCommand(channel, LooperChannel::Command::undo);
        else if (mods.isCtrlDown() || mods.isCommandDown())
            engine.pushCommand(channel, LooperChannel::Command::redo);
        else
            engine.pushCommand(channel, LooperChannel::Command::toggleRecord);

        return true;
    }

    return false;
}

void UiPanel::ChannelNumberField::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(juce::Colour(0xff23262c));
    g.fillRoundedRectangle(bounds, cornerRadius);
    g.setColour(juce::Colours::darkgrey);
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    // Fixed oversized font (at the default window size the field is ~24 px
    // wide, so the ~48 px wide digit is about twice the field width). Drawn
    // without fitting, so it is clipped by the field bounds on the sides;
    // the baseline is placed so the glyph top sits a few px above the field.
    constexpr float bigFontSize = 88.0f;
    auto font = juce::Font(juce::FontOptions().withHeight(bigFontSize));
    g.setFont(font);
    g.setColour(juce::Colours::lightgrey);

    const auto text = juce::String(channelNumber);
    const float x = bounds.getCentreX() - font.getStringWidthFloat(text) * 0.5f;
    const float baseline = bounds.getY() + font.getAscent() - 4.0f;
    g.drawSingleLineText(text, (int) x, (int) baseline);
}

UiPanel::ChannelStrip::ChannelStrip(int index)
    : numberField(index + 1),
      channelIndex(index)
{
}

void UiPanel::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.2f));
}

void UiPanel::LevelMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colours::black);

    const float levelToShow = juce::jlimit(0.0f, 1.0f, level);
    auto barHeight = bounds.getHeight() * levelToShow;
    g.setColour(levelToShow > 0.85f ? juce::Colours::red
                : levelToShow > 0.5f  ? juce::Colours::orange
                                     : juce::Colours::green);
    g.fillRect(bounds.removeFromBottom(barHeight));
}

void UiPanel::BeatLed::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colours::black);

    const auto colour = accent ? juce::Colours::orange : juce::Colours::limegreen;
    const auto circle = bounds.reduced(2.0f);

    // dim base glow so the idle LED stays visible, full brightness at the beat
    g.setColour(colour.withAlpha(juce::jlimit(0.15f, 1.0f, level)));
    g.fillEllipse(circle);
    g.setColour(juce::Colours::darkgrey);
    g.drawEllipse(circle, 1.0f);
}

void UiPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    auto topRow = bounds.removeFromTop(28);
    inputMeter.setBounds(topRow.removeFromRight(14).withSizeKeepingCentre(10, 24));
    outMeter.setBounds(topRow.removeFromRight(14).withSizeKeepingCentre(10, 24));

    auto controls = bounds.removeFromTop(34);
    beatLed.setBounds(controls.removeFromRight(30).withSizeKeepingCentre(24, 24));
    quantizeButton.setBounds(controls.removeFromLeft(140));
    controls.removeFromLeft(8);
    bpmSlider.setBounds(controls.removeFromLeft(180));
    controls.removeFromLeft(8);
    metronomeButton.setBounds(controls.removeFromLeft(100));
    controls.removeFromLeft(6);
    countInButton.setBounds(controls.removeFromLeft(85));
    controls.removeFromLeft(8);
    latencySlider.setBounds(controls.removeFromLeft(170));
    controls.removeFromLeft(8);
    tempoSourceLabel.setBounds(controls);

    statusBar.setBounds(bounds.removeFromBottom(32));
    bounds.removeFromBottom(8);   // gap between lowest strip and status bar = window margin

    // Font scaling must be known before the strip layout (the number field's
    // width follows the strip font size). The reference geometry is captured
    // once, only from a sane layout; the base font is a constant (the header
    // controls' font at the design size), independent of runtime geometry.
    if (baseWidth <= 0 && getWidth() >= 200 && getHeight() >= 200)
    {
        baseWidth = getWidth();
        baseHeight = getHeight();
    }

    const int refWidth = baseWidth > 0 ? baseWidth : designWidth;
    const int refHeight = baseWidth > 0 ? baseHeight : designHeight;

    // Scale with both window dimensions, but subtly: the ratio above 1 is
    // damped and capped, so the fonts return to the default at the original
    // size, never go below it, and never get huge (max +20%).
    const float sizeRatio = juce::jmin((float) getWidth() / (float) refWidth,
                                       (float) getHeight() / (float) refHeight);
    const float fontSize = baseFontSizeNominal * (1.0f + juce::jlimit(0.0f, 0.4f, sizeRatio - 1.0f) * 0.5f);

    stripLookAndFeel.fontSize = fontSize;

    // Number field width: approx. the width of a checkbox (toggle tick box).
    const int numberFieldWidth = (int) (fontSize * 1.1f) + 8;

    const int rowHeight = juce::jmax(30, bounds.getHeight() / juce::jmax(1, (int) strips.size()));

    for (auto& strip : strips)
    {
        auto row = bounds.removeFromTop(rowHeight).reduced(2);

        strip->meter.setBounds(row.removeFromRight(14).withSizeKeepingCentre(10, row.getHeight()));
        row.removeFromRight(8);

        // Number field, then five equally wide buttons, then the state label
        // at 1.5x button width.
        const float buttonWidth = (float) (row.getWidth() - numberFieldWidth - 6 * stripGap) / 6.5f;
        const int labelWidth = (int) (buttonWidth * 1.5f);

        strip->numberField.setBounds(row.removeFromLeft(numberFieldWidth));
        row.removeFromLeft(stripGap);
        strip->recordButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->muteButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->undoButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->redoButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->clearButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->stateLabel.setBounds(row.removeFromLeft(labelWidth));

        strip->stateLabel.setFontSize(fontSize);
    }

    repaint();
}

void UiPanel::timerCallback()
{
    auto stateText = [](LoopState state, int undoDepth)
    {
        switch (state)
        {
            case LoopState::empty:       return juce::String("empty");
            case LoopState::armed:       return juce::String("waiting for bar...");
            case LoopState::recording:   return juce::String("RECORDING (defines loop length)");
            case LoopState::overdubbing: return juce::String("overdubbing");
            case LoopState::playing:     return juce::String("playing");
            case LoopState::stopped:     return juce::String("stopped");
        }
        return juce::String("unknown") + " (undo depth " + juce::String(undoDepth) + ")";
    };

    // Status bar content (free mode: text + progress; quantize mode: beat boxes)
    StatusBar::Info info;
    info.quantized = engine.isQuantizeEnabled();
    info.beatsPerBar = engine.getBeatsPerBar();

    const double sr = engine.getSampleRate();
    const double beatLen = (60.0 / engine.getEffectiveBpm()) * sr;
    const double barLen = beatLen * info.beatsPerBar;

    if (engine.isLoopLengthKnown())
    {
        const double loopS = engine.getLoopLengthSeconds();
        const double posS = engine.getPositionSeconds();

        info.text = "Loop: " + juce::String(loopS, 3) + " s    pos: "
                    + juce::String(posS, 3) + " s    " + juce::String((int) sr) + " Hz";
        info.progress = loopS > 0.0 ? posS / loopS : 0.0;

        const int64_t lenSamples = (int64_t) (loopS * sr + 0.5);
        const int64_t posSamples = (int64_t) (posS * sr + 0.5);
        const int64_t beatLenS = (int64_t) (beatLen + 0.5);
        const int64_t barLenS = (int64_t) (barLen + 0.5);

        const int bars = barLenS > 0 ? (int) (lenSamples / barLenS) : 0;
        info.totalBoxes = bars * info.beatsPerBar;

        // Box index = barIndex * beatsPerBar + beatIndexWithinBar.
        const int64_t barIndex = barLenS > 0 ? posSamples / barLenS : 0;
        const int64_t beatInBar = beatLenS > 0 ? (posSamples % barLenS) / beatLenS : 0;
        info.currentBox = juce::jlimit(0, juce::jmax(0, info.totalBoxes - 1),
                                       (int) (barIndex * info.beatsPerBar + beatInBar));
    }
    else
    {
        info.text = "No loop yet - record the first take on any channel to set the loop length";
        info.progress = 0.0;
        info.totalBoxes = info.beatsPerBar;   // one empty bar while waiting
        info.currentBox = -1;
    }

    statusBar.setState(info);

    for (auto& strip : strips)
    {
        auto& channel = engine.getChannel(strip->channelIndex);
        const auto state = channel.getState();

        strip->stateLabel.setText(stateText(state, channel.getUndoDepth()));

        // Waveform background: the redoable layer (dimmed) whenever the redo
        // stack is non-empty, otherwise the current loop content.
        const auto& thumb = channel.getDisplayThumb();
        strip->stateLabel.setWaveform(thumb.minV.data(), thumb.maxV.data(),
                                      thumb.validColumns, channel.getRedoDepth() > 0);

        strip->recordButton.setButtonText(
            state == LoopState::empty ? "Record"
            : state == LoopState::armed ? "Cancel"
            : state == LoopState::recording ? "Stop (set length)"
            : state == LoopState::overdubbing ? "Stop overdub"
            : "Overdub");

        // Mute is a plain TextButton; its colors mark the state:
        // dimmed grey when not muted, subdued red when muted.
        if (channel.isMuted())
        {
            strip->muteButton.setColour(juce::TextButton::buttonColourId,
                                        juce::Colour(0xff8b2323));
            strip->muteButton.setColour(juce::TextButton::textColourOffId,
                                        juce::Colours::white);
        }
        else
        {
            strip->muteButton.setColour(juce::TextButton::buttonColourId,
                                        juce::Colour(0xff2c2f36));
            strip->muteButton.setColour(juce::TextButton::textColourOffId,
                                        juce::Colours::grey);
        }

        strip->undoButton.setEnabled(channel.getUndoDepth() > 0);
        strip->redoButton.setEnabled(channel.getRedoDepth() > 0);
        // peak hold with decay so short transients stay visible
        strip->meter.setLevel(juce::jmax(channel.getOutputLevel(),
                                         strip->meter.level * 0.85f));
        strip->meter.repaint();
    }

    tempoSourceLabel.setText(
        juce::String("tempo: ") + juce::String(engine.getEffectiveBpm(), 1) + " BPM ("
        + (engine.hasHostTempo() ? "host" : "internal") + ")",
        juce::dontSendNotification);

    // Beat LED: only meaningful with bar quantization (otherwise the free
    // loop's start wanders across the beat grid and the flashes would
    // mislead). Visible only while quantization is on.
    beatLed.setVisible(engine.isQuantizeEnabled());

    if (beatLed.isVisible())
    {
        const double sr = engine.getSampleRate();
        const double beatLength = (60.0 / engine.getEffectiveBpm()) * sr;
        const int64_t clock = engine.getBeatClock() + engine.getGridOffset();
        const int64_t barLength = (int64_t) (beatLength * 4.0);

        if (beatLength > 1.0 && sr > 0.0)
        {
            const auto sinceBeat = loopMod(clock, (int64_t) beatLength);
            const auto sinceBar = loopMod(clock, barLength);
            const double msSinceBeat = (double) sinceBeat / sr * 1000.0;
            const float level = (float) juce::jlimit(0.0, 1.0, 1.0 - msSinceBeat / 150.0);
            beatLed.setLevel(level, sinceBar < (int64_t) beatLength);
        }
        beatLed.repaint();
    }

    metronomeButton.setToggleState(engine.isMetronomeEnabled(), juce::dontSendNotification);
    countInButton.setToggleState(engine.isMetronomeCountInOnly(), juce::dontSendNotification);
    countInButton.setEnabled(engine.isMetronomeEnabled());

    inputMeter.setLevel(juce::jmax(engine.getInputLevel(), inputMeter.level * 0.85f));
    inputMeter.repaint();

    if (outLevelProvider)
    {
        outMeter.setLevel(juce::jmax(outLevelProvider(), outMeter.level * 0.85f));
        outMeter.repaint();
    }
}
