#include "plugin/PluginProcessor.h"
#include "core/PluginScanProcess.h"
#include "core/ScopedFileAccess.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <atomic>
#include <limits>
#include <thread>

namespace
{
bool writeFixture(const juce::File& file, float amplitude = 0.25f)
{
    constexpr double sampleRate = 48000.0;
    juce::AudioBuffer<float> audio(1, 960);
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        audio.setSample(0, sample, static_cast<float>(amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * sample / sampleRate)));

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream)
        return false;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

double renderPeak(MagDrumAudioProcessor& processor, int note, int velocity)
{
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(velocity)), 0);
    processor.processBlock(audio, midi);
    if (!midi.isEmpty())
        return -1.0;
    double peak = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            peak = juce::jmax(peak, std::abs(static_cast<double>(audio.getSample(channel, sample))));
    return peak;
}

std::array<double, 2> renderChannelPeaks(MagDrumAudioProcessor& processor, int note, int velocity)
{
    juce::AudioBuffer<float> audio(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(velocity)), 0);
    processor.processBlock(audio, midi);
    std::array<double, 2> peaks{};
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            peaks[static_cast<size_t>(channel)] = juce::jmax(peaks[static_cast<size_t>(channel)],
                std::abs(static_cast<double>(audio.getSample(channel, sample))));
    return peaks;
}

double renderPeakInRange(MagDrumAudioProcessor& processor, int note, int velocity, int start, int length)
{
    juce::AudioBuffer<float> audio(2, start + length);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(velocity)), 0);
    processor.processBlock(audio, midi);
    double peak = 0.0;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int sample = start; sample < start + length; ++sample)
            peak = juce::jmax(peak, std::abs(static_cast<double>(audio.getSample(channel, sample))));
    return peak;
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--magdrum-plugin-scan-worker") == 0)
        return PluginScanProcess::runWorker(juce::File(argv[2]));

    if (argc == 4 && std::strcmp(argv[1], "--magdrum-test-write-plugin-state") == 0)
    {
        MagDrumAudioProcessor fixture;
        if (!fixture.loadSampleForNote(36, juce::File(argv[2]))) return 201;
        juce::MemoryBlock processorState;
        fixture.getStateInformation(processorState);
        if (processorState.isEmpty()) return 202;

        // A host's AudioPluginInstance state wraps processor state in a VST3 envelope.
        juce::XmlElement hostedState("VST3PluginState");
        hostedState.createNewChildElement("IComponent")->addTextElement(processorState.toBase64Encoding());
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary(hostedState, state);
        return !state.isEmpty() && juce::File(argv[3]).replaceWithData(state.getData(), state.getSize()) ? 0 : 203;
    }

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrumPluginSmokeTest");
    root.deleteRecursively();
    if (!root.createDirectory().wasOk()) return 1;

    const auto sample = root.getChildFile("kick.wav");
    if (!writeFixture(sample)) return 2;

    const auto importFolder = root.getChildFile("imports");
    const auto sourceBytes = root.getChildFile("url-source.mid");
    const auto copiedBytes = importFolder.getChildFile("url-copy.mid");
    const auto roundTripBytes = root.getChildFile("url-round-trip.mid");
    const auto filesMatch = [](const juce::File& left, const juce::File& right)
    {
        juce::MemoryBlock leftData, rightData;
        return left.loadFileAsData(leftData) && right.loadFileAsData(rightData)
            && leftData.getSize() == rightData.getSize()
            && leftData.matches(rightData.getData(), rightData.getSize());
    };
    const char midiMarker[] = "MagDrum URL copy fixture";
    {
        std::unique_ptr<juce::OutputStream> stream(sourceBytes.createOutputStream());
        if (stream == nullptr || !stream->write(midiMarker, sizeof(midiMarker))) return 3;
    }
    juce::String copyError;
    if (!importFolder.createDirectory().wasOk() && !importFolder.isDirectory()) return 3;
    if (!copiedBytes.replaceWithText("old destination contents")) return 3;
    if (!MagDrumFiles::copyURLToFile(juce::URL(sourceBytes), copiedBytes, {}, copyError)
        || !filesMatch(sourceBytes, copiedBytes)) return 4;
    if (!MagDrumFiles::copyURLToFile(juce::URL(copiedBytes), copiedBytes, {}, copyError)
        || !filesMatch(sourceBytes, copiedBytes)) return 5;

    const auto preservedDestination = importFolder.getChildFile("preserved.mid");
    if (!preservedDestination.replaceWithText("keep this file on cancellation")) return 6;
    const auto largeImportSource = root.getChildFile("large-import-source.bin");
    juce::MemoryBlock largeImportData(128 * 1024, true);
    if (!largeImportSource.replaceWithData(largeImportData.getData(), largeImportData.getSize())) return 6;
    int cancellationChecks = 0;
    if (MagDrumFiles::copyURLToFile(juce::URL(largeImportSource), preservedDestination,
            [&cancellationChecks] { return ++cancellationChecks >= 3; }, copyError)) return 6;
    juce::MemoryBlock preservedData;
    if (!preservedDestination.loadFileAsData(preservedData)
        || juce::String::fromUTF8(static_cast<const char*>(preservedData.getData()),
                                 static_cast<int>(preservedData.getSize())) != "keep this file on cancellation") return 6;

    const auto cancelledCopy = importFolder.getChildFile("cancelled.mid");
    if (MagDrumFiles::copyURLToFile(juce::URL(sourceBytes), cancelledCopy, [] { return true; }, copyError)
        || cancelledCopy.existsAsFile()) return 7;
    if (!MagDrumFiles::copyFileToURL(copiedBytes, juce::URL(roundTripBytes), copyError)
        || !filesMatch(sourceBytes, roundTripBytes)) return 8;

    MagDrumAudioProcessor processor;
    if (!processor.loadSampleForNote(36, sample) || processor.getSamplePathForNote(36) != sample.getFullPathName()) return 8;
    if (processor.loadSampleForNote(-1, sample) || processor.loadSampleForNote(128, sample)
        || processor.loadSampleForNote(37, root.getChildFile("missing.wav"))) return 9;
    if (processor.getSamplePathForNote(37).isNotEmpty()) return 10;

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    if (state.isEmpty()) return 11;

    MagDrumAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (restored.getSamplePathForNote(36) != sample.getFullPathName()) return 12;
    restored.prepareToPlay(44100.0, 128);
    const double fullVelocityPeak = renderPeak(restored, 36, 127);
    MagDrumAudioProcessor lowVelocity;
    lowVelocity.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    lowVelocity.prepareToPlay(44100.0, 128);
    const double lowVelocityPeak = renderPeak(lowVelocity, 36, 64);
    if (fullVelocityPeak <= 0.05 || fullVelocityPeak > 0.26 || lowVelocityPeak <= 0.0
        || lowVelocityPeak >= fullVelocityPeak || std::abs(lowVelocityPeak / fullVelocityPeak - 64.0 / 127.0) > 0.03)
    {
        std::cerr << "Velocity render check failed: full=" << fullVelocityPeak << ", low=" << lowVelocityPeak
                  << ", ratio=" << (fullVelocityPeak > 0.0 ? lowVelocityPeak / fullVelocityPeak : 0.0) << '\n';
        return 13;
    }

    MagDrumAudioProcessor queuedTrigger;
    if (!queuedTrigger.loadSampleForNote(36, sample)) return 32;
    queuedTrigger.prepareToPlay(44100.0, 128);
    queuedTrigger.triggerNote(36, 127);
    juce::AudioBuffer<float> queuedAudio(2, 128);
    juce::MidiBuffer queuedMidi;
    queuedTrigger.processBlock(queuedAudio, queuedMidi);
    double queuedPeak = 0.0;
    for (int channel = 0; channel < queuedAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < queuedAudio.getNumSamples(); ++sampleIndex)
            queuedPeak = juce::jmax(queuedPeak, std::abs(static_cast<double>(queuedAudio.getSample(channel, sampleIndex))));
    if (queuedPeak <= 0.05 || !queuedMidi.isEmpty())
    {
        std::cerr << "Queued UI trigger render check failed: peak=" << queuedPeak << '\n';
        return 33;
    }

    MagDrumAudioProcessor controlStateSource;
    if (!controlStateSource.loadSampleForNote(36, sample)) return 22;
    controlStateSource.setNoteParameters(36, {-6.0f, -1.0f, 12.0f});
    controlStateSource.setNoteParameters(37, {99.0f, -99.0f, 99.0f});
    const auto clampedParameters = controlStateSource.getNoteParameters(37);
    if (std::abs(clampedParameters.gainDb - 6.0f) > 0.001f
        || std::abs(clampedParameters.pan + 1.0f) > 0.001f
        || std::abs(clampedParameters.tuneSemitones - 24.0f) > 0.001f) return 23;
    juce::MemoryBlock controlState;
    controlStateSource.getStateInformation(controlState);
    MagDrumAudioProcessor controlStateRestored;
    controlStateRestored.setStateInformation(controlState.getData(), static_cast<int>(controlState.getSize()));
    const auto restoredParameters = controlStateRestored.getNoteParameters(36);
    if (std::abs(restoredParameters.gainDb + 6.0f) > 0.001f
        || std::abs(restoredParameters.pan + 1.0f) > 0.001f
        || std::abs(restoredParameters.tuneSemitones - 12.0f) > 0.001f
        || std::abs(controlStateRestored.getNoteParameters(37).gainDb - 6.0f) > 0.001f) return 24;
    controlStateSource.setNoteParameters(38, {std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity(),
                                               -std::numeric_limits<float>::infinity()});
    const auto finiteParameters = controlStateSource.getNoteParameters(38);
    if (std::abs(finiteParameters.gainDb) > 0.001f || std::abs(finiteParameters.pan) > 0.001f
        || std::abs(finiteParameters.tuneSemitones) > 0.001f) return 31;

    MagDrumAudioProcessor gainReference, gainReduced;
    if (!gainReference.loadSampleForNote(36, sample) || !gainReduced.loadSampleForNote(36, sample)) return 25;
    gainReference.prepareToPlay(44100.0, 128);
    gainReduced.prepareToPlay(44100.0, 128);
    gainReduced.setNoteParameters(36, {-6.0f, 0.0f, 0.0f});
    const double unattenuatedPeak = renderPeak(gainReference, 36, 127);
    const double attenuatedPeak = renderPeak(gainReduced, 36, 127);
    if (unattenuatedPeak <= 0.05 || std::abs(attenuatedPeak / unattenuatedPeak - 0.501187) > 0.02) return 26;

    MagDrumAudioProcessor leftPan, rightPan;
    if (!leftPan.loadSampleForNote(36, sample) || !rightPan.loadSampleForNote(36, sample)) return 27;
    leftPan.prepareToPlay(44100.0, 128);
    rightPan.prepareToPlay(44100.0, 128);
    leftPan.setNoteParameters(36, {0.0f, -1.0f, 0.0f});
    rightPan.setNoteParameters(36, {0.0f, 1.0f, 0.0f});
    const auto leftPeaks = renderChannelPeaks(leftPan, 36, 127);
    const auto rightPeaks = renderChannelPeaks(rightPan, 36, 127);
    if (leftPeaks[0] < 0.05 || leftPeaks[1] > 0.001
        || rightPeaks[1] < 0.05 || rightPeaks[0] > 0.001) return 28;

    MagDrumAudioProcessor octaveUp;
    if (!octaveUp.loadSampleForNote(36, sample)) return 29;
    octaveUp.prepareToPlay(44100.0, 128);
    octaveUp.setNoteParameters(36, {0.0f, 0.0f, 12.0f});
    if (renderPeakInRange(octaveUp, 36, 127, 500, 100) > 0.0) return 30;

    const auto softLayer = root.getChildFile("kick-soft.wav");
    const auto hardLayer = root.getChildFile("kick-hard.wav");
    if (!writeFixture(softLayer, 0.10f) || !writeFixture(hardLayer, 0.22f)) return 15;
    MagDrumAudioProcessor layered;
    if (!layered.loadSampleForVelocityRange(36, 64, 127, hardLayer)
        || !layered.loadSampleForVelocityRange(36, 1, 63, softLayer)
        || layered.loadSampleForVelocityRange(36, 50, 80, sample)
        || layered.loadSampleForNote(36, sample)
        || layered.getSamplePathForNote(36) != softLayer.getFullPathName()) return 16;
    juce::MemoryBlock layeredState;
    layered.getStateInformation(layeredState);
    MagDrumAudioProcessor restoredLayers;
    restoredLayers.setStateInformation(layeredState.getData(), static_cast<int>(layeredState.getSize()));
    const auto restoredLayerInfo = restoredLayers.getSampleLayersForNote(36);
    if (restoredLayers.getSamplePathForNote(36) != softLayer.getFullPathName() || restoredLayerInfo.size() != 2
        || restoredLayerInfo[0].minimumVelocity != 1 || restoredLayerInfo[0].maximumVelocity != 63
        || restoredLayerInfo[1].minimumVelocity != 64 || restoredLayerInfo[1].maximumVelocity != 127) return 17;
    restoredLayers.prepareToPlay(44100.0, 128);
    const double softPeak = renderPeak(restoredLayers, 36, 32);
    const double hardPeak = renderPeak(restoredLayers, 36, 96);
    if (softPeak <= 0.0 || hardPeak <= softPeak * 3.0) return 18;
    if (!restoredLayers.clearSamplesForNote(36) || restoredLayers.clearSamplesForNote(36)
        || restoredLayers.getSamplePathForNote(36).isNotEmpty() || renderPeak(restoredLayers, 36, 96) != 0.0) return 19;

    MagDrumAudioProcessor concurrentSwaps;
    if (!concurrentSwaps.loadSampleForNote(36, sample)) return 20;
    concurrentSwaps.prepareToPlay(44100.0, 64);
    std::atomic<bool> inspectLayers{true}, layersWereValid{true};
    std::thread layerReader([&]
    {
        while (inspectLayers.load(std::memory_order_acquire))
        {
            const auto layers = concurrentSwaps.getSampleLayersForNote(36);
            for (const auto& layer : layers)
                if (layer.minimumVelocity < 1 || layer.maximumVelocity > 127
                    || layer.minimumVelocity > layer.maximumVelocity || layer.path.isEmpty())
                    layersWereValid.store(false, std::memory_order_release);
        }
    });
    std::thread audioCallback([&]
    {
        juce::AudioBuffer<float> block(2, 64);
        juce::MidiBuffer events;
        while (inspectLayers.load(std::memory_order_acquire))
        {
            events.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(96)), 0);
            concurrentSwaps.processBlock(block, events);
            events.clear();
        }
    });
    bool swapsSucceeded = true;
    for (int iteration = 0; iteration < 24; ++iteration)
        swapsSucceeded = concurrentSwaps.clearSamplesForNote(36)
            && concurrentSwaps.loadSampleForNote(36, sample) && swapsSucceeded;
    inspectLayers.store(false, std::memory_order_release);
    layerReader.join();
    audioCallback.join();
    if (!swapsSucceeded || !layersWereValid.load(std::memory_order_acquire)) return 21;

    MagDrumAudioProcessor emptyNote;
    emptyNote.prepareToPlay(44100.0, 128);
    if (renderPeak(emptyNote, 37, 100) != 0.0) return 14;

    restored.releaseResources();
    queuedTrigger.releaseResources();
    lowVelocity.releaseResources();
    restoredLayers.releaseResources();
    controlStateSource.releaseResources();
    controlStateRestored.releaseResources();
    gainReference.releaseResources();
    gainReduced.releaseResources();
    leftPan.releaseResources();
    rightPan.releaseResources();
    octaveUp.releaseResources();
    emptyNote.releaseResources();
    root.deleteRecursively();
    std::cout << "Scoped file copy, cancellation, MIDI playback, velocity, pad gain/pan/tune, state restoration and empty-note checks passed.\n";
    return 0;
}
