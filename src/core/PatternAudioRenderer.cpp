#include "PatternAudioRenderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace
{
constexpr int outputSampleRate = 44100;
constexpr int renderBlockSize = 4096;
constexpr int maxVoices = 64;
constexpr double maximumPreviewSeconds = 120.0;

struct TempoSegment
{
    double startBeat = 0.0;
    double startSeconds = 0.0;
    double secondsPerBeat = 0.5;
};

struct ScheduledHit
{
    std::int64_t frame = 0;
    int sampleIndex = 0;
    int velocity = 100;
};

struct Voice
{
    int sampleIndex = -1;
    double position = 0.0;
    float gain = 0.0f;
    bool active = false;
};

int getInstrumentIndex(DrumInstrument instrument)
{
    const int index = static_cast<int>(instrument);
    return juce::isPositiveAndBelow(index, 9) ? index : -1;
}

double secondsAtBeat(double beat, const std::vector<TempoSegment>& segments)
{
    const auto found = std::upper_bound(segments.begin(), segments.end(), beat,
        [](double value, const TempoSegment& segment) { return value < segment.startBeat; });
    const auto& segment = found == segments.begin() ? segments.front() : *std::prev(found);
    return segment.startSeconds + juce::jmax(0.0, beat - segment.startBeat) * segment.secondsPerBeat;
}

std::vector<TempoSegment> makeTempoSegments(const MidiPattern& pattern)
{
    std::vector<MidiMetaEvent> changes;
    for (const auto& item : pattern.tempoAndSignature)
        if (item.message.isTempoMetaEvent() && std::isfinite(item.positionBeats) && item.positionBeats >= 0.0)
            changes.push_back(item);
    std::stable_sort(changes.begin(), changes.end(),
        [](const auto& left, const auto& right) { return left.positionBeats < right.positionBeats; });

    std::vector<TempoSegment> segments{{0.0, 0.0, 0.5}};
    for (const auto& change : changes)
    {
        const double secondsPerBeat = change.message.getTempoSecondsPerQuarterNote();
        if (!std::isfinite(secondsPerBeat) || secondsPerBeat <= 0.0)
            continue;
        const double beat = change.positionBeats;
        const double seconds = segments.back().startSeconds
            + (beat - segments.back().startBeat) * segments.back().secondsPerBeat;
        if (std::abs(beat - segments.back().startBeat) < 1.0e-9)
            segments.back() = {beat, seconds, secondsPerBeat};
        else
            segments.push_back({beat, seconds, secondsPerBeat});
    }
    return segments;
}

double getPatternLengthBeats(const MidiPattern& pattern)
{
    double contentEnd = juce::jmax(0.0, pattern.lengthBeats);
    for (const auto& event : pattern.events)
        if (std::isfinite(event.positionBeats) && std::isfinite(event.durationBeats))
            contentEnd = juce::jmax(contentEnd, juce::jmax(0.0, event.positionBeats) + juce::jmax(0.0, event.durationBeats));

    int numerator = 4, denominator = 4;
    for (const auto& item : pattern.tempoAndSignature)
        if (item.positionBeats <= 0.0 && item.message.isTimeSignatureMetaEvent())
            item.message.getTimeSignatureInfo(numerator, denominator);
    if (numerator < 1 || denominator < 1)
        return contentEnd;
    const double barLength = static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
    if (contentEnd <= 0.0)
        return barLength;
    if (pattern.lengthBeats > 0.0)
        return contentEnd;
    return std::ceil(contentEnd / barLength - 1.0e-9) * barLength;
}
}

bool PatternAudioRenderer::loadSample(juce::AudioFormatManager& formats, const juce::File& file,
                                      PatternAudioSample& destination, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "The sample file is missing.";
        return false;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        error = "The sample file could not be decoded.";
        return false;
    }

    constexpr double maxSampleSeconds = 15.0;
    const int channels = static_cast<int>(juce::jmin(2u, reader->numChannels));
    const auto maxSamples = static_cast<juce::int64>(reader->sampleRate * maxSampleSeconds);
    const int sampleCount = static_cast<int>(std::min(reader->lengthInSamples, maxSamples));
    PatternAudioSample sample;
    sample.audio.setSize(channels, sampleCount);
    if (!reader->read(sample.audio.getArrayOfWritePointers(), channels, 0, sampleCount))
    {
        error = "The sample file could not be read.";
        return false;
    }
    sample.sampleRate = reader->sampleRate;
    destination = std::move(sample);
    return true;
}

PatternAudioRenderResult PatternAudioRenderer::render(const MidiPattern& pattern, const Samples& samples,
                                                      const juce::File& output, ShouldCancel shouldCancel) const
{
    PatternAudioRenderResult result;
    if (output.existsAsFile())
    {
        result.error = "The preview output file already exists.";
        return result;
    }

    const auto tempoSegments = makeTempoSegments(pattern);
    const double patternLengthBeats = getPatternLengthBeats(pattern);
    if (!std::isfinite(patternLengthBeats) || patternLengthBeats <= 0.0)
    {
        result.error = "The pattern has no playable length.";
        return result;
    }

    std::vector<ScheduledHit> hits;
    double outputDuration = secondsAtBeat(patternLengthBeats, tempoSegments);
    for (const auto& event : pattern.events)
    {
        const int sampleIndex = getInstrumentIndex(event.instrument);
        if (sampleIndex < 0 || samples[static_cast<size_t>(sampleIndex)].audio.getNumSamples() == 0
            || samples[static_cast<size_t>(sampleIndex)].sampleRate <= 0.0
            || !std::isfinite(event.positionBeats) || event.positionBeats < 0.0)
            continue;
        const double startSeconds = secondsAtBeat(event.positionBeats, tempoSegments);
        hits.push_back({static_cast<std::int64_t>(std::llround(startSeconds * outputSampleRate)),
                        sampleIndex, juce::jlimit(1, 127, static_cast<int>(event.velocity))});
        const auto& sample = samples[static_cast<size_t>(sampleIndex)];
        const double sampleDuration = sample.audio.getNumSamples() / sample.sampleRate;
        outputDuration = juce::jmax(outputDuration, startSeconds + sampleDuration);
    }
    if (hits.empty())
    {
        result.error = "Add drum hits and index at least one matching sample before previewing.";
        return result;
    }
    if (!std::isfinite(outputDuration) || outputDuration <= 0.0 || outputDuration > maximumPreviewSeconds)
    {
        result.error = "The preview is longer than the 120-second limit.";
        return result;
    }
    std::stable_sort(hits.begin(), hits.end(),
        [](const auto& left, const auto& right) { return left.frame < right.frame; });

    if (shouldCancel && shouldCancel())
    {
        result.error = "Preview cancelled.";
        return result;
    }
    if (!output.getParentDirectory().createDirectory())
    {
        result.error = "The preview folder could not be created.";
        return result;
    }
    std::unique_ptr<juce::OutputStream> stream = output.createOutputStream();
    if (!stream)
    {
        result.error = "The preview file could not be created.";
        return result;
    }
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(outputSampleRate)
        .withNumChannels(2).withBitsPerSample(24);
    auto writer = wav.createWriterFor(stream, options);
    if (!writer)
    {
        stream.reset();
        output.deleteFile();
        result.error = "The WAV preview writer could not be created.";
        return result;
    }

    const auto totalFrames = static_cast<std::int64_t>(std::ceil(outputDuration * outputSampleRate));
    juce::AudioBuffer<float> block(2, renderBlockSize);
    std::array<Voice, maxVoices> voices{};
    size_t nextHit = 0;
    size_t nextVoice = 0;
    for (std::int64_t blockStart = 0; blockStart < totalFrames; blockStart += renderBlockSize)
    {
        if (shouldCancel && shouldCancel())
        {
            writer.reset();
            output.deleteFile();
            result.error = "Preview cancelled.";
            return result;
        }
        const int frames = static_cast<int>(juce::jmin<std::int64_t>(renderBlockSize, totalFrames - blockStart));
        block.clear();
        for (int frame = 0; frame < frames; ++frame)
        {
            const auto absoluteFrame = blockStart + frame;
            while (nextHit < hits.size() && hits[nextHit].frame <= absoluteFrame)
            {
                const auto& hit = hits[nextHit++];
                auto& voice = voices[nextVoice++ % voices.size()];
                voice = {hit.sampleIndex, 0.0, static_cast<float>(hit.velocity) / 127.0f * 0.35f, true};
            }

            float left = 0.0f, right = 0.0f;
            for (auto& voice : voices)
            {
                if (!voice.active || voice.sampleIndex < 0)
                    continue;
                const auto& sample = samples[static_cast<size_t>(voice.sampleIndex)];
                const auto& audio = sample.audio;
                const int index = static_cast<int>(voice.position);
                if (index >= audio.getNumSamples())
                {
                    voice.active = false;
                    continue;
                }
                const int next = juce::jmin(index + 1, audio.getNumSamples() - 1);
                const float fraction = static_cast<float>(voice.position - index);
                const auto readChannel = [&](int channel)
                {
                    const auto* data = audio.getReadPointer(juce::jmin(channel, audio.getNumChannels() - 1));
                    return (data[index] + (data[next] - data[index]) * fraction) * voice.gain;
                };
                left += readChannel(0);
                right += readChannel(audio.getNumChannels() > 1 ? 1 : 0);
                voice.position += sample.sampleRate / outputSampleRate;
            }
            block.setSample(0, frame, juce::jlimit(-1.0f, 1.0f, left));
            block.setSample(1, frame, juce::jlimit(-1.0f, 1.0f, right));
        }
        if (!writer->writeFromAudioSampleBuffer(block, 0, frames))
        {
            writer.reset();
            output.deleteFile();
            result.error = "The preview WAV could not be written.";
            return result;
        }
    }

    writer.reset();
    result.ok = true;
    result.durationSeconds = outputDuration;
    return result;
}
