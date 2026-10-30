#include "LooperChannel.h"

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

void LooperChannel::processSample(juce::AudioBuffer<float>& io, int i,
                                  int64_t positionInLoop, int64_t loopLength)
{
    const int ioChannels = juce::jmin(io.getNumChannels(), 2);
    const float inL = io.getSample(0, i);
    const float inR = ioChannels > 1 ? io.getSample(1, i) : inL;

    const auto st = state.load(std::memory_order_relaxed);
    const bool mute = muted.load(std::memory_order_relaxed);
    const int bufChannels = juce::jmin(buffer.getNumChannels(), 2);

    float outL = 0.0f, outR = 0.0f;

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

    io.setSample(0, i, outL);
    if (ioChannels > 1)
        io.setSample(1, i, outR);
}

void LooperChannel::conformToLoopLength(int64_t loopLength)
{
    buffer.setSize(2, (int) loopLength, true, true, false);
    recordLength = loopLength;
    setState(LoopState::playing);
}

bool LooperChannel::consumeCommands(int64_t& loopLengthInOut)
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
                            setState(LoopState::recording);
                        }
                        break;

                    case LoopState::recording:
                        // Finishing the base take defines the loop length for
                        // all channels.
                        loopLengthInOut = recordLength;
                        definedLoopLength = true;
                        buffer.setSize(2, (int) recordLength, true, true, false);
                        // Undoing the base take returns to an empty channel.
                        pushUndo(juce::AudioBuffer<float>(), LoopState::empty);
                        setState(LoopState::playing);
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
