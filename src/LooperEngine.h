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

    void prepare(double newSampleRate);
    void processBlock(juce::AudioBuffer<float>& buffer);

    void pushCommand(int channelIndex, LooperChannel::Command command);

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
    std::array<LooperChannel, maxChannels> channels { { { 0 }, { 1 }, { 2 }, { 3 } } };
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int64_t> loopLength { -1 };   ///< -1 until the first take is finished
    std::atomic<int64_t> position { 0 };      ///< global transport position in samples
    std::atomic<float> inputLevel { 0.0f };   ///< block peak of the input
};
