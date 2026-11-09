#include "plugin/PluginProcessor.h"
#include <cmath>
#include <iostream>

namespace
{
bool writeFixture(const juce::File& file)
{
    constexpr double sampleRate = 48000.0;
    juce::AudioBuffer<float> audio(1, 960);
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        audio.setSample(0, sample, static_cast<float>(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * sample / sampleRate)));

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

    MagDrumAudioProcessor processor;
    if (!processor.loadSampleForNote(36, sample) || processor.getSamplePathForNote(36) != sample.getFullPathName()) return 3;
    if (processor.loadSampleForNote(-1, sample) || processor.loadSampleForNote(128, sample)
        || processor.loadSampleForNote(37, root.getChildFile("missing.wav"))) return 4;
    if (processor.getSamplePathForNote(37).isNotEmpty()) return 5;

    juce::MemoryBlock state;
    processor.getStateInformation(state);
    if (state.isEmpty()) return 6;

    MagDrumAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (restored.getSamplePathForNote(36) != sample.getFullPathName()) return 7;
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
        return 8;
    }
    MagDrumAudioProcessor emptyNote;
    emptyNote.prepareToPlay(44100.0, 128);
    if (renderPeak(emptyNote, 37, 100) != 0.0) return 9;

    restored.releaseResources();
    lowVelocity.releaseResources();
    emptyNote.releaseResources();
    root.deleteRecursively();
    std::cout << "Plugin sample load, state restore, MIDI playback, velocity and empty-note checks passed.\n";
    return 0;
}
