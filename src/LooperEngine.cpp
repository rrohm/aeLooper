#include "LooperEngine.h"
#include <cmath>

void LooperEngine::prepare(double newSampleRate)
{
    sampleRate.store(newSampleRate, std::memory_order_relaxed);
}

void LooperEngine::pushCommand(int channelIndex, LooperChannel::Command command)
{
    if (channelIndex >= 0 && channelIndex < maxChannels)
        channels[(size_t) channelIndex].pushCommand(command);
}

int64_t LooperEngine::currentBarLength() const noexcept
{
    if (! quantizeEnabled.load(std::memory_order_relaxed))
        return 0;

    const auto bpm = getEffectiveBpm();
    const auto sr = sampleRate.load(std::memory_order_relaxed);
    return (int64_t) ((60.0 / bpm) * beatsPerBar * sr);
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

    if (barLen > 0)
        for (auto& channel : channels)
            channel.finishQuantizedStop(len);

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

        // Every channel that had started recording a base take of its own
        // gets conformed (truncated / padded) to the new length; empty
        // channels stay empty and are resized lazily when they overdub.
        for (auto& channel : channels)
            if (channel.getState() == LoopState::recording)
                channel.conformToLoopLength(len);
    }

    // 2. Let channels that are recording a base take grow their buffers so
    //    the incoming block fits without per-sample reallocation.
    for (auto& channel : channels)
        channel.prepareBlock(numSamples);

    // 3. Transport and per-sample processing. The input is read once, every
    //    channel produces a contribution, and the sum is written back. The
    //    summed output is clamped so multiple active channels cannot exceed
    //    full scale.
    const bool lengthKnown = len > 0;
    const int bufferChannels = juce::jmin(buffer.getNumChannels(), 2);
    int64_t pos = position.load(std::memory_order_relaxed);
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
            channel.processSample(inL, inR, l, r, posInLoop, len);
            outL += l;
            outR += r;
        }

        buffer.setSample(0, i, juce::jlimit(-1.0f, 1.0f, outL));
        if (bufferChannels > 1)
            buffer.setSample(1, i, juce::jlimit(-1.0f, 1.0f, outR));
        ++pos;
    }

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
