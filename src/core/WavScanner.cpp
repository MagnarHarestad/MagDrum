#include "WavScanner.h"
#include "AudioClassifier.h"
#include <juce_cryptography/juce_cryptography.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
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

std::vector<double> findTransientAttackTimes(const std::vector<double>& envelope, double sampleRate, double overallRms)
{
    std::vector<double> attackTimes;
    if (envelope.size() < 2 || sampleRate <= 0.0) return attackTimes;
    const auto peak = *std::max_element(envelope.begin(), envelope.end());
    if (peak <= 1.0e-8) return attackTimes;
    constexpr int envelopeFrameSamples = 256;
    const auto minimumSeparationFrames = juce::jmax(1, static_cast<int>(std::lround(sampleRate * 0.012 / envelopeFrameSamples)));
    const auto minimumLevel = juce::jmax(peak * 0.15, overallRms * 0.10);
    int lastAttack = -minimumSeparationFrames;
    for (size_t index = 0; index + 1 < envelope.size(); ++index)
    {
        const auto previous = index == 0 ? 0.0 : envelope[index - 1];
        const auto current = envelope[index];
        if (current >= minimumLevel && current > previous * 1.8
            && static_cast<int>(index) - lastAttack >= minimumSeparationFrames)
        {
            lastAttack = static_cast<int>(index);
            attackTimes.push_back(static_cast<double>(index * envelopeFrameSamples) / sampleRate);
        }
    }
    return attackTimes;
}

juce::String createAcousticFingerprint(juce::AudioFormatReader& reader, juce::dsp::FFT& fft,
                                       juce::HeapBlock<float>& fftData, juce::AudioBuffer<float>& buffer,
                                       float** channelData, int fftSize)
{
    constexpr int segmentCount = 4;
    constexpr int bandCount = 16;
    std::array<std::uint8_t, segmentCount * bandCount> quantized{};
    const auto sampleCount = static_cast<juce::int64>(reader.lengthInSamples);
    const double topHz = juce::jmin(18000.0, reader.sampleRate * 0.48);
    if (sampleCount <= 0 || topHz <= 30.0) return {};

    for (int segment = 0; segment < segmentCount; ++segment)
    {
        const auto start = sampleCount <= fftSize ? juce::int64(0)
            : static_cast<juce::int64>((sampleCount - fftSize) * segment / (segmentCount - 1));
        const int count = static_cast<int>(std::min<juce::int64>(fftSize, sampleCount - start));
        if (count <= 0) continue;
        buffer.clear();
        if (!reader.read(channelData, static_cast<int>(reader.numChannels), start, count)) return {};
        std::fill_n(fftData.getData(), fftSize * 2, 0.0f);
        for (int i = 0; i < count; ++i)
        {
            double mono = 0.0;
            for (unsigned int channel = 0; channel < reader.numChannels; ++channel)
                mono += buffer.getSample(static_cast<int>(channel), i);
            mono /= reader.numChannels;
            const double window = count > 1 ? 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (count - 1)) : 1.0;
            fftData[i] = static_cast<float>(mono * window);
        }
        fft.performFrequencyOnlyForwardTransform(fftData.getData());
        std::array<double, bandCount> bandEnergy{};
        double totalEnergy = 0.0;
        for (int band = 0; band < bandCount; ++band)
        {
            const double lowHz = 30.0 * std::pow(topHz / 30.0, static_cast<double>(band) / bandCount);
            const double highHz = 30.0 * std::pow(topHz / 30.0, static_cast<double>(band + 1) / bandCount);
            const int firstBin = juce::jlimit(1, fftSize / 2 - 1, static_cast<int>(std::floor(lowHz * fftSize / reader.sampleRate)));
            const int lastBin = juce::jlimit(firstBin + 1, fftSize / 2, static_cast<int>(std::ceil(highHz * fftSize / reader.sampleRate)));
            for (int bin = firstBin; bin < lastBin; ++bin)
            {
                const double magnitude = fftData[bin];
                bandEnergy[static_cast<size_t>(band)] += magnitude * magnitude;
            }
            totalEnergy += bandEnergy[static_cast<size_t>(band)];
        }
        if (totalEnergy <= 1.0e-20) continue;
        for (int band = 0; band < bandCount; ++band)
        {
            const auto proportion = bandEnergy[static_cast<size_t>(band)] / totalEnergy;
            quantized[static_cast<size_t>(segment * bandCount + band)] = static_cast<std::uint8_t>(juce::jlimit(0, 255, static_cast<int>(std::lround(proportion * 255.0))));
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.reserve(quantized.size() * 2);
    for (const auto value : quantized)
    {
        encoded.push_back(digits[value >> 4]);
        encoded.push_back(digits[value & 0x0f]);
    }
    return juce::String::fromUTF8(encoded.c_str());
}
}

double estimateRhythmicLoopConfidence(const std::vector<double>& attacks, double durationSeconds)
{
    if (!std::isfinite(durationSeconds) || durationSeconds < 2.0 || attacks.size() < 8)
        return 0.0;

    std::vector<double> validAttacks;
    validAttacks.reserve(juce::jmin(static_cast<int>(attacks.size()), 256));
    for (const auto time : attacks)
        if (std::isfinite(time) && time >= 0.0 && time < durationSeconds)
            validAttacks.push_back(time);
    if (validAttacks.size() < 8)
        return 0.0;
    std::sort(validAttacks.begin(), validAttacks.end());

    // Bound work for pathological material with thousands of detected attacks.
    if (validAttacks.size() > 256)
    {
        std::vector<double> bounded;
        bounded.reserve(256);
        const auto stride = static_cast<double>(validAttacks.size() - 1) / 255.0;
        for (int index = 0; index < 256; ++index)
            bounded.push_back(validAttacks[static_cast<size_t>(std::lround(index * stride))]);
        validAttacks = std::move(bounded);
    }

    const double firstAttack = validAttacks.front();
    const double coverage = (validAttacks.back() - firstAttack) / durationSeconds;
    if (coverage < 0.55)
        return 0.0;

    double bestAlignment = 0.0;
    constexpr double slowestBeatSeconds = 60.0 / 200.0;
    constexpr double fastestBeatSeconds = 60.0 / 55.0;
    constexpr double beatStepSeconds = 0.0025;
    for (double beatSeconds = slowestBeatSeconds; beatSeconds <= fastestBeatSeconds;
         beatSeconds += beatStepSeconds)
    {
        const double gridStepSeconds = beatSeconds / 4.0;
        const double toleranceSeconds = juce::jmin(0.045, gridStepSeconds * 0.32);
        int aligned = 0;
        for (const auto time : validAttacks)
        {
            const double gridPosition = (time - firstAttack) / gridStepSeconds;
            const double nearestGridTime = std::round(gridPosition) * gridStepSeconds;
            if (std::abs((time - firstAttack) - nearestGridTime) <= toleranceSeconds)
                ++aligned;
        }
        bestAlignment = juce::jmax(bestAlignment,
            static_cast<double>(aligned) / static_cast<double>(validAttacks.size()));
    }

    const double coverageWeight = juce::jlimit(0.0, 1.0, coverage / 0.80);
    return juce::jlimit(0.0, 1.0, bestAlignment * coverageWeight);
}

juce::Array<ScannedSample> WavScanner::scan(const juce::File& root, ShouldCancel shouldCancel,
                                            ShouldSkipFile shouldSkipFile, Stats* stats,
                                            SampleConsumer sampleConsumer,
                                            const juce::String& resumeAfterFile,
                                            FileProgressConsumer fileProgressConsumer,
                                            std::shared_ptr<const DrumClassificationModel> classificationModel,
                                            ScanProgressConsumer scanProgressConsumer) const
{
    juce::Array<ScannedSample> result;
    if (stats != nullptr) *stats = {};
    if (!root.isDirectory())
        return result;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const AudioClassifier classifier(std::move(classificationModel));
    juce::Array<juce::File> files;
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension == ".wav" || extension == ".wave" || extension == ".aif"
            || extension == ".aiff" || extension == ".flac")
            files.add(file);
        else if (stats != nullptr && extension == ".vstsound")
            ++stats->steinbergVstSoundContainers;
        else if (stats != nullptr && extension == ".ncw")
            ++stats->kontaktNcwFiles;
    }
    std::sort(files.begin(), files.end(), [](const juce::File& left, const juce::File& right)
    {
        const auto leftPath = left.getFullPathName();
        const auto rightPath = right.getFullPathName();
        const int folded = leftPath.compareIgnoreCase(rightPath);
        return folded == 0 ? leftPath < rightPath : folded < 0;
    });
    int firstFile = 0;
    if (resumeAfterFile.isNotEmpty())
        while (firstFile < files.size()
               && files.getReference(firstFile).getFullPathName().compareIgnoreCase(resumeAfterFile) <= 0)
            ++firstFile;
    if (scanProgressConsumer)
        scanProgressConsumer(root, 0, files.size() - firstFile);
    const auto reportProgress = [&](const juce::File& file, int fileIndex)
    {
        if (scanProgressConsumer)
            scanProgressConsumer(file, fileIndex - firstFile + 1, files.size() - firstFile);
    };
    for (int fileIndex = firstFile; fileIndex < files.size(); ++fileIndex)
    {
        if (shouldCancel && shouldCancel())
        {
            if (stats != nullptr) stats->cancelled = true;
            break;
        }
        const auto file = files.getReference(fileIndex);
        const auto extension = file.getFileExtension().toLowerCase();
        const auto checkpoint = [&]() { return !fileProgressConsumer || fileProgressConsumer(file); };
        const auto fileSizeBytes = static_cast<std::int64_t>(file.getSize());
        const auto modifiedTimeMillis = file.getLastModificationTime().toMilliseconds();
        if (stats != nullptr) ++stats->filesConsidered;
        if (shouldSkipFile && shouldSkipFile(file, fileSizeBytes, modifiedTimeMillis))
        {
            if (stats != nullptr) ++stats->filesSkippedUnchanged;
            if (!checkpoint() && stats != nullptr) ++stats->errors;
            reportProgress(file, fileIndex);
            continue;
        }
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
        {
            if (stats != nullptr) ++stats->errors;
            checkpoint();
            reportProgress(file, fileIndex);
            continue;
        }
        ScannedSample sample;
        sample.file = file;
        sample.fileSizeBytes = fileSizeBytes;
        sample.modifiedTimeMillis = modifiedTimeMillis;
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
        constexpr int transientFrameSize = 256;
        std::vector<double> transientEnvelope;
        bool pitchMeasured = false;
        constexpr int fftOrder = 10;
        constexpr int fftSize = 1 << fftOrder;
        juce::dsp::FFT fft(fftOrder);
        juce::HeapBlock<float> fftData(static_cast<size_t>(fftSize * 2), true);
        juce::int64 measured = 0;
        bool cancelledDuringFile = false;
        bool readFailed = false;
        for (juce::int64 offset = 0; offset < sampleCount; offset += frameCount)
        {
            if (shouldCancel && shouldCancel())
            {
                cancelledDuringFile = true;
                if (stats != nullptr) stats->cancelled = true;
                break;
            }
            const int count = static_cast<int>(std::min<juce::int64>(frameCount, sampleCount - offset));
            buffer.clear();
            if (!reader->read(channelData.getData(), static_cast<int>(reader->numChannels), offset, count))
            {
                readFailed = true;
                break;
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
            for (int frameOffset = 0; frameOffset < count; frameOffset += transientFrameSize)
            {
                const int transientCount = juce::jmin(transientFrameSize, count - frameOffset);
                double transientEnergy = 0.0;
                for (unsigned int channel = 0; channel < reader->numChannels; ++channel)
                    for (int i = 0; i < transientCount; ++i)
                    {
                        const auto value = buffer.getSample(static_cast<int>(channel), frameOffset + i);
                        transientEnergy += value * value;
                    }
                transientEnvelope.push_back(std::sqrt(transientEnergy / juce::jmax(1, transientCount * static_cast<int>(reader->numChannels))));
            }
        }
        if (cancelledDuringFile)
            break;
        if (readFailed)
        {
            if (stats != nullptr) ++stats->errors;
            checkpoint();
            reportProgress(file, fileIndex);
            continue;
        }
        if (measured == 0)
        {
            if (stats != nullptr) ++stats->errors;
            checkpoint();
            reportProgress(file, fileIndex);
            continue;
        }
        std::array<juce::int64, 4> centroidOffsets{};
        const auto lastCentroidOffset = std::max<juce::int64>(0, sampleCount - fftSize);
        for (size_t window = 0; window < centroidOffsets.size(); ++window)
            centroidOffsets[window] = lastCentroidOffset * static_cast<juce::int64>(window)
                                    / static_cast<juce::int64>(centroidOffsets.size() - 1);
        std::sort(centroidOffsets.begin(), centroidOffsets.end());
        double centroidMagnitude = 0.0, centroidWeightedMagnitude = 0.0;
        double spectralFlatnessTotal = 0.0;
        int spectralWindowCount = 0;
        juce::int64 previousCentroidOffset = -1;
        for (const auto offset : centroidOffsets)
        {
            if (offset == previousCentroidOffset)
                continue;
            previousCentroidOffset = offset;
            if (shouldCancel && shouldCancel())
            {
                cancelledDuringFile = true;
                if (stats != nullptr) stats->cancelled = true;
                break;
            }
            buffer.clear();
            const int count = static_cast<int>(std::min<juce::int64>(fftSize, sampleCount - offset));
            if (count <= 0 || !reader->read(channelData.getData(), static_cast<int>(reader->numChannels), offset, count))
            {
                readFailed = true;
                break;
            }
            std::fill_n(fftData.getData(), fftSize * 2, 0.0f);
            for (int i = 0; i < count; ++i)
            {
                double mono = 0.0;
                for (unsigned int channel = 0; channel < reader->numChannels; ++channel)
                    mono += buffer.getSample(static_cast<int>(channel), i);
                mono /= reader->numChannels;
                const double window = count > 1 ? 0.5 - 0.5 * std::cos(2.0 * juce::MathConstants<double>::pi * i / (count - 1)) : 1.0;
                fftData[i] = static_cast<float>(mono * window);
            }
            fft.performFrequencyOnlyForwardTransform(fftData.getData());
            double windowMagnitude = 0.0;
            double windowLogMagnitude = 0.0;
            int windowBinCount = 0;
            for (int bin = 1; bin < fftSize / 2; ++bin)
            {
                const double magnitude = fftData[bin];
                centroidWeightedMagnitude += static_cast<double>(bin) * reader->sampleRate / fftSize * magnitude;
                centroidMagnitude += magnitude;
                windowMagnitude += magnitude;
                windowLogMagnitude += std::log(juce::jmax(magnitude, 1.0e-12));
                ++windowBinCount;
            }
            if (windowBinCount > 0 && windowMagnitude > 0.0)
            {
                const double arithmeticMean = windowMagnitude / windowBinCount;
                const double geometricMean = std::exp(windowLogMagnitude / windowBinCount);
                spectralFlatnessTotal += juce::jlimit(0.0, 1.0, geometricMean / arithmeticMean);
                ++spectralWindowCount;
            }
        }
        if (cancelledDuringFile)
            break;
        if (readFailed)
        {
            if (stats != nullptr) ++stats->errors;
            checkpoint();
            continue;
        }
        sample.rms = measured > 0 ? std::sqrt(sumSquares / measured) : 0.0;
        sample.transientStrength = transientMax;
        sample.transientAttackTimesSeconds = findTransientAttackTimes(transientEnvelope, reader->sampleRate, sample.rms);
        sample.transientCount = static_cast<int>(sample.transientAttackTimesSeconds.size());
        sample.rhythmicLoopConfidence = estimateRhythmicLoopConfidence(
            sample.transientAttackTimesSeconds, sample.durationSeconds);
        sample.spectralCentroidHz = centroidMagnitude > 0.0 ? centroidWeightedMagnitude / centroidMagnitude : 0.0;
        sample.spectralFlatness = spectralWindowCount > 0 ? spectralFlatnessTotal / spectralWindowCount : 0.0;
        sample.fingerprint = juce::SHA256(file).toHexString();
        sample.acousticFingerprint = createAcousticFingerprint(*reader, fft, fftData, buffer, channelData.getData(), fftSize);
        sample.acousticFingerprintVersion = sample.acousticFingerprint.length() == 128 ? 1 : 0;
        const auto classification = classifier.classify(sample);
        sample.category = classification.category;
        sample.classificationTags = classification.tags;
        sample.classificationConfidence = classification.confidence;
        bool fileStored = true;
        if (sampleConsumer)
        {
            fileStored = sampleConsumer(sample);
            if (!fileStored && stats != nullptr) ++stats->errors;
        }
        else
            result.add(std::move(sample));
        if (fileStored)
        {
            if (stats != nullptr) ++stats->filesAnalyzed;
            if (!checkpoint() && stats != nullptr) ++stats->errors;
        }
        reportProgress(file, fileIndex);
    }
    return result;
}
