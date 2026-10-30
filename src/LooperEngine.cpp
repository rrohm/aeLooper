#include "LooperEngine.h"

void LooperEngine::prepare(double newSampleRate)
{
    sampleRate.store(newSampleRate, std::memory_order_relaxed);
}

void LooperEngine::pushCommand(int channelIndex, LooperChannel::Command command)
{
    if (channelIndex >= 0 && channelIndex < maxChannels)
        channels[(size_t) channelIndex].pushCommand(command);
}

void LooperEngine::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    // 1. Consume pending commands. A channel finishing its base take
    //    defines the loop length for the whole engine (len changes from
    //    -1 to the take length).
    int64_t len = loopLength.load(std::memory_order_relaxed);
    const int64_t lenBefore = len;
    bool lengthJustDefined = false;

    for (auto& channel : channels)
        if (channel.consumeCommands(len))
            lengthJustDefined = true;

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

    // 3. Transport and per-sample processing.
    const bool lengthKnown = len > 0;
    int64_t pos = position.load(std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        const int64_t posInLoop = lengthKnown ? (pos % len) : pos;
        for (auto& channel : channels)
            channel.processSample(buffer, i, posInLoop, len);
        ++pos;
    }

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
