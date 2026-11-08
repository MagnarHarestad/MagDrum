#include "WavScanner.h"

juce::Array<ScannedSample> WavScanner::scan(const juce::File& root, ShouldCancel shouldCancel) const
{
    juce::Array<ScannedSample> result;
    if (!root.isDirectory())
        return result;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
    {
        if (shouldCancel && shouldCancel())
            break;
        const auto file = entry.getFile();
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension != ".wav" && extension != ".wave" && extension != ".aif" && extension != ".aiff" && extension != ".flac")
            continue;
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            continue;
        ScannedSample sample;
        sample.file = file;
        sample.durationSeconds = reader->lengthInSamples / reader->sampleRate;
        sample.channels = static_cast<int>(reader->numChannels);
        sample.sampleRate = reader->sampleRate;
        sample.format = extension.substring(1).toUpperCase();
        sample.bitDepth = reader-> bitsPerSample;

        constexpr int frameCount = 4096;
        juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), frameCount);
        juce::HeapBlock<float*> channelData(static_cast<size_t>(reader->numChannels));
        for (unsigned int c = 0; c < reader->numChannels; ++c)
            channelData[c] = buffer.getWritePointer(static_cast<int>(c));
        const auto sampleCount = static_cast<int64>(reader->lengthInSamples);
        double sumSquares = 0.0;
        double previousMean = 0.0;
        double transientMax = 0.0;
        int64 measured = 0;
        for (int64 offset = 0; offset < sampleCount; offset += frameCount)
        {
            if (shouldCancel && shouldCancel())
                break;
            const int count = static_cast<int>(juce::jmin<int64>(frameCount, sampleCount - offset));
            buffer.clear();
            if (!reader->read(channelData.getData(), static_cast<int>(reader->numChannels), offset, count))
                break;
            double frameEnergy = 0.0;
            for (unsigned int c = 0; c < reader->numChannels; ++c)
                for (int i = 0; i < count; ++i)
                {
                    const double value = buffer.getSample(static_cast<int>(c), i);
                    sample.peak = juce::jmax(sample.peak, std::abs(value));
                    sumSquares += value * value;
                    frameEnergy += value * value;
                    ++measured;
                }
            const double mean = std::sqrt(frameEnergy / juce::jmax(1, count * static_cast<int>(reader->numChannels)));
            transientMax = juce::jmax(transientMax, juce::jmax(0.0, mean - previousMean));
            previousMean = mean;
        }
        sample.rms = measured > 0 ? std::sqrt(sumSquares / measured) : 0.0;
        sample.transientStrength = transientMax;
        sample.fingerprint = juce::SHA256(file).toHexString();
        result.add(std::move(sample));
    }
    return result;
}
