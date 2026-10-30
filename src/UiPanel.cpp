#include "UiPanel.h"

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

    bpmSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 22);
    bpmSlider.setRange(30.0, 300.0, 0.1);
    bpmSlider.setValue(engine.getBpm(), juce::dontSendNotification);
    bpmSlider.setTextValueSuffix(" BPM");
    bpmSlider.onValueChange = [this] { engine.setBpm(bpmSlider.getValue()); };
    addAndMakeVisible(bpmSlider);

    tempoSourceLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(tempoSourceLabel);
    addAndMakeVisible(beatLed);

    for (int i = 0; i < engine.getNumChannels(); ++i)
    {
        auto strip = std::make_unique<ChannelStrip>(i);
        auto* s = strip.get();

        s->recordButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::toggleRecord); };
        s->muteButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::toggleMute); };
        s->undoButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::undo); };
        s->clearButton.onClick = [this, s] { engine.pushCommand(s->channelIndex, LooperChannel::Command::clear); };

        s->stateLabel.setJustificationType(juce::Justification::centredLeft);

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
    quantizeButton.setBounds(controls.removeFromLeft(150));
    controls.removeFromLeft(10);
    bpmSlider.setBounds(controls.removeFromLeft(230));
    controls.removeFromLeft(10);
    tempoSourceLabel.setBounds(controls);

    const int rowHeight = juce::jmax(30, bounds.getHeight() / juce::jmax(1, (int) strips.size()));

    for (auto& strip : strips)
    {
        auto row = bounds.removeFromTop(rowHeight).reduced(2);

        strip->recordButton.setBounds(row.removeFromLeft(juce::jmin(130, row.getWidth() / 5)));
        row.removeFromLeft(6);
        strip->muteButton.setBounds(row.removeFromLeft(juce::jmin(70, row.getWidth() / 5)));
        row.removeFromLeft(6);
        strip->undoButton.setBounds(row.removeFromLeft(juce::jmin(70, row.getWidth() / 5)));
        row.removeFromLeft(6);
        strip->clearButton.setBounds(row.removeFromLeft(juce::jmin(70, row.getWidth() / 5)));
        row.removeFromLeft(10);
        strip->meter.setBounds(row.removeFromRight(14).withSizeKeepingCentre(10, row.getHeight()));
        strip->stateLabel.setBounds(row);
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

        strip->stateLabel.setText(stateText(state, channel.getUndoDepth()), juce::dontSendNotification);

        strip->recordButton.setButtonText(
            state == LoopState::empty ? "Record"
            : state == LoopState::armed ? "Cancel"
            : state == LoopState::recording ? "Stop (set length)"
            : state == LoopState::overdubbing ? "Stop overdub"
            : "Overdub");

        strip->muteButton.setToggleState(channel.isMuted(), juce::dontSendNotification);
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

    // Beat LED: derive the flash phase from the engine's beat clock.
    {
        const double sr = engine.getSampleRate();
        const double beatLength = (60.0 / engine.getEffectiveBpm()) * sr;
        const int64_t barLength = (int64_t) (beatLength * 4.0);
        const int64_t clock = engine.getBeatClock();

        if (beatLength > 1.0 && sr > 0.0)
        {
            const auto sinceBeat = (int64_t) ((int64_t) clock % (int64_t) beatLength);
            const auto sinceBar = (int64_t) ((int64_t) clock % barLength);
            const double msSinceBeat = (double) sinceBeat / sr * 1000.0;
            const float level = (float) juce::jlimit(0.0, 1.0, 1.0 - msSinceBeat / 150.0);
            beatLed.setLevel(level, sinceBar < (int64_t) beatLength);
        }
        beatLed.repaint();
    }

    inputMeter.setLevel(juce::jmax(engine.getInputLevel(), inputMeter.level * 0.85f));
    inputMeter.repaint();

    if (outLevelProvider)
    {
        outMeter.setLevel(juce::jmax(outLevelProvider(), outMeter.level * 0.85f));
        outMeter.repaint();
    }
}
