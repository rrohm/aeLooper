#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

class LooperEngine;

/**
    Fixed MIDI note mapping for remote control (skeleton implementation).

    Channel-relative note bases (channel 1-4 => base note .. base note + 3):

    | Function      | Channel 1 | 2  | 3  | 4  |
    |---------------|-----------|----|----|----|
    | Record/Overdub| 36 (C2)   | 37 | 38 | 39 |
    | Mute          | 40        | 41 | 42 | 43 |
    | Undo          | 44        | 45 | 46 | 47 |
    | Clear         | 48        | 49 | 50 | 51 |

    Later this can become user-configurable (MIDI learn) by moving the
    mapping into AudioProcessorValueTreeState parameters or a config file.
*/
class MidiController
{
public:
    static constexpr int recordBaseNote = 36;
    static constexpr int muteBaseNote   = 40;
    static constexpr int undoBaseNote   = 44;
    static constexpr int clearBaseNote  = 48;

    /** Translate incoming note-ons into engine commands. */
    void process(const juce::MidiBuffer& midiInput, LooperEngine& engine);
};
