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

    std::array<LooperChannel, maxChannels> channels { { { 0 }, { 1 }, { 2 }, { 3 } } };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int64_t> loopLength { -1 };   ///< -1 until the first take is finished
    std::atomic<int64_t> position { 0 };      ///< global transport position in samples
    std::atomic<float> inputLevel { 0.0f };   ///< block peak of the input
    std::atomic<bool> quantizeEnabled { false };
    std::atomic<double> userBpm { 120.0 };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostTempoValid { false };
    std::atomic<int64_t> beatClock { 0 };     ///< monotonic, never resets
};
