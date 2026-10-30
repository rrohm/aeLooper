#include "UiPanel.h"

UiPanel::UiPanel(LooperEngine& engineToUse, std::function<float()> outLevelProvider)
    : engine(engineToUse),
      outLevelProvider(std::move(outLevelProvider))
{
    headerLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(headerLabel);
    addAndMakeVisible(inputMeter);
    addAndMakeVisible(outMeter);

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

void UiPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    auto header = bounds.removeFromTop(28);
    inputMeter.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 24));
    outMeter.setBounds(header.removeFromRight(14).withSizeKeepingCentre(10, 24));
    headerLabel.setBounds(header);

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

    inputMeter.setLevel(juce::jmax(engine.getInputLevel(), inputMeter.level * 0.85f));
    inputMeter.repaint();

    if (outLevelProvider)
    {
        outMeter.setLevel(juce::jmax(outLevelProvider(), outMeter.level * 0.85f));
        outMeter.repaint();
    }
}
