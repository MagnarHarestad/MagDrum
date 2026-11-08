#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

class MagDrumAudioProcessor final : public juce::AudioProcessor
{
public:
    MagDrumAudioProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MagDrum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadSampleForNote(int midiNote, const juce::File&);
    juce::String getSamplePathForNote(int midiNote) const;

private:
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
    };
    struct Voice
    {
        const SampleData* sample = nullptr;
        double position = 0.0;
        float gain = 0.0f;
        bool active = false;
    };

    static constexpr int maxVoices = 32;
    double outputSampleRate = 44100.0;
    juce::AudioFormatManager formats;
    std::array<std::atomic<const SampleData*>, 128> sampleSlots{};
    std::array<juce::String, 128> samplePaths{};
    std::vector<std::unique_ptr<SampleData>> ownedSamples;
    juce::CriticalSection stateLock;
    std::array<Voice, maxVoices> voices{};
    int nextVoice = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessor)
};
