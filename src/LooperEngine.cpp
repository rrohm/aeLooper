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

#include "LooperEngine.h"
#include <cmath>

void LooperEngine::renderClick(juce::AudioBuffer<float>& click, double sr,
                               double frequency, float level)
{
    // A short sine burst with exponential decay: the classic electronic
    // metronome tick. A ~1 ms cosine onset ramp avoids a DC pop.
    const int length = juce::jmax (1, (int) (0.05 * sr));   // 50 ms
    click.setSize (2, length, false, true, true);

    for (int i = 0; i < length; ++i)
    {
        const double t = (double) i / sr;
        const double envelope = std::exp (-t * 60.0);
        const double onset = juce::jlimit (0.0, 1.0, t / 0.001);
        const double sample = std::sin (juce::MathConstants<double>::twoPi
                                            * frequency * t)
                                  * envelope * onset * level;
        click.setSample (0, i, (float) sample);
        click.setSample (1, i, (float) sample);
    }
}

void LooperEngine::prepare(double newSampleRate)
{
    sampleRate.store(newSampleRate, std::memory_order_relaxed);

    // Re-render the metronome clicks for the new sample rate.
    renderClick (clickBeat,   newSampleRate, 1500.0, 0.3f);
    renderClick (clickAccent, newSampleRate, 2000.0, 0.4f);
    clickOffset = -1;
}

void LooperEngine::pushCommand(int channelIndex, LooperChannel::Command command)
{
    if (channelIndex >= 0 && channelIndex < maxChannels)
        channels[(size_t) channelIndex].pushCommand(command);
}

int64_t LooperEngine::currentBeatLength() const noexcept
{
    const auto bpm = getEffectiveBpm();
    const auto sr = sampleRate.load(std::memory_order_relaxed);
    return bpm > 0.0 ? (int64_t) ((60.0 / bpm) * sr) : 0;
}

int64_t LooperEngine::currentBarLength() const noexcept
{
    if (! quantizeEnabled.load(std::memory_order_relaxed))
        return 0;

    const int numerator = hostGridValid.load(std::memory_order_relaxed)
        ? hostNumerator.load(std::memory_order_relaxed)
        : (int) beatsPerBar;
    return currentBeatLength() * numerator;
}

void LooperEngine::updateHostGrid(int64_t hostTimeInSamples, int numerator,
                                  bool transportRunning)
{
    hostTimeAtBlock.store(hostTimeInSamples, std::memory_order_relaxed);
    hostNumerator.store(juce::jmax(1, numerator), std::memory_order_relaxed);
    hostGridValid.store(true, std::memory_order_relaxed);
    hostTransportRunning.store(transportRunning, std::memory_order_relaxed);
}

void LooperEngine::clearHostGrid()
{
    hostGridValid.store(false, std::memory_order_relaxed);
    hostTransportRunning.store(false, std::memory_order_relaxed);
    gridOffset.store(0, std::memory_order_relaxed);   // back to the internal grid
}

void LooperEngine::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    // 1. Consume pending commands. A channel finishing its base take
    //    defines the loop length for the whole engine (len changes from
    //    -1 to the take length). With bar quantization the stop is only
    //    armed here; the take ends when the bar boundary is reached.
    const int64_t barLen = currentBarLength();
    int64_t len = loopLength.load(std::memory_order_relaxed);
    const int64_t lenBefore = len;

    for (auto& channel : channels)
        channel.consumeCommands(len, barLen);

    // Armed base takes: set the absolute start position on the beat clock.
    // With quantization on, that is the next bar boundary of the *aligned*
    // grid (host bars while the transport runs, internal grid otherwise),
    // so the take begins sample-accurately on the LED's accented beat.
    const int64_t beatAtBlockStart = beatClock.load(std::memory_order_relaxed);

    int64_t effectiveGridOffset;
    if (hostGridValid.load(std::memory_order_relaxed)
        && hostTransportRunning.load(std::memory_order_relaxed))
    {
        effectiveGridOffset = hostTimeAtBlock.load(std::memory_order_relaxed) - beatAtBlockStart;
        gridOffset.store(effectiveGridOffset, std::memory_order_relaxed);
    }
    else
    {
        effectiveGridOffset = gridOffset.load(std::memory_order_relaxed);
    }

    for (auto& channel : channels)
    {
        if (channel.getState() != LoopState::armed)
            continue;

        if (len > 0)
        {
            // A loop already exists after all: no base take, cancel.
            channel.disarm();
        }
        else if (barLen > 0)
        {
            const int64_t alignedNow = beatAtBlockStart + effectiveGridOffset;
            channel.armAt(nextGridBoundary(alignedNow, barLen) - effectiveGridOffset);
        }
        else
        {
            channel.armAt(beatAtBlockStart); // quantize off -> start now
        }
    }

    // Finalize stopped base takes (bar-quantized and free alike), once the
    // latency extension has been recorded.
    const int64_t latency = latencySamples.load(std::memory_order_relaxed);

    for (auto& channel : channels)
        channel.finishQuantizedStop(len, latency);

    // All channels empty -> the session is fresh again: free the loop
    // length so that the next recording defines a new one.
    if (len > 0)
    {
        bool allEmpty = true;
        for (auto& channel : channels)
            if (channel.getState() != LoopState::empty)
            {
                allEmpty = false;
                break;
            }

        if (allEmpty)
            len = -1;
    }

    if (len != lenBefore)
    {
        loopLength.store(len, std::memory_order_relaxed);
        position.store(0, std::memory_order_relaxed);

        if (len > 0)
            savedLoopLength = len;

        // Every channel that had started recording a base take of its own
        // gets conformed (truncated / padded) to the new length; empty
        // channels stay empty and are resized lazily when they overdub.
        for (auto& channel : channels)
            if (channel.getState() == LoopState::recording)
                channel.conformToLoopLength(len);
    }

    // Undo/redo can bring a channel back to playing/overdubbing after the
    // loop length was freed (all channels empty): restore the saved length,
    // otherwise playback would run without a loop length.
    if (len <= 0 && savedLoopLength > 0)
    {
        for (auto& channel : channels)
        {
            const auto st = channel.getState();
            if (st == LoopState::playing || st == LoopState::overdubbing)
            {
                len = savedLoopLength;
                loopLength.store(len, std::memory_order_relaxed);
                position.store(0, std::memory_order_relaxed);
                break;
            }
        }
    }

    // 2. Let channels that are recording a base take grow their buffers so
    //    the incoming block fits without per-sample reallocation. Also push
    //    the monitor fade ramp length (1 = hard switching when the
    //    anti-plop feature is disabled).
    const int fadeRamp = antiPlopEnabled.load(std::memory_order_relaxed)
        ? juce::jmax(1, (int) (0.005 * sampleRate.load(std::memory_order_relaxed)))
        : 1;

    for (auto& channel : channels)
    {
        channel.prepareBlock(numSamples);
        channel.setFadeRampLength(fadeRamp);
    }

    // 3. Transport and per-sample processing. The input is read once, every
    //    channel produces a contribution, and the sum is written back. The
    //    summed output is clamped so multiple active channels cannot exceed
    //    full scale.
    const bool lengthKnown = len > 0;
    const int bufferChannels = juce::jmin(buffer.getNumChannels(), 2);
    const bool metronomeMaster = metronomeEnabled.load(std::memory_order_relaxed);
    const bool countInOnly = metronomeCountInOnly.load(std::memory_order_relaxed);
    const int64_t beatLen = currentBeatLength();
    const int numerator = hostGridValid.load(std::memory_order_relaxed)
        ? hostNumerator.load(std::memory_order_relaxed)
        : (int) beatsPerBar;
    const int64_t barLenSamples = beatLen * numerator;
    int64_t pos = position.load(std::memory_order_relaxed);
    int64_t beat = beatAtBlockStart;
    float inputPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t posInLoop = lengthKnown ? (pos % len) : pos;

        const float inL = buffer.getSample(0, i);
        const float inR = bufferChannels > 1 ? buffer.getSample(1, i) : inL;
        inputPeak = juce::jmax(inputPeak, std::fabs(inL), std::fabs(inR));

        float outL = 0.0f;
        float outR = 0.0f;

        for (auto& channel : channels)
        {
            float l = 0.0f, r = 0.0f;
            channel.checkArmedStart(beat);
            channel.processSample(inL, inR, l, r, posInLoop, len, latency);
            outL += l;
            outR += r;
        }

        // Metronome: trigger a click at each beat of the aligned grid,
        // accented on the downbeat. The click state persists across blocks,
        // so the tick is seamless at any block size.
        // Count-in mode is evaluated per sample: an armed take can flip to
        // recording mid-block, and the click must stop exactly at that bar.
        bool metronomeActive = metronomeMaster;

        if (metronomeActive && countInOnly)
        {
            bool anyRecording = false;
            for (auto& channel : channels)
                if (channel.getState() == LoopState::recording)
                {
                    anyRecording = true;
                    break;
                }

            if (lengthKnown || anyRecording)
                metronomeActive = false;
        }

        const int64_t alignedBeat = beat + effectiveGridOffset;

        if (metronomeActive && beatLen > 0 && loopMod(alignedBeat, beatLen) == 0)
        {
            clickOffset = 0;
            clickIsAccent = barLenSamples > 0 && loopMod(alignedBeat, barLenSamples) == 0;
        }

        if (clickOffset >= 0)
        {
            const auto& click = clickIsAccent ? clickAccent : clickBeat;

            if (clickOffset < click.getNumSamples())
            {
                outL += click.getSample(0, (int) clickOffset);
                outR += click.getSample(1, (int) clickOffset);
            }

            if (++clickOffset >= click.getNumSamples())
                clickOffset = -1;
        }

        buffer.setSample(0, i, juce::jlimit(-1.0f, 1.0f, outL));
        if (bufferChannels > 1)
            buffer.setSample(1, i, juce::jlimit(-1.0f, 1.0f, outR));
        ++pos;
        ++beat;
    }

    beatClock.store(beat, std::memory_order_relaxed);
    inputLevel.store(inputPeak, std::memory_order_relaxed);
    for (auto& channel : channels)
        channel.finishBlock();

    position.store(pos, std::memory_order_relaxed);
}

double LooperEngine::getLoopLengthSeconds() const noexcept
{
    const auto len = loopLength.load(std::memory_order_relaxed);
    const auto sr = sampleRate.load(std::memory_order_relaxed);
    return len > 0 ? (double) len / sr : 0.0;
}

double LooperEngine::getPositionSeconds() const noexcept
{
    const auto len = loopLength.load(std::memory_order_relaxed);
    const auto pos = position.load(std::memory_order_relaxed);
    const auto sr = sampleRate.load(std::memory_order_relaxed);
    if (len <= 0)
        return 0.0;
    return (double) (pos % len) / sr;
}
