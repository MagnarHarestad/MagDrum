#include "WavScanner.h"
#include "AudioClassifier.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct PitchEstimate { double frequency = 0.0, confidence = 0.0; };

PitchEstimate estimateFundamental(const juce::AudioBuffer<float>& buffer, unsigned int channelCount, int count, double sampleRate)
{
    const int decimation = juce::jmax(1, (count + 1023) / 1024);
    const int size = count / decimation;
    const double effectiveRate = sampleRate / decimation;
    if (size < 32 || effectiveRate <= 0.0) return {};

    std::vector<double> mono(static_cast<size_t>(size));
    double mean = 0.0;
    for (int i = 0; i < size; ++i)
    {
        double value = 0.0;
        for (unsigned int channel = 0; channel < channelCount; ++channel)
            for (int j = 0; j < decimation; ++j)
                value += buffer.getSample(static_cast<int>(channel), i * decimation + j);
        value /= static_cast<double>(channelCount * static_cast<unsigned int>(decimation));
        mono[static_cast<size_t>(i)] = value;
        mean += value;
    }
    mean /= size;
    for (auto& value : mono) value -= mean;

    const int minLag = juce::jmax(2, static_cast<int>(std::floor(effectiveRate / 1000.0)));
    const int maxLag = juce::jmin(size / 2, static_cast<int>(std::ceil(effectiveRate / 25.0)));
    if (maxLag <= minLag) return {};
    std::vector<double> correlation(static_cast<size_t>(maxLag + 1), 0.0);
    double strongest = 0.0;
    int strongestLag = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double cross = 0.0, energyA = 0.0, energyB = 0.0;
        for (int i = 0; i < size - lag; ++i)
        {
            const double a = mono[static_cast<size_t>(i)], b = mono[static_cast<size_t>(i + lag)];
            cross += a * b; energyA += a * a; energyB += b * b;
        }
        const double denominator = std::sqrt(energyA * energyB);
        const double score = denominator > 0.0 ? cross / denominator : 0.0;
        correlation[static_cast<size_t>(lag)] = score;
        if (score > strongest) { strongest = score; strongestLag = lag; }
    }
    if (strongest < 0.65 || strongestLag == 0) return {};

    int selectedLag = strongestLag;
    for (int lag = minLag + 1; lag < strongestLag; ++lag)
        if (correlation[static_cast<size_t>(lag)] >= strongest * 0.95
            && correlation[static_cast<size_t>(lag)] >= correlation[static_cast<size_t>(lag - 1)]
            && correlation[static_cast<size_t>(lag)] >= correlation[static_cast<size_t>(lag + 1)])
        {
            selectedLag = lag;
            break;
        }
    double adjustment = 0.0;
    if (selectedLag > minLag && selectedLag < maxLag)
    {
        const double before = correlation[static_cast<size_t>(selectedLag - 1)];
        const double current = correlation[static_cast<size_t>(selectedLag)];
        const double after = correlation[static_cast<size_t>(selectedLag + 1)];
        const double divisor = before - 2.0 * current + after;
        if (std::abs(divisor) > 1.0e-12) adjustment = juce::jlimit(-0.5, 0.5, 0.5 * (before - after) / divisor);
    }
    return {effectiveRate / (selectedLag + adjustment), juce::jlimit(0.0, 1.0, correlation[static_cast<size_t>(selectedLag)])};
}
}

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
        const auto sampleCount = static_cast<juce::int64>(reader->lengthInSamples);
        double sumSquares = 0.0;
        double previousMean = 0.0;
        double transientMax = 0.0;
        double centroid = 0.0;
        bool centroidMeasured = false;
        bool pitchMeasured = false;
        constexpr int fftOrder = 10;
        constexpr int fftSize = 1 << fftOrder;
        juce::dsp::FFT fft(fftOrder);
        juce::HeapBlock<float> fftData(static_cast<size_t>(fftSize * 2), true);
        juce::int64 measured = 0;
        for (juce::int64 offset = 0; offset < sampleCount; offset += frameCount)
        {
            if (shouldCancel && shouldCancel())
                break;
            const int count = static_cast<int>(std::min<juce::int64>(frameCount, sampleCount - offset));
            buffer.clear();
            if (!reader->read(channelData.getData(), static_cast<int>(reader->numChannels), offset, count))
                break;
            if (!centroidMeasured && count >= fftSize)
            {
                double sumMagnitude = 0.0, weightedMagnitude = 0.0;
                for (int i = 0; i < fftSize; ++i)
                {
                    double mono = 0.0;
                    for (unsigned int c = 0; c < reader->numChannels; ++c)
                        mono += buffer.getSample(static_cast<int>(c), i);
                    mono /= reader->numChannels;
                    fftData[i] = static_cast<float>(mono * (0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (fftSize - 1))));
                }
                fft.performFrequencyOnlyForwardTransform(fftData.getData());
                for (int bin = 1; bin < fftSize / 2; ++bin)
                {
                    const double magnitude = fftData[bin];
                    const double hz = static_cast<double>(bin) * reader->sampleRate / fftSize;
                    weightedMagnitude += hz * magnitude;
                    sumMagnitude += magnitude;
                }
                centroid = sumMagnitude > 0.0 ? weightedMagnitude / sumMagnitude : 0.0;
                centroidMeasured = true;
            }
            if (!pitchMeasured && count >= 256)
            {
                const auto pitch = estimateFundamental(buffer, reader->numChannels, count, reader->sampleRate);
                sample.fundamentalHz = pitch.frequency;
                sample.pitchConfidence = pitch.confidence;
                pitchMeasured = true;
            }
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
        sample.spectralCentroidHz = centroid;
        sample.fingerprint = juce::SHA256(file).toHexString();
        const auto classification = AudioClassifier{}.classify(sample);
        sample.category = classification.category;
        sample.classificationConfidence = classification.confidence;
        result.add(std::move(sample));
    }
    return result;
}
