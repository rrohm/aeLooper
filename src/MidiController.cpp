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
