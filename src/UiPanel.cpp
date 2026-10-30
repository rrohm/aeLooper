#include "UiPanel.h"

void UiPanel::StateLabel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);

    g.setColour(juce::Colour(0xff23262c));
    g.fillRoundedRectangle(bounds, cornerRadius);
    g.setColour(juce::Colours::darkgrey);
    g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);

    g.setColour(juce::Colours::lightgrey);
    g.setFont(font);

    // Left padding equals the horizontal gap between the strip controls.
    auto textArea = bounds.reduced((float) UiPanel::stripGap, 0.0f);
    g.drawFittedText(text, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
}

UiPanel::UiPanel(LooperEngine& engineToUse, std::function<float()> outLevelProvider)
    : engine(engineToUse),
      outLevelProvider(std::move(outLevelProvider))
{
    headerLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(headerLabel);
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outMeter);

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
        s->clearButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::clear); };

        // Hand cursor over the clickable controls.
        for (auto* button : { &s->recordButton, &s->muteButton, &s->undoButton, &s->clearButton })
        {
            button->setMouseCursor(juce::MouseCursor::PointingHandCursor);
            button->setLookAndFeel(&stripLookAndFeel);
        }

        addAndMakeVisible(s->recordButton);
        addAndMakeVisible(s->muteButton);
        addAndMakeVisible(s->undoButton);
        addAndMakeVisible(s->clearButton);
        addAndMakeVisible(s->stateLabel);
        addAndMakeVisible(s->meter);

        strips.push_back(std::move(strip));
    }

    startTimerHz(20);
}

UiPanel::ChannelStrip::ChannelStrip(int index)
    : channelIndex(index)
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

    auto header = bounds.removeFromTop(28);
    inputMeter.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 24));
    outMeter.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 24));
    headerLabel.setBounds(header);

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

    const int rowHeight = juce::jmax(30, bounds.getHeight() / juce::jmax(1, (int) strips.size()));

    for (auto& strip : strips)
    {
        auto row = bounds.removeFromTop(rowHeight).reduced(2);

        strip->meter.setBounds(row.removeFromRight(14).withSizeKeepingCentre(10, row.getHeight()));
        row.removeFromRight(8);

        // All four buttons share one equal width; the state label is 1.5x that.
        const float buttonWidth = (float) (row.getWidth() - 4 * stripGap) / 5.5f;
        const int labelWidth = (int) (buttonWidth * 1.5f);

        strip->recordButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->muteButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->undoButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->clearButton.setBounds(row.removeFromLeft((int) buttonWidth));
        row.removeFromLeft(stripGap);
        strip->stateLabel.setBounds(row.removeFromLeft(labelWidth));
    }

    // Capture the initial geometry once. The base font size is taken from the
    // header controls, so the strips match them at startup; it is also the
    // minimum font size.
    if (baseWidth <= 0 && getWidth() > 0)
    {
        baseWidth = getWidth();
        baseHeight = getHeight();
        // Same formula as LookAndFeel_V4::drawToggleButton, so the strips
        // match the header controls at startup; this is also the minimum.
        baseFontSize = juce::jmin(15.0f, (float) quantizeButton.getHeight() * 0.75f);
    }

    if (baseWidth > 0)
    {
        // Scale with both window dimensions, but subtly: the ratio above 1 is
        // damped and capped, so the fonts return to the default at the original
        // size, never go below it, and never get huge (max +20%).
        const float sizeRatio = juce::jmin((float) getWidth() / (float) baseWidth,
                                           (float) getHeight() / (float) baseHeight);
        const float fontSize = baseFontSize * (1.0f + juce::jlimit(0.0f, 0.4f, sizeRatio - 1.0f) * 0.5f);

        stripLookAndFeel.fontSize = fontSize;

        for (auto& strip : strips)
            strip->stateLabel.setFontSize(fontSize);

        repaint();
    }
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

    if (engine.isLoopLengthKnown())
        headerLabel.setText(juce::String("Loop length: ")
                                + juce::String(engine.getLoopLengthSeconds(), 3)
                                + " s    Position: "
                                + juce::String(engine.getPositionSeconds(), 3)
                                + " s    "
                                + juce::String((int) engine.getSampleRate()) + " Hz",
                            juce::dontSendNotification);
    else
        headerLabel.setText("No loop yet - record the first take on any channel to set the loop length",
                            juce::dontSendNotification);

    for (auto& strip : strips)
    {
        auto& channel = engine.getChannel(strip->channelIndex);
        const auto state = channel.getState();

        strip->stateLabel.setText(stateText(state, channel.getUndoDepth()));

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
