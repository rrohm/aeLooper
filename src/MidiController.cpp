#include "MidiController.h"
#include "LooperEngine.h"

void MidiController::process(const juce::MidiBuffer& midiInput, LooperEngine& engine)
{
    using Command = LooperChannel::Command;

    for (const auto metadata : midiInput)
    {
        const auto& message = metadata.getMessage();

        if (! message.isNoteOn())
            continue;

        const int note = message.getNoteNumber();

        auto dispatch = [&engine, note] (int baseNote, Command command)
        {
            const int channel = note - baseNote;
            if (channel >= 0 && channel < engine.getNumChannels())
                engine.pushCommand(channel, command);
        };

        dispatch(recordBaseNote, Command::toggleRecord);
        dispatch(muteBaseNote,   Command::toggleMute);
        dispatch(undoBaseNote,   Command::undo);
        dispatch(clearBaseNote,  Command::clear);
    }
}
