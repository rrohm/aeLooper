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
    recording,      ///< recording the first take (defines the loop length)
    overdubbing,    ///< adding audio on top of the existing loop
    playing,        ///< playing back the accumulated loop content
    stopped         ///< has content, but currently silent
};

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
    void pushCommand(Command command) noexcept { commands.push(command); }
    double getContentSeconds(double sampleRate) const noexcept;

    // ---- audio thread (called by LooperEngine) ----
    /** Consume pending commands. Returns true if this channel finished the base take. */
    bool consumeCommands(int64_t& loopLengthInOut);

    /** Grow the take buffer so that a whole incoming block fits. */
    void prepareBlock(int numSamplesInBlock);

    /** Produce this channel's output sample for the given loop position.
        Never touches the shared bus; the engine sums the contributions. */
    void processSample(float inL, float inR, float& outL, float& outR,
                       int64_t positionInLoop, int64_t loopLength);

    /** Resize to the final loop length (called when another channel defined the length). */
    void conformToLoopLength(int64_t loopLength);

private:
    struct UndoEntry
    {
        juce::AudioBuffer<float> content;
        LoopState stateAfterUndo;
    };

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
    int64_t recordLength = 0;                  // samples of the current base take
};
