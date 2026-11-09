#include "PluginNoteScanner.h"
#include <algorithm>
#include <cmath>
#include <limits>

PluginNoteScanResult PluginNoteScanner::scan(juce::AudioProcessor& plugin,
                                             const PluginNoteScanSettings& settings,
                                             ShouldCancel shouldCancel,
                                             Progress progress) const
{
    PluginNoteScanResult result;
    if (!std::isfinite(settings.sampleRate) || settings.sampleRate < 8000.0 || settings.sampleRate > 192000.0
        || settings.blockSize < 16 || settings.blockSize > 8192 || settings.velocityLevels.empty() || settings.velocityLevels.size() > 16
        || settings.repeatCount < 1 || settings.repeatCount > 8
        || !std::isfinite(settings.noteDurationSeconds) || settings.noteDurationSeconds <= 0.0 || settings.noteDurationSeconds > 10.0
        || !std::isfinite(settings.releaseSeconds) || settings.releaseSeconds < 0.0 || settings.releaseSeconds > 10.0
        || !std::isfinite(settings.captureLengthSeconds) || settings.captureLengthSeconds <= 0.0 || settings.captureLengthSeconds > 20.0
        || !std::isfinite(settings.silencePeakThreshold) || settings.silencePeakThreshold < 0.0)
    {
        result.error = "Invalid plugin note scan settings.";
        return result;
    }
    for (const auto velocity : settings.velocityLevels)
        if (velocity < 1 || velocity > 127)
        {
            result.error = "Velocity levels must be between 1 and 127.";
            return result;
        }

    const int outputChannels = plugin.getTotalNumOutputChannels();
    if (outputChannels < 1 || outputChannels > 8)
    {
        result.error = "The selected plugin does not expose a supported audio output.";
        return result;
    }

    const auto noteSamples = static_cast<juce::int64>(std::llround(settings.noteDurationSeconds * settings.sampleRate));
    const auto releaseSamples = juce::jmax<juce::int64>(1, static_cast<juce::int64>(std::llround(settings.releaseSeconds * settings.sampleRate)));
    const auto captureSamples64 = juce::jmax(noteSamples + releaseSamples, static_cast<juce::int64>(std::llround(settings.captureLengthSeconds * settings.sampleRate)));
    if (captureSamples64 > static_cast<juce::int64>(settings.sampleRate * 20.0) || captureSamples64 > std::numeric_limits<int>::max())
    {
        result.error = "The requested capture is too long.";
        return result;
    }
    const int captureSamples = static_cast<int>(captureSamples64);
    const int inputChannels = juce::jlimit(0, 8, plugin.getTotalNumInputChannels());
    const int processChannels = juce::jmax(1, juce::jmax(inputChannels, outputChannels));
    const auto totalCaptureCount = static_cast<juce::int64>(128) * static_cast<juce::int64>(settings.velocityLevels.size()) * settings.repeatCount;
    const auto estimatedBytes = totalCaptureCount * captureSamples * outputChannels * static_cast<int>(sizeof(float));
    if (estimatedBytes > 512 * 1024 * 1024)
    {
        result.error = "The requested scan would require more than 512 MiB of capture memory. Reduce velocity levels, repeats, or capture length.";
        return result;
    }

    juce::MemoryBlock previousState;
    plugin.getStateInformation(previousState);
    if (previousState.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        result.error = "The plugin state is too large to preserve safely.";
        return result;
    }
    const bool wasNonRealtime = plugin.isNonRealtime();
    plugin.setNonRealtime(true);
    bool prepared = false;
    const auto restorePlugin = [&]()
    {
        if (prepared)
            plugin.releaseResources();
        if (!previousState.isEmpty())
            plugin.setStateInformation(previousState.getData(), static_cast<int>(previousState.getSize()));
        plugin.setNonRealtime(wasNonRealtime);
    };

    try
    {
        plugin.prepareToPlay(settings.sampleRate, settings.blockSize);
        prepared = true;
        const int totalScans = static_cast<int>(totalCaptureCount);
        result.notes.reserve(static_cast<size_t>(totalScans));
        int completed = 0;

        for (const auto velocity : settings.velocityLevels)
        {
            for (int note = 0; note < 128; ++note)
            {
                if (shouldCancel && shouldCancel())
                {
                    result.cancelled = true;
                    break;
                }

                plugin.reset();
                for (int take = 1; take <= settings.repeatCount; ++take)
                {
                    if (shouldCancel && shouldCancel())
                    {
                        result.cancelled = true;
                        break;
                    }

                    PluginNoteCapture capture;
                    capture.midiNote = note;
                    capture.velocity = velocity;
                    capture.takeIndex = take;
                    capture.audio.setSize(outputChannels, captureSamples, false, true, false);

                    juce::AudioBuffer<float> block(processChannels, settings.blockSize);
                    juce::MidiBuffer midi;
                    double sumSquares = 0.0;
                    juce::int64 measuredSamples = 0;

                    for (int offset = 0; offset < captureSamples; offset += settings.blockSize)
                    {
                        const int count = juce::jmin(settings.blockSize, captureSamples - offset);
                        block.clear();
                        midi.clear();
                        if (offset == 0)
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity)), 0);
                        if (noteSamples >= offset && noteSamples < offset + count)
                            midi.addEvent(juce::MidiMessage::noteOff(1, note), static_cast<int>(noteSamples - offset));

                        plugin.processBlock(block, midi);
                        for (int channel = 0; channel < outputChannels; ++channel)
                        {
                            const float* source = block.getReadPointer(channel);
                            float* destination = capture.audio.getWritePointer(channel) + offset;
                            for (int i = 0; i < count; ++i)
                            {
                                const double value = std::isfinite(source[i]) ? source[i] : 0.0;
                                capture.peak = juce::jmax(capture.peak, std::abs(value));
                                sumSquares += value * value;
                                destination[i] = static_cast<float>(value);
                            }
                            measuredSamples += count;
                        }
                    }

                    capture.rms = measuredSamples > 0 ? std::sqrt(sumSquares / static_cast<double>(measuredSamples)) : 0.0;
                    capture.silent = capture.peak <= settings.silencePeakThreshold;
                    result.notes.push_back(std::move(capture));
                    ++completed;
                    if (progress)
                        progress(completed, totalScans, note, velocity);
                }
                if (result.cancelled)
                    break;
            }
            if (result.cancelled)
                break;
        }
    }
    catch (...)
    {
        result.error = "The selected plugin failed while rendering MIDI notes.";
    }

    restorePlugin();
    return result;
}
