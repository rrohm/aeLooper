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

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UiPanel.h"
#include "ColorScheme.h"
#include <iostream>

/**
    Standalone entry point. The same AeLooperAudioProcessor used by the VST3
    plugin is wrapped in a juce::AudioProcessorPlayer, which routes the audio
    device I/O and the MIDI input into processBlock().
*/

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow()
        : juce::DocumentWindow("aeLooper",
                               juce::Desktop::getInstance().getDefaultLookAndFeel()
                                   .findColour(juce::ResizableWindow::backgroundColourId),
                               juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setBackgroundColour(ColorScheme::background());

        audioDeviceManager.initialiseWithDefaultDevices(2, 2);

        // Register the player as audio callback; without this the device
        // runs but processBlock() is never invoked.
        audioDeviceManager.addAudioCallback(&player);

        // Log which device was actually opened; visible in Qt Creator's
        // "Application output" pane when the debugger is attached.
        juce::String deviceInfo;
        if (auto* currentDevice = audioDeviceManager.getCurrentAudioDevice())
        {
            deviceInfo << audioDeviceManager.getCurrentAudioDeviceType()
                       << " " << currentDevice->getName()
                       << ", " << (int) currentDevice->getCurrentSampleRate() << " Hz"
                       << ", block " << currentDevice->getCurrentBufferSizeSamples()
                       << ", active inputs "
                       << currentDevice->getActiveInputChannels().countNumberOfSetBits()
                       << ", active outputs "
                       << currentDevice->getActiveOutputChannels().countNumberOfSetBits();
        }
        else
        {
            deviceInfo = "NO AUDIO DEVICE OPENED";
        }

        std::cerr << "aeLooper audio: " << deviceInfo << std::endl;
        if (auto* currentDevice = audioDeviceManager.getCurrentAudioDevice())
        {
            const int deviceLatency = currentDevice->getInputLatencyInSamples()
                                    + currentDevice->getOutputLatencyInSamples();
            processor->getEngine().setLatency(deviceLatency);
            std::cerr << "aeLooper latency (in+out): " << deviceLatency
                      << " samples" << std::endl;

            const auto midiCount = juce::MidiInput::getAvailableDevices().size();
            std::cerr << "aeLooper midi inputs available: " << midiCount << std::endl;
        }
        setName("aeLooper  [" + deviceInfo + "]");

        // Enable the first available MIDI input; its events are forwarded to
        // the processor by the AudioProcessorPlayer.
        const auto midiInputs = juce::MidiInput::getAvailableDevices();
        if (! midiInputs.isEmpty())
        {
            midiInputId = midiInputs[0].identifier;
            audioDeviceManager.setMidiInputDeviceEnabled(midiInputId, true);
            audioDeviceManager.addMidiInputDeviceCallback(midiInputId, &player);
        }

        player.setProcessor(processor.get());

        setContentOwned(new UiPanel(processor->getEngine(),
                                   [proc = processor.get()] { return proc->getBusLevel(); }),
                        true);
        setResizable(true, false);
        centreWithSize(1000, 530);
        setVisible(true);
    }

    ~MainWindow() override
    {
        player.setProcessor(nullptr);
        audioDeviceManager.removeAudioCallback(&player);
        if (midiInputId.isNotEmpty())
            audioDeviceManager.removeMidiInputDeviceCallback(midiInputId, &player);
        audioDeviceManager.closeAudioDevice();
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    juce::AudioDeviceManager audioDeviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<AeLooperAudioProcessor> processor = std::make_unique<AeLooperAudioProcessor>();
    juce::String midiInputId;
};

class AeLooperApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "aeLooper"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(AeLooperApplication)
