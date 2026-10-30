# aeLooper

A 4-channel audio looper for Linux built with JUCE. The first recorded take
defines the loop length for all channels; every channel can overdub, mute and
undo independently, and the whole looper is remote-controllable via MIDI notes.
One codebase builds both a standalone application and a VST3 plugin.

## Concept

- **4 parallel loop channels.** Each channel accumulates its audio in a single
  buffer (non-destructive overdub: input is added to the existing material).
- **First take defines the global loop length.** While no length is known, a
  recording channel appends input to a growing buffer. When that take is
  stopped, its length in samples becomes the transport loop length for all
  channels; other channels that were still recording are conformed
  (truncated/padded with silence) to that length.
- **Per-channel mute** (output only - recording/overdubbing continues).
- **Per-channel undo.** Before each overdub a snapshot of the accumulated
  buffer is pushed on that channel's undo stack; undo pops it. The first take
  pushes an "empty" snapshot, so undo also removes the base take.
- **MIDI remote control** (note-on messages, fixed mapping for now):

  | Function       | Ch 1     | Ch 2 | Ch 3 | Ch 4 |
  |----------------|----------|------|------|------|
  | Record/Overdub | 36 (C2)  | 37   | 38   | 39   |
  | Mute           | 40       | 41   | 42   | 43   |
  | Undo           | 44       | 45   | 46   | 47   |
  | Clear          | 48       | 49   | 50   | 51   |

- **Free loop length by default.** Length = exact sample count of the first
  take. Optionally, **bar quantization** can be enabled ("Quantize to bars"
  toggle in the UI): the record press *arms* the take and it begins exactly
  at the next bar boundary of the engine's free-running beat clock;
  stopping is quantized to the next bar as well, so the loop length is an
  exact multiple of one bar (4/4). The tempo comes from the host (VST3 in a
  DAW, via `AudioPlayHead`) or from the manual BPM slider when no host
  tempo is available (standalone). The **beat LED** in the controls row
  flashes on each beat of that clock (accented on the downbeat); it is only
  visible while quantization is on, because in free-length mode the loop
  start wanders across the beat grid and the flashes would mislead. A
  second record press cancels an armed take.
- **Metronome** ("Metronome" toggle): a click at each beat of the clock,
  accented on the downbeat, mixed into the output regardless of channel
  mutes. The clicks are synthesized (sine burst with exponential decay,
  re-rendered for the current sample rate) rather than loaded from files;
  if natural percussion sounds are wanted later, only the click-buffer
  generation in `LooperEngine::prepare()` needs to change (e.g. BinaryData
  WAVs).
- **Latency compensation** ("Latency" slider, in samples): recording writes
  are shifted earlier by the configured round-trip latency, and a stopped
  base take keeps recording that many samples past its nominal end before
  finalizing - so overdubs land where the material was *heard*, and the
  loop length stays exact. The standalone initialises the value from the
  audio device's reported input+output latency; in a plugin there is no
  host API for device latency, so set it manually there.
- **Host-grid alignment** (plugin): while the host transport runs, the
  engine aligns its bar grid - armed take starts, metronome accents and
  the beat LED - to the host's bar lines (using the playhead's position,
  tempo and time signature). Without a host, or while the transport is
  stopped, the last known grid offset is kept (initially the internal
  grid). The time signature follows the host numerator when available.
- **Anti-plop monitor fades** (internal flag, `LooperEngine::setAntiPlopEnabled`,
  default on, reserved for a future settings dialog): when a channel starts
  recording or overdubbing, its input monitoring fades in over ~5 ms instead
  of switching hard; on stop it fades out again, and loop playback fades in
  likewise, so every state change is a click-free crossfade. The fades apply
  only to the monitoring contribution - recorded buffer content is never
  faded, so attacks of played material stay intact.
- **Clearing all channels frees the loop length**: when every channel is
  empty, the next recording defines a fresh loop length (and a fresh bar
  grid).

## Threading model

- Audio thread: engine processes in-place per sample; commands are delivered
  through a lock-free single-producer/single-consumer FIFO per channel.
- UI thread: reads only atomics (state, mute, undo depth, transport position)
  in a 20 Hz timer and pushes commands. No locks between the threads.
- Known skeleton simplification (documented, fine for a start): the undo
  snapshot copy, buffer growth and truncation allocate on the audio thread.
  A production version should move snapshots to a preallocated pool and use a
  `std::atomic`-swapped read/write buffer scheme.

## Project structure

```
aeLooper/
├── CMakeLists.txt          # builds standalone app + VST3 from one source set
└── src/
    ├── LooperChannel.{h,cpp}   # one channel: buffer, overdub, undo stack, mute
    ├── LooperEngine.{h,cpp}   # 4 channels, global loop length, transport
    ├── MidiController.{h,cpp}  # MIDI note -> command mapping
    ├── PluginProcessor.{h,cpp} # shared juce::AudioProcessor (both targets)
    ├── PluginEditor.{h,cpp}    # VST3 editor (wraps UiPanel)
    ├── UiPanel.{h,cpp}         # shared UI: header + 4 channel strips
    └── Main.cpp                # standalone app entry point
```

## Building (Qt Creator, Ubuntu)

CMake is the primary build system; Projucer is not required (JUCE itself is
CMake-first since JUCE 6, and the Projucer is in maintenance mode). Qt Creator
opens CMake projects natively.

1. Install build dependencies:

   ```bash
   sudo apt install build-essential cmake git \
       libasound2-dev libx11-dev libxext-dev libxrandr-dev \
       libxinerama-dev libxcursor-dev libfreetype6-dev \
       libfontconfig1-dev libglu1-mesa-dev mesa-common-dev
   ```

2. Open `CMakeLists.txt` in Qt Creator (File > Open File or Project),
   choose the Desktop kit, and build.

   - On first configure, JUCE 8.0.8 is downloaded via CMake FetchContent.
     Alternatively, clone JUCE next to `CMakeLists.txt` as `./JUCE` and it
     will be used instead:
     `git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git JUCE`
   - Standalone target: `aeLooper`
   - Plugin target: `aeLooperPlugin` (VST3, copied to `~/.vst3` after build)

3. Command line works too:

   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target aeLooper -j
   cmake --build build --target aeLooperPlugin_VST3 -j
   ```

## Roadmap

- ~~Bar-quantized first take via host tempo (`AudioPlayHead`)~~ - done.
- ~~Latency compensation~~ - done.
- ~~Beat LED / metronome~~ - done.
- Real-time-safe undo snapshots (preallocated buffers, no allocation on the
  audio thread).
- MIDI learn / configurable mapping, per-channel volume, solo, feedback mute.
- Robust host transport sync (cycle/loop mode of the DAW, tempo automation,
  host position re-anchoring after seeks).
- Dynamic channel count (engine already abstracts over the channel list; the
  fixed array in `LooperEngine` is the only place to change).
- Persistence: save/load loops (e.g., per-channel wav, or a single session
  file) - `getStateInformation`/`setStateInformation` are stubs for now.
- A headless engine test exists in `build/engine_test.cpp` (not installed by
  CMake; compile it manually or move it into CTest later).
