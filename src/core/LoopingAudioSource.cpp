#include "LoopingAudioSource.h"

LoopingAudioSource::LoopingAudioSource(juce::AudioFormatReader* reader)
    : readerSource(reader != nullptr ? std::make_unique<juce::AudioFormatReaderSource>(reader, true) : nullptr)
{
}

void LoopingAudioSource::setLooping(bool shouldLoop) noexcept
{
    looping.store(shouldLoop, std::memory_order_relaxed);
}

bool LoopingAudioSource::isLooping() const noexcept
{
    return looping.load(std::memory_order_relaxed);
}

void LoopingAudioSource::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    if (readerSource != nullptr)
        readerSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void LoopingAudioSource::releaseResources()
{
    if (readerSource != nullptr)
        readerSource->releaseResources();
}

void LoopingAudioSource::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    if (readerSource == nullptr || info.buffer == nullptr || info.numSamples <= 0)
    {
        if (info.buffer != nullptr)
            info.clearActiveBufferRegion();
        return;
    }

    const auto totalLength = readerSource->getTotalLength();
    if (totalLength <= 0)
    {
        info.clearActiveBufferRegion();
        return;
    }

    int destinationOffset = 0;
    while (destinationOffset < info.numSamples)
    {
        const bool shouldLoop = looping.load(std::memory_order_relaxed);
        auto readPosition = readerSource->getNextReadPosition();
        if (readPosition >= totalLength)
        {
            if (!shouldLoop)
            {
                const auto remaining = info.numSamples - destinationOffset;
                readerSource->getNextAudioBlock({info.buffer, info.startSample + destinationOffset, remaining});
                return;
            }

            readerSource->setNextReadPosition(0);
            readPosition = 0;
        }

        const auto available = totalLength - readPosition;
        const int chunkSize = shouldLoop
            ? static_cast<int>(juce::jmin<juce::int64>(info.numSamples - destinationOffset, available))
            : info.numSamples - destinationOffset;
        if (chunkSize <= 0)
        {
            info.buffer->clear(info.startSample + destinationOffset, info.numSamples - destinationOffset);
            return;
        }

        readerSource->getNextAudioBlock({info.buffer, info.startSample + destinationOffset, chunkSize});
        destinationOffset += chunkSize;
    }
}

void LoopingAudioSource::setNextReadPosition(juce::int64 newPosition)
{
    if (readerSource != nullptr)
        readerSource->setNextReadPosition(juce::jmax<juce::int64>(0, newPosition));
}

juce::int64 LoopingAudioSource::getNextReadPosition() const
{
    if (readerSource == nullptr)
        return 0;

    const auto position = readerSource->getNextReadPosition();
    const auto length = readerSource->getTotalLength();
    return isLooping() && length > 0 ? position % length : position;
}

juce::int64 LoopingAudioSource::getTotalLength() const
{
    return readerSource != nullptr ? readerSource->getTotalLength() : 0;
}
