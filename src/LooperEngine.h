#pragma once

#include "LooperChannel.h"
#include <array>

/**
    Owns the four loop channels, the global loop length (defined by the first
    finished take on any channel) and the global playback position.

    First take defines the length for all channels:
      - While no loop length is known, a channel in `recording` state appends
        input to a growing buffer.
      - The moment that take is finished, its length becomes the global loop
        length, the transport wraps around it, and every other channel is
        conformed to that length (padded with silence / truncated).

    All processing happens on the audio thread. The UI thread reads the
    atomics and pushes commands into the per-channel FIFOs.
*/
class LooperEngine
{
public:
    static constexpr int maxChannels = 4;
    static constexpr double beatsPerBar = 4.0;  ///< 4/4 for now

    void prepare(double newSampleRate);
    void processBlock(juce::AudioBuffer<float>& buffer);

    void pushCommand(int channelIndex, LooperChannel::Command command);

    // ---- UI thread: bar quantization settings ----
    /** When enabled, stopping the first take is quantized to the next bar
        boundary (4/4 at the effective tempo). The loop length is then an
        exact multiple of one bar. */
    void setQuantizeEnabled(bool shouldQuantize) noexcept
    { quantizeEnabled.store(shouldQuantize, std::memory_order_relaxed); }
    bool isQuantizeEnabled() const noexcept
    { return quantizeEnabled.load(std::memory_order_relaxed); }

    /** Fallback tempo when no host provides one. */
    void setBpm(double bpm) noexcept { userBpm.store(bpm, std::memory_order_relaxed); }
    double getBpm() const noexcept
    { return userBpm.load(std::memory_order_relaxed); }

    /** Metronome click mixed into the output at each beat of the clock. */
    void setMetronomeEnabled(bool enabled) noexcept
    { metronomeEnabled.store(enabled, std::memory_order_relaxed); }
    bool isMetronomeEnabled() const noexcept
    { return metronomeEnabled.load(std::memory_order_relaxed); }

    /**
        Count-in mode: the metronome clicks only while no loop exists and no
        take is running (i.e. before the first recording, including the armed
        phase), and starts again once the last loop is cleared. When false,
        the metronome runs permanently while enabled.
    */
    void setMetronomeCountInOnly(bool countInOnly) noexcept
    { metronomeCountInOnly.store(countInOnly, std::memory_order_relaxed); }
    bool isMetronomeCountInOnly() const noexcept
    { return metronomeCountInOnly.load(std::memory_order_relaxed); }

    /**
        Latency compensation: recording writes are shifted this many samples
        earlier, and a stopped base take keeps recording this many samples
        past its nominal end. Set it to the round-trip latency
        (input + output) of the audio system; the standalone initialises it
        from the audio device, the plugin exposes a manual slider.
    */
    void setLatency(int64_t samples) noexcept
    {
        latencySamples.store(juce::jlimit((int64_t) 0, (int64_t) 65536, samples),
                             std::memory_order_relaxed);
    }
    int64_t getLatency() const noexcept
    { return latencySamples.load(std::memory_order_relaxed); }

    /**
        Anti-plop monitor fades: when enabled (default), a channel's input
        monitoring fades in over ~5 ms when recording/overdubbing starts and
        out on stop, and loop playback fades in likewise, so state changes
        are click-free crossfades. Recorded buffer content is never faded.
        Internal flag - reserved for a future settings dialog.
    */
    void setAntiPlopEnabled(bool enabled) noexcept
    { antiPlopEnabled.store(enabled, std::memory_order_relaxed); }
    bool isAntiPlopEnabled() const noexcept
    { return antiPlopEnabled.load(std::memory_order_relaxed); }

    // ---- host grid (audio thread, called by PluginProcessor) ----
    /**
        Feed the host transport position (in host samples, valid while the
        transport is running) and its time signature numerator. The engine
        aligns its bar grid (armed take starts, metronome accents, beat LED)
        to the host's bars while the transport runs; without a host, or when
        stopped, the last known offset is kept (initially 0 = internal grid).
    */
    void updateHostGrid(int64_t hostTimeInSamples, int numerator, bool transportRunning);
    /** No playhead information available at all (e.g. no host). */
    void clearHostGrid();

    /** Offset (host samples - engine samples) of the aligned beat grid.
        Used by the UI for the beat LED phase. */
    int64_t getGridOffset() const noexcept
    { return gridOffset.load(std::memory_order_relaxed); }

    // ---- audio thread (via PluginProcessor): host tempo ----
    void setHostTempo(double bpm) noexcept
    {
        hostBpm.store(bpm, std::memory_order_relaxed);
        hostTempoValid.store(true, std::memory_order_relaxed);
    }
    void clearHostTempo() noexcept
    { hostTempoValid.store(false, std::memory_order_relaxed); }
    bool hasHostTempo() const noexcept
    { return hostTempoValid.load(std::memory_order_relaxed); }
    double getHostBpm() const noexcept
    { return hostBpm.load(std::memory_order_relaxed); }

    /** Host tempo if available, otherwise the user-set BPM. */
    double getEffectiveBpm() const noexcept
    {
        return hasHostTempo() ? getHostBpm() : getBpm();
    }

    /**
        Monotonic beat clock in samples, independent of the loop transport
        (it never resets). The beat LED derives its phase from it:
        beat flash when clock % beatLength == 0, bar accent when
        clock % barLength == 0. With bar quantization enabled, armed base
        takes start exactly on the next bar boundary of this clock, so the
        LED and the loop grid always agree.
    */
    int64_t getBeatClock() const noexcept
    { return beatClock.load(std::memory_order_relaxed); }

    // ---- UI thread access ----
    int getNumChannels() const noexcept { return maxChannels; }
    LooperChannel& getChannel(int i) { return channels[(size_t) i]; }

    bool isLoopLengthKnown() const noexcept
    { return loopLength.load(std::memory_order_relaxed) > 0; }

    /** Peak level of the incoming signal (for the input meter). */
    float getInputLevel() const noexcept
    { return inputLevel.load(std::memory_order_relaxed); }

    double getLoopLengthSeconds() const noexcept;
    double getPositionSeconds() const noexcept;
    double getSampleRate() const noexcept { return sampleRate.load(std::memory_order_relaxed); }

private:
    /** Length of one 4/4 bar in samples, or 0 when quantization is off. */
    int64_t currentBarLength() const noexcept;

    /** Length of one beat in samples (always tempo-derived, >= 0). */
    int64_t currentBeatLength() const noexcept;

    /** Render one metronome click (sine burst with exponential decay). */
    static void renderClick(juce::AudioBuffer<float>& click, double sampleRate,
                            double frequency, float level);

    std::array<LooperChannel, maxChannels> channels { { { 0 }, { 1 }, { 2 }, { 3 } } };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int64_t> loopLength { -1 };   ///< -1 until the first take is finished
    std::atomic<int64_t> position { 0 };      ///< global transport position in samples
    std::atomic<float> inputLevel { 0.0f };   ///< block peak of the input
    std::atomic<bool> quantizeEnabled { false };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<bool> metronomeCountInOnly { false };
    std::atomic<bool> antiPlopEnabled { true };
    std::atomic<int64_t> latencySamples { 0 };
    std::atomic<double> userBpm { 120.0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostTempoValid { false };
    std::atomic<int64_t> beatClock { 0 };     ///< monotonic, never resets

    std::atomic<int64_t> hostTimeAtBlock { 0 };
    std::atomic<int> hostNumerator { 4 };
    std::atomic<bool> hostGridValid { false };
    std::atomic<bool> hostTransportRunning { false };
    std::atomic<int64_t> gridOffset { 0 };   ///< last host-engine grid offset

    juce::AudioBuffer<float> clickBeat;      ///< regular beat click
    juce::AudioBuffer<float> clickAccent;    ///< downbeat click
    int64_t clickOffset = -1;                ///< playhead into the active click, -1 = none
    bool clickIsAccent = false;
};
