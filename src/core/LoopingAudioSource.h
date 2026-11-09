#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

class LoopingAudioSource final : public juce::PositionableAudioSource
{
public:
    explicit LoopingAudioSource(juce::AudioFormatReader* reader);

    void setLooping(bool shouldLoop) noexcept;
    bool isLooping() const noexcept;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void setNextReadPosition(juce::int64 newPosition) override;
    juce::int64 getNextReadPosition() const override;
    juce::int64 getTotalLength() const override;

private:
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::atomic<bool> looping{false};
};
