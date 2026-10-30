#include "PluginEditor.h"

AeLooperAudioProcessorEditor::AeLooperAudioProcessorEditor(AeLooperAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      panel(p.getEngine(), [&p] { return p.getBusLevel(); })
{
    setSize(780, 530);
    addAndMakeVisible(panel);
}

void AeLooperAudioProcessorEditor::resized()
{
    panel.setBounds(getLocalBounds());
}
