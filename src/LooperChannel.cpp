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
    if (state.load(std::memory_order_relaxed) == LoopState::recording)
        buffer.setSize(2, (int) recordLength + numSamplesInBlock,
                        true, false, false);
}

void LooperChannel::processSample(float inL, float inR, float& outL, float& outR,
                                  int64_t positionInLoop, int64_t loopLength)
{
    outL = 0.0f;
    outR = 0.0f;

    const auto st = state.load(std::memory_order_relaxed);
    const bool mute = muted.load(std::memory_order_relaxed);
    const int bufChannels = juce::jmin(buffer.getNumChannels(), 2);

    switch (st)
    {
        case LoopState::recording:
            // Append the input to the growing base take and monitor it.
            buffer.setSample(0, (int) recordLength, inL);
            if (bufChannels > 1)
                buffer.setSample(1, (int) recordLength, inR);
            ++recordLength;
            outL = inL;
            outR = inR;
            break;

        case LoopState::overdubbing:
        {
            const int idx = (int) juce::jlimit((int64_t) 0, loopLength - 1, positionInLoop);
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

bool LooperChannel::finishQuantizedStop(int64_t& loopLengthInOut)
{
    if (state.load(std::memory_order_relaxed) != LoopState::recording
        || pendingStopBoundary <= 0
        || recordLength < pendingStopBoundary)
        return false;

    const int64_t boundary = pendingStopBoundary;
    finalizeTake(boundary);
    loopLengthInOut = boundary;
    return true;
}

bool LooperChannel::consumeCommands(int64_t& loopLengthInOut, int64_t barLengthInSamples)
{
    bool definedLoopLength = false;
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

                    case LoopState::recording:
                        if (barLengthInSamples > 0)
                        {
                            // Quantized stop: keep recording (and monitoring)
                            // until the next bar boundary.
                            pendingStopBoundary = ((recordLength + barLengthInSamples - 1)
                                                   / barLengthInSamples) * barLengthInSamples;
                        }
                        else
                        {
                            finalizeTake(recordLength);
                            loopLengthInOut = recordLength;
                            definedLoopLength = true;
                        }
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
    return definedLoopLength;
}

void LooperChannel::pushUndo(const juce::AudioBuffer<float>& content, LoopState stateAfterUndo)
{
    if ((int) undoStack.size() >= maxUndoDepth)
        undoStack.erase(undoStack.begin()); // bounded memory, oldest entry dropped

    undoStack.push_back({ content, stateAfterUndo });
    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
}
