#include "LooperChannel.h"
#include <cmath>

LooperChannel::LooperChannel(int channelIndex)
    : index(channelIndex)
{
    buffer.setSize(2, 0);
}

double LooperChannel::getContentSeconds(double sampleRate) const noexcept
{
    // buffer is only resized on the audio thread; the length read here is an
    // approximate snapshot for display purposes.
    return buffer.getNumSamples() / sampleRate;
}

void LooperChannel::prepareBlock(int numSamplesInBlock)
{
    const auto st = state.load(std::memory_order_relaxed);

    // Armed channels are prepared too: the take can begin mid-block when the
    // bar boundary is crossed, so the buffer must fit the rest of the block.
    if (st == LoopState::recording || st == LoopState::armed)
        buffer.setSize(2, (int) recordLength + numSamplesInBlock,
                        true, false, false);
}

void LooperChannel::processSample(float inL, float inR, float& outL, float& outR,
                                  int64_t positionInLoop, int64_t loopLength,
                                  int64_t latencySamples)
{
    outL = 0.0f;
    outR = 0.0f;

    const auto st = state.load(std::memory_order_relaxed);
    const bool mute = muted.load(std::memory_order_relaxed);
    const int bufChannels = juce::jmin(buffer.getNumChannels(), 2);

    switch (st)
    {
        case LoopState::recording:
        {
            // Append the input to the growing base take, shifted earlier by
            // the latency (the audio was produced that much time before it
            // arrives in the input buffer). Samples whose shifted index is
            // negative predate the take start and are dropped.
            const int64_t writeIndex = recordLength - latencySamples;

            if (writeIndex >= 0)
            {
                buffer.setSample(0, (int) writeIndex, inL);
                if (bufChannels > 1)
                    buffer.setSample(1, (int) writeIndex, inR);
            }
            ++recordLength;
            outL = inL;
            outR = inR;
            break;
        }

        case LoopState::overdubbing:
        {
            const int idx = (int) loopMod(positionInLoop - latencySamples, loopLength);
            if (idx < buffer.getNumSamples())
            {
                const float l = buffer.getSample(0, idx) + inL;
                const float r = bufChannels > 1 ? buffer.getSample(1, idx) + inR : l;
                buffer.setSample(0, idx, juce::jlimit(-1.0f, 1.0f, l));
                if (bufChannels > 1)
                    buffer.setSample(1, idx, juce::jlimit(-1.0f, 1.0f, r));
                outL = l;
                outR = r;
            }
            else
            {
                outL = inL;
                outR = inR;
            }
            break;
        }

        case LoopState::playing:
        {
            const int idx = (int) juce::jlimit((int64_t) 0, loopLength - 1, positionInLoop);
            if (idx < buffer.getNumSamples())
            {
                outL = buffer.getSample(0, idx);
                outR = bufChannels > 1 ? buffer.getSample(1, idx) : outL;
            }
            break;
        }

        case LoopState::stopped:
        case LoopState::armed:
        case LoopState::empty:
            break;
    }

    if (mute)
    {
        outL = 0.0f;
        outR = 0.0f;
    }

    blockPeak = juce::jmax(blockPeak, std::fabs(outL), std::fabs(outR));
}

void LooperChannel::finishBlock()
{
    outputLevel.store(blockPeak, std::memory_order_relaxed);
    blockPeak = 0.0f;
}

void LooperChannel::checkArmedStart(int64_t absolutePosition)
{
    if (state.load(std::memory_order_relaxed) == LoopState::armed
        && armedStartPos >= 0
        && absolutePosition >= armedStartPos)
    {
        // The buffer was already grown by prepareBlock() for this block.
        recordLength = 0;
        pendingStopBoundary = -1;
        setState(LoopState::recording);
    }
}

void LooperChannel::conformToLoopLength(int64_t loopLength)
{
    buffer.setSize(2, (int) loopLength, true, true, false);
    recordLength = loopLength;
    pendingStopBoundary = -1;
    setState(LoopState::playing);
}

void LooperChannel::finalizeTake(int64_t takeLength)
{
    // Truncate (quantized takes may overshoot the boundary by up to one
    // block) and switch to playing.
    buffer.setSize(2, (int) takeLength, true, true, false);
    recordLength = takeLength;
    pendingStopBoundary = -1;

    // Undoing the base take returns to an empty channel.
    pushUndo(juce::AudioBuffer<float>(), LoopState::empty);
    setState(LoopState::playing);
}

bool LooperChannel::finishQuantizedStop(int64_t& loopLengthInOut, int64_t latencySamples)
{
    if (state.load(std::memory_order_relaxed) != LoopState::recording
        || pendingStopBoundary < 0)
        return false;

    // A zero-length take (record/stop pressed immediately) is discarded.
    if (pendingStopBoundary <= 0)
    {
        buffer.setSize(2, 0);
        recordLength = 0;
        pendingStopBoundary = -1;
        setState(LoopState::empty);
        return false;
    }

    // Recording continues until the nominal end plus the latency, so the
    // last in-flight samples still land inside the take.
    if (recordLength < pendingStopBoundary + latencySamples)
        return false;

    const int64_t boundary = pendingStopBoundary;
    finalizeTake(boundary);
    loopLengthInOut = boundary;
    return true;
}

void LooperChannel::consumeCommands(int64_t& loopLengthInOut, int64_t barLengthInSamples)
{
    Command cmd;

    while (commands.pop(cmd))
    {
        switch (cmd)
        {
            case Command::toggleRecord:
                switch (state.load(std::memory_order_relaxed))
                {
                    case LoopState::empty:
                        if (loopLengthInOut > 0)
                        {
                            // Loop already exists: record onto it (overdub).
                            if (buffer.getNumSamples() != (int) loopLengthInOut)
                                buffer.setSize(2, (int) loopLengthInOut, true, true, false);
                            pushUndo(buffer, LoopState::playing);
                            setState(LoopState::overdubbing);
                        }
                        else if (barLengthInSamples > 0)
                        {
                            // Base take with bar quantization: arm the start
                            // at the next bar boundary (engine sets the
                            // absolute target via armAt()).
                            pendingStopBoundary = -1;
                            armedStartPos = -1;
                            setState(LoopState::armed);
                        }
                        else
                        {
                            // First take on this channel: it defines the loop length.
                            undoStack.clear();
                            undoDepth.store(0, std::memory_order_relaxed);
                            buffer.setSize(2, 0);
                            recordLength = 0;
                            pendingStopBoundary = -1;
                            setState(LoopState::recording);
                        }
                        break;

                    case LoopState::armed:
                        // Second press cancels the pending quantized start.
                        disarm();
                        break;

                    case LoopState::recording:
                        // Stop the base take. The nominal end is the next bar
                        // boundary with quantization, or "now" without it;
                        // finishQuantizedStop() finalizes once the latency
                        // extension has been recorded.
                        pendingStopBoundary = barLengthInSamples > 0
                            ? ((recordLength + barLengthInSamples - 1)
                               / barLengthInSamples) * barLengthInSamples
                            : recordLength;
                        break;

                    case LoopState::overdubbing:
                        setState(LoopState::playing);
                        break;

                    case LoopState::playing:
                    case LoopState::stopped:
                        if (loopLengthInOut > 0)
                        {
                            pushUndo(buffer, LoopState::playing);
                            setState(LoopState::overdubbing);
                        }
                        break;
                }
                break;

            case Command::toggleMute:
                muted.store(! muted.load(std::memory_order_relaxed), std::memory_order_relaxed);
                break;

            case Command::undo:
                if (! undoStack.empty())
                {
                    auto& entry = undoStack.back();
                    buffer = entry.content;
                    setState(entry.stateAfterUndo);
                    if (state.load(std::memory_order_relaxed) == LoopState::empty)
                    {
                        recordLength = 0;
                        buffer.setSize(2, 0);
                    }
                    undoStack.pop_back();
                    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
                }
                break;

            case Command::clear:
                undoStack.clear();
                undoDepth.store(0, std::memory_order_relaxed);
                buffer.setSize(2, 0);
                recordLength = 0;
                pendingStopBoundary = -1;
                armedStartPos = -1;
                setState(LoopState::empty);
                break;

            case Command::play:
                if (state.load(std::memory_order_relaxed) == LoopState::stopped)
                    setState(LoopState::playing);
                break;

            case Command::none:
                break;
        }
    }

    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
}

void LooperChannel::pushUndo(const juce::AudioBuffer<float>& content, LoopState stateAfterUndo)
{
    if ((int) undoStack.size() >= maxUndoDepth)
        undoStack.erase(undoStack.begin()); // bounded memory, oldest entry dropped

    undoStack.push_back({ content, stateAfterUndo });
    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
}
