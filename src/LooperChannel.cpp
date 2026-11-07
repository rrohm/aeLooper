// aeLooper - A multichannel audio looper for Linux, built with JUCE.
// 
// Copyright (C) 2026 Robert Rohm
// 
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your option) 
// any later version. This program is distributed in the hope that it will be useful, 
// but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY 
// or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more 
// details. 
// 
// You should have received a copy of the GNU General Public License along with 
// this program. If not, see <http://www.gnu.org/licenses/>.

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

namespace
{
/** Move `current` toward `target` by at most `step` (per-sample fade). */
float approachGain (float current, float target, float step) noexcept
{
    if (current < target)
        return juce::jmin (target, current + step);
    return juce::jmax (target, current - step);
}
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

    // Anti-plop fades: the input-monitor component fades in while
    // recording/overdubbing, the playback component while playing/overdubbing.
    // With a ramp length of 1 (feature disabled) this is a hard switch.
    const float step = 1.0f / (float) fadeRampLength;
    monitorInGain = approachGain(monitorInGain,
                                 (st == LoopState::recording || st == LoopState::overdubbing) ? 1.0f : 0.0f,
                                 step);
    bufferGain = approachGain(bufferGain,
                              (st == LoopState::playing || st == LoopState::overdubbing) ? 1.0f : 0.0f,
                              step);

    float bufCompL = 0.0f, bufCompR = 0.0f;   // loop-playback component
    float inCompL = 0.0f, inCompR = 0.0f;    // input-monitor component

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
            inCompL = inL;
            inCompR = inR;
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
                bufCompL = l;
                bufCompR = r;
            }
            else
            {
                bufCompL = inL;
                bufCompR = inR;
            }
            inCompL = inL;
            inCompR = inR;
            break;
        }

        case LoopState::playing:
        {
            const int idx = (int) juce::jlimit((int64_t) 0, loopLength - 1, positionInLoop);
            if (idx < buffer.getNumSamples())
            {
                bufCompL = buffer.getSample(0, idx);
                bufCompR = bufChannels > 1 ? buffer.getSample(1, idx) : bufCompL;
            }
            break;
        }

        case LoopState::stopped:
        case LoopState::armed:
        case LoopState::empty:
            break;
    }

    outL = bufCompL * bufferGain + inCompL * monitorInGain;
    outR = bufCompR * bufferGain + inCompR * monitorInGain;

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

    updateThumbnailIfNeeded();
}

void LooperChannel::updateThumbnailIfNeeded()
{
    const auto st = state.load(std::memory_order_relaxed);
    const bool mutating = st == LoopState::recording || st == LoopState::overdubbing;

    if (mutating)
        ++displayVersion;

    ++blockCounter;

    // While mutating, recompute at most every 8th block; on state changes
    // (undo/redo/finalize/clear/...) recompute immediately.
    if (displayVersion != computedVersion && (! mutating || blockCounter % 8 == 0))
    {
        computedVersion = displayVersion;
        computeDisplayThumb();
    }
}

void LooperChannel::computeThumb(const juce::AudioBuffer<float>& source, WaveformThumb& thumb)
{
    constexpr int cols = WaveformThumb::columns;
    const int n = source.getNumSamples();

    thumb.validColumns = 0;

    if (n <= 0 || source.getNumChannels() <= 0)
        return;

    const int numCh = juce::jmin(source.getNumChannels(), 2);

    for (int c = 0; c < cols; ++c)
    {
        const int start = (int) ((int64_t) c * n / cols);
        const int end = (int) ((int64_t) (c + 1) * n / cols);
        const int region = juce::jmax(1, end - start);
        // Read at most ~32 samples per column: bounded cost even for very
        // long loops (<= cols * 32 reads total).
        const int step = juce::jmax(1, region / 32);

        float mn = 0.0f, mx = 0.0f;
        bool first = true;

        for (int i = start; i < end; i += step)
        {
            float v = source.getSample(0, i);
            if (numCh > 1)
                v = (v + source.getSample(1, i)) * 0.5f;

            if (first) { mn = mx = v; first = false; }
            else { mn = juce::jmin(mn, v); mx = juce::jmax(mx, v); }
        }

        thumb.minV[(size_t) c] = mn;
        thumb.maxV[(size_t) c] = mx;
    }

    thumb.validColumns = cols;
}

void LooperChannel::computeDisplayThumb()
{
    // Whenever something is redoable, show the undone layer's waveform (the
    // UI dims it), so the user sees per channel that content can be restored.
    const bool showRedo = ! redoStack.empty();
    const auto& source = showRedo ? redoStack.back().content : buffer;

    const int inactive = 1 - thumbActive.load(std::memory_order_relaxed);
    computeThumb(source, thumbs[(size_t) inactive]);
    thumbActive.store(inactive, std::memory_order_release);
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
    ++displayVersion;
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
    ++displayVersion;
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
            {
                const auto st = state.load(std::memory_order_relaxed);

                // Undo only makes sense with content present and not while a
                // take is being recorded.
                if ((st == LoopState::playing || st == LoopState::overdubbing
                     || st == LoopState::stopped)
                    && ! undoStack.empty())
                {
                    pushRedoSnapshot();
                    applySnapshot(undoStack.back());
                    undoStack.pop_back();
                    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
                }
                break;
            }

            case Command::redo:
            {
                const auto st = state.load(std::memory_order_relaxed);

                // Redo also works from empty (redoing the base take after
                // undoing it), but not while recording.
                if ((st == LoopState::playing || st == LoopState::overdubbing
                     || st == LoopState::stopped || st == LoopState::empty)
                    && ! redoStack.empty())
                {
                    pushCurrentOntoUndo();
                    applySnapshot(redoStack.back());
                    redoStack.pop_back();
                    redoDepth.store((int) redoStack.size(), std::memory_order_relaxed);
                }
                break;
            }

            case Command::clear:
                undoStack.clear();
                redoStack.clear();
                undoDepth.store(0, std::memory_order_relaxed);
                redoDepth.store(0, std::memory_order_relaxed);
                buffer.setSize(2, 0);
                recordLength = 0;
                pendingStopBoundary = -1;
                armedStartPos = -1;
                setState(LoopState::empty);
                ++displayVersion;
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

void LooperChannel::applySnapshot(const UndoEntry& entry)
{
    buffer = entry.content;
    setState(entry.stateAfterUndo);

    if (state.load(std::memory_order_relaxed) == LoopState::empty)
    {
        recordLength = 0;
        buffer.setSize(2, 0);
    }

    ++displayVersion;
}

void LooperChannel::pushRedoSnapshot()
{
    if ((int) redoStack.size() >= maxUndoDepth)
        redoStack.erase(redoStack.begin());

    redoStack.push_back({ buffer, state.load(std::memory_order_relaxed) });
    redoDepth.store((int) redoStack.size(), std::memory_order_relaxed);
}

void LooperChannel::pushCurrentOntoUndo()
{
    if ((int) undoStack.size() >= maxUndoDepth)
        undoStack.erase(undoStack.begin());

    undoStack.push_back({ buffer, state.load(std::memory_order_relaxed) });
    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
}

void LooperChannel::pushUndo(const juce::AudioBuffer<float>& content, LoopState stateAfterUndo)
{
    // A new layer invalidates the redo stack: undone content is forgotten.
    redoStack.clear();
    redoDepth.store(0, std::memory_order_relaxed);
    ++displayVersion;   // the displayed waveform may switch back to the current buffer

    if ((int) undoStack.size() >= maxUndoDepth)
        undoStack.erase(undoStack.begin()); // bounded memory, oldest entry dropped

    undoStack.push_back({ content, stateAfterUndo });
    undoDepth.store((int) undoStack.size(), std::memory_order_relaxed);
}
