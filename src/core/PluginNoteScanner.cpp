#include "PluginNoteScanner.h"
#include "DeviceRealtimeRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

PluginNoteScanResult PluginNoteScanner::scan(juce::AudioProcessor& plugin,
                                             const PluginNoteScanSettings& settings,
                                             ShouldCancel shouldCancel,
                                             Progress progress,
                                             CaptureConsumer captureConsumer) const
{
    PluginNoteScanResult result;
    if (!std::isfinite(settings.sampleRate) || settings.sampleRate < 8000.0 || settings.sampleRate > 192000.0
        || settings.blockSize < 16 || settings.blockSize > 8192
        || settings.firstMidiNote < 0 || settings.firstMidiNote > 127
        || settings.lastMidiNote < settings.firstMidiNote || settings.lastMidiNote > 127
        || settings.velocityLevels.empty() || settings.velocityLevels.size() > 16
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
    if (outputChannels < 1 || outputChannels > 64)
    {
        result.error = "The selected plugin must expose between 1 and 64 audio output channels.";
        return result;
    }

    double sampleRate = settings.sampleRate;
    int blockSize = settings.blockSize;
    std::unique_ptr<DeviceRealtimeRenderer> deviceRenderer;
    if (settings.renderMode == PluginNoteRenderMode::audioDevice)
    {
        deviceRenderer = std::make_unique<DeviceRealtimeRenderer>(plugin);
        if (!deviceRenderer->open(result.error))
            return result;
        sampleRate = deviceRenderer->getSampleRate();
        blockSize = deviceRenderer->getBlockSize();
    }

    const auto noteSamples = static_cast<juce::int64>(std::llround(settings.noteDurationSeconds * sampleRate));
    const auto releaseSamples = juce::jmax<juce::int64>(1, static_cast<juce::int64>(std::llround(settings.releaseSeconds * sampleRate)));
    const auto captureSamples64 = juce::jmax(noteSamples + releaseSamples, static_cast<juce::int64>(std::llround(settings.captureLengthSeconds * sampleRate)));
    if (captureSamples64 > static_cast<juce::int64>(sampleRate * 20.0) || captureSamples64 > std::numeric_limits<int>::max())
    {
        result.error = "The requested capture is too long.";
        return result;
    }
    const int captureSamples = static_cast<int>(captureSamples64);
    const int inputChannels = juce::jlimit(0, 8, plugin.getTotalNumInputChannels());
    const int processChannels = juce::jmax(1, juce::jmax(inputChannels, outputChannels));
    const auto noteCount = static_cast<juce::int64>(settings.lastMidiNote - settings.firstMidiNote + 1);
    const auto totalCaptureCount = noteCount * static_cast<juce::int64>(settings.velocityLevels.size()) * settings.repeatCount;
    const auto estimatedBytes = totalCaptureCount * captureSamples * outputChannels * static_cast<int>(sizeof(float));
    if (!captureConsumer && estimatedBytes > 512 * 1024 * 1024)
    {
        result.error = "The in-memory scan result would exceed 512 MiB. Use a streaming capture consumer or reduce velocity levels, repeats, or capture length.";
        return result;
    }

    juce::MemoryBlock previousState;
    plugin.getStateInformation(previousState);
    if (previousState.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        result.error = "The plugin state is too large to preserve safely.";
        return result;
    }
    const int previousProgramCount = plugin.getNumPrograms();
    const int previousProgram = previousProgramCount > 0 ? plugin.getCurrentProgram() : -1;
    const bool wasNonRealtime = plugin.isNonRealtime();
    plugin.setNonRealtime(settings.renderMode == PluginNoteRenderMode::offline);
    bool prepared = false;
    const auto restorePlugin = [&]()
    {
        if (prepared)
        {
            if (deviceRenderer != nullptr)
                deviceRenderer->detach();
            plugin.releaseResources();
        }
        const auto restoreHostState = [&]
        {
            if (!previousState.isEmpty())
                plugin.setStateInformation(previousState.getData(), static_cast<int>(previousState.getSize()));
            if (previousProgram >= 0 && previousProgram < plugin.getNumPrograms()
                && plugin.getCurrentProgram() != previousProgram)
                plugin.setCurrentProgram(previousProgram);
            plugin.setNonRealtime(wasNonRealtime);
        };
        if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
            messageManager != nullptr && !messageManager->isThisTheMessageThread())
            juce::MessageManager::callSync(restoreHostState);
        else
            restoreHostState();
    };

    try
    {
        plugin.prepareToPlay(sampleRate, blockSize);
        prepared = true;
        if (deviceRenderer != nullptr)
            deviceRenderer->attach();
        const int totalScans = static_cast<int>(totalCaptureCount);
        if (!captureConsumer)
            result.notes.reserve(static_cast<size_t>(totalScans));
        int completed = 0;

        for (const auto velocity : settings.velocityLevels)
        {
            for (int note = settings.firstMidiNote; note <= settings.lastMidiNote; ++note)
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
                    capture.sampleRate = sampleRate;
                    capture.audio.setSize(outputChannels, captureSamples, false, true, false);

                    juce::AudioBuffer<float> block(processChannels, blockSize);
                    juce::MidiBuffer midi;
                    const auto captureStart = std::chrono::steady_clock::now();

                    bool deviceCancelled = false;
                    if (deviceRenderer != nullptr)
                    {
                        if (!deviceRenderer->render(note, velocity, static_cast<int>(noteSamples), capture.audio,
                            shouldCancel, deviceCancelled))
                        {
                            if (deviceCancelled)
                                result.cancelled = true;
                            else
                                result.error = "The default audio device stopped or timed out during real-time plugin rendering.";
                        }
                    }
                    else for (int offset = 0; offset < captureSamples; offset += blockSize)
                    {
                        const int count = juce::jmin(blockSize, captureSamples - offset);
                        block.clear();
                        midi.clear();
                        if (offset == 0)
                            midi.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(velocity)), 0);
                        if (noteSamples >= offset && noteSamples < offset + count)
                            midi.addEvent(juce::MidiMessage::noteOff(1, note), static_cast<int>(noteSamples - offset));

                        plugin.processBlock(block, midi);
                        if (settings.renderMode == PluginNoteRenderMode::realtimePaced)
                        {
                            const auto samplesThroughBlock = static_cast<double>(offset + count);
                            const auto deadline = captureStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double>(samplesThroughBlock / sampleRate));
                            std::this_thread::sleep_until(deadline);
                        }
                        for (int channel = 0; channel < outputChannels; ++channel)
                        {
                            const float* source = block.getReadPointer(channel);
                            float* destination = capture.audio.getWritePointer(channel) + offset;
                            for (int i = 0; i < count; ++i)
                            {
                                const double value = std::isfinite(source[i]) ? source[i] : 0.0;
                                destination[i] = static_cast<float>(value);
                            }
                        }
                    }

                    if (result.cancelled || result.error.isNotEmpty())
                        break;
                    std::vector<double> channelEnergy(static_cast<size_t>(outputChannels), 0.0);
                    for (int channel = 0; channel < outputChannels; ++channel)
                    {
                        float* samples = capture.audio.getWritePointer(channel);
                        for (int i = 0; i < captureSamples; ++i)
                        {
                            const float value = std::isfinite(samples[i]) ? samples[i] : 0.0f;
                            samples[i] = value;
                            channelEnergy[static_cast<size_t>(channel)] += static_cast<double>(value) * value;
                        }
                    }

                    int firstChannel = 0;
                    int secondChannel = outputChannels > 1 ? 1 : -1;
                    if (outputChannels > 2)
                    {
                        double bestPairEnergy = channelEnergy[0] + channelEnergy[1];
                        for (int channel = 2; channel + 1 < outputChannels; channel += 2)
                        {
                            const double pairEnergy = channelEnergy[static_cast<size_t>(channel)]
                                + channelEnergy[static_cast<size_t>(channel + 1)];
                            if (pairEnergy > bestPairEnergy)
                            {
                                bestPairEnergy = pairEnergy;
                                firstChannel = channel;
                                secondChannel = channel + 1;
                            }
                        }
                        if ((outputChannels % 2) != 0)
                        {
                            const int lastChannel = outputChannels - 1;
                            if (channelEnergy[static_cast<size_t>(lastChannel)] > bestPairEnergy)
                            {
                                firstChannel = lastChannel;
                                secondChannel = -1;
                            }
                        }
                    }

                    if (outputChannels > 2)
                    {
                        juce::AudioBuffer<float> selectedOutputs(secondChannel >= 0 ? 2 : 1, captureSamples);
                        selectedOutputs.copyFrom(0, 0, capture.audio, firstChannel, 0, captureSamples);
                        if (secondChannel >= 0)
                        {
                            selectedOutputs.copyFrom(1, 0, capture.audio, secondChannel, 0, captureSamples);
                            const double firstEnergy = channelEnergy[static_cast<size_t>(firstChannel)];
                            const double secondEnergy = channelEnergy[static_cast<size_t>(secondChannel)];
                            if (firstEnergy == 0.0 && secondEnergy > 0.0)
                                selectedOutputs.copyFrom(0, 0, selectedOutputs, 1, 0, captureSamples);
                            else if (secondEnergy == 0.0 && firstEnergy > 0.0)
                                selectedOutputs.copyFrom(1, 0, selectedOutputs, 0, 0, captureSamples);
                        }
                        capture.audio = std::move(selectedOutputs);
                    }

                    double sumSquares = 0.0;
                    juce::int64 measuredSamples = 0;
                    for (int channel = 0; channel < capture.audio.getNumChannels(); ++channel)
                    {
                        const float* samples = capture.audio.getReadPointer(channel);
                        for (int i = 0; i < captureSamples; ++i)
                        {
                            capture.peak = std::max(capture.peak, std::abs(static_cast<double>(samples[i])));
                            sumSquares += static_cast<double>(samples[i]) * samples[i];
                            ++measuredSamples;
                        }
                    }
                    capture.rms = measuredSamples > 0 ? std::sqrt(sumSquares / static_cast<double>(measuredSamples)) : 0.0;
                    capture.silent = capture.peak <= settings.silencePeakThreshold;
                    if (captureConsumer)
                    {
                        if (!captureConsumer(capture))
                        {
                            result.error = "A completed plugin capture could not be saved.";
                            break;
                        }
                    }
                    else
                        result.notes.push_back(std::move(capture));
                    ++completed;
                    if (progress)
                        progress(completed, totalScans, note, velocity);
                }
                if (result.cancelled || result.error.isNotEmpty())
                    break;
            }
            if (result.cancelled || result.error.isNotEmpty())
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
