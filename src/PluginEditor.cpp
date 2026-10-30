#include "PluginEditor.h"

AeLooperAudioProcessorEditor::AeLooperAudioProcessorEditor(AeLooperAudioProcessor& p)
    : AudioProcessorEditor(&p),
      processor(p),
      panel(p.getEngine())
{
    setSize(720, 480);
    addAndMakeVisible(panel);
}

void AeLooperAudioProcessorEditor::resized()
{
    panel.setBounds(getLocalBounds());
}
