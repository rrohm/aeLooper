#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

/** High-level state of one loop channel, read by the UI thread. */
enum class LoopState
{
    empty,          ///< no audio recorded yet
    armed,          ///< base take armed: starts at the next bar boundary
    recording,      ///< recording the first take (defines the loop length)
    overdubbing,    ///< adding audio on top of the existing loop
    playing,        ///< playing back the accumulated loop content
    stopped         ///< has content, but currently silent
};

/** Arithmetic modulo that also works for negative values. */
inline int64_t loopMod (int64_t value, int64_t length) noexcept
{
    const auto r = value % length;
    return r >= 0 ? r : r + length;
}

/** Next multiple of `length` strictly after `position` (also for negative
    positions, e.g. on a host-aligned grid). */
inline int64_t nextGridBoundary (int64_t position, int64_t length) noexcept
{
    return position + (length - loopMod (position, length));
}

/**
    One loop channel: an audio buffer with the accumulated loop content, an
    undo stack of previous buffer snapshots, and a mute flag.

    All mutating methods are called on the audio thread (via LooperEngine).
    The UI thread only reads the atomics (state, muted, undoDepth) and pushes
    commands through the lock-free FIFO.
*/
class LooperChannel
{
public:
    enum class Command
    {
        none,
        toggleRecord,   ///< empty -> record | record -> finish take | playing -> overdub | overdub -> stop
        toggleMute,
        undo,
        clear,
        play             ///< stopped -> playing
    };

    LooperChannel(int channelIndex);

    int getIndex() const noexcept { return index; }

    // ---- UI thread access ----
    LoopState getState() const noexcept { return state.load(std::memory_order_relaxed); }
    bool isMuted() const noexcept { return muted.load(std::memory_order_relaxed); }
    int getUndoDepth() const noexcept { return undoDepth.load(std::memory_order_relaxed); }
    /** Peak level of this channel's output contribution (for the meter). */
    float getOutputLevel() const noexcept { return outputLevel.load(std::memory_order_relaxed); }
    void pushCommand(Command command) noexcept { commands.push(command); }
    double getContentSeconds(double sampleRate) const noexcept;

    // ---- audio thread (called by LooperEngine) ----
    /** Consume pending commands. The stop of a base take only arms the
        pendingStopBoundary; the take is finalized by finishQuantizedStop()
        once latency compensation is satisfied. */
    void consumeCommands(int64_t& loopLengthInOut, int64_t barLengthInSamples);

    /** Finalize a stopped base take: recording continues until
        (pendingStopBoundary + latencySamples) so in-flight material lands
        inside the loop. Returns true when it defined the loop length. */
    bool finishQuantizedStop(int64_t& loopLengthInOut, int64_t latencySamples);

    /** Grow the take buffer so that a whole incoming block fits. */
    void prepareBlock(int numSamplesInBlock);

    /** Produce this channel's output sample for the given loop position.
        Never touches the shared bus; the engine sums the contributions.
        latencySamples shifts the write position earlier (compensation). */
    void processSample(float inL, float inR, float& outL, float& outR,
                       int64_t positionInLoop, int64_t loopLength,
                       int64_t latencySamples);

    /** Publish the block peak to the outputLevel meter. */
    void finishBlock();

    /** Audio thread: set the engine beat-clock position at which an armed
        base take shall begin. Only the first call while armed sticks; the
        target must never move (otherwise the take could never start when
        the boundary lands exactly on a block edge). */
    void armAt(int64_t absolutePosition) noexcept
    {
        if (armedStartPos < 0)
            armedStartPos = absolutePosition;
    }

    /** Audio thread: cancel an armed base-take start. */
    void disarm() noexcept
    {
        armedStartPos = -1;
        setState(LoopState::empty);
    }

    /** Audio thread: length of the monitor/playback fade ramp in samples
        (1 = hard switching). */
    void setFadeRampLength(int lengthSamples) noexcept
    { fadeRampLength = juce::jmax(1, lengthSamples); }

    /** Audio thread: begin the armed take once the clock reaches the target. */
    void checkArmedStart(int64_t absolutePosition);

    /** Resize to the final loop length (called when another channel defined the length). */
    void conformToLoopLength(int64_t loopLength);

private:
    struct UndoEntry
    {
        juce::AudioBuffer<float> content;
        LoopState stateAfterUndo;
    };

    /** End the base take: truncate to takeLength, push the "empty" undo
        snapshot (undo removes the base take) and switch to playing. */
    void finalizeTake(int64_t takeLength);

    void pushUndo(const juce::AudioBuffer<float>& content, LoopState stateAfterUndo);
    void setState(LoopState newState) noexcept { state.store(newState, std::memory_order_relaxed); }

    // Minimal single-producer/single-consumer command FIFO.
    class CommandFifo
    {
    public:
        void push(Command c) noexcept
        {
            auto h = head.load(std::memory_order_relaxed);
            if (h - tail.load(std::memory_order_acquire) >= (int) slots.size())
                return; // full: drop the oldest-overflowing command
            slots[(size_t)(h % (int) slots.size())] = c;
            head.store(h + 1, std::memory_order_release);
        }
        bool pop(Command& c) noexcept
        {
            auto t = tail.load(std::memory_order_relaxed);
            if (t == head.load(std::memory_order_acquire))
                return false;
            c = slots[(size_t)(t % (int) slots.size())];
            tail.store(t + 1, std::memory_order_release);
            return true;
        }
    private:
        std::array<Command, 16> slots {};
        std::atomic<int> head { 0 }, tail { 0 };
    };

    static constexpr int maxUndoDepth = 32;

    const int index;
    juce::AudioBuffer<float> buffer;        // accumulated loop content
    std::vector<UndoEntry> undoStack;
    CommandFifo commands;

    std::atomic<LoopState> state { LoopState::empty };
    std::atomic<bool> muted { false };
    std::atomic<int> undoDepth { 0 };
    std::atomic<float> outputLevel { 0.0f };  // block peak for the meter
    float blockPeak = 0.0f;                    // audio-thread only
    int64_t recordLength = 0;                  // samples of the current base take
    int64_t pendingStopBoundary = -1;         ///< quantized stop target (samples), -1 = none
    int64_t armedStartPos = -1;               ///< beat-clock position of an armed take start

    // Anti-plop fades: gains of the channel's output contribution. The
    // input-monitor gain fades in when recording/overdubbing starts (and out
    // on stop), the playback gain fades in when loop playback starts - so
    // state changes are click-free crossfades. Never applied to the recorded
    // buffer content.
    int fadeRampLength = 1;                   // samples for a full 0->1 ramp
    float monitorInGain = 0.0f;
    float bufferGain = 0.0f;
};
