#include "plugin/PluginProcessor.h"
#include "core/ScopedFileAccess.h"
#include <cmath>
#include <iostream>

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
}

int main()
{
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
    if (!MagDrumFiles::copyURLToFile(juce::URL(sourceBytes), copiedBytes, {}, copyError)
        || !filesMatch(sourceBytes, copiedBytes)) return 4;
    if (!MagDrumFiles::copyURLToFile(juce::URL(copiedBytes), copiedBytes, {}, copyError)
        || !filesMatch(sourceBytes, copiedBytes)) return 5;
    const auto cancelledCopy = importFolder.getChildFile("cancelled.mid");
    if (MagDrumFiles::copyURLToFile(juce::URL(sourceBytes), cancelledCopy, [] { return true; }, copyError)
        || cancelledCopy.existsAsFile()) return 6;
    if (!MagDrumFiles::copyFileToURL(copiedBytes, juce::URL(roundTripBytes), copyError)
        || !filesMatch(sourceBytes, roundTripBytes)) return 7;

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

    MagDrumAudioProcessor emptyNote;
    emptyNote.prepareToPlay(44100.0, 128);
    if (renderPeak(emptyNote, 37, 100) != 0.0) return 14;

    restored.releaseResources();
    lowVelocity.releaseResources();
    restoredLayers.releaseResources();
    emptyNote.releaseResources();
    root.deleteRecursively();
    std::cout << "Scoped file copy, cancellation, MIDI playback, velocity and empty-note checks passed.\n";
    return 0;
}
