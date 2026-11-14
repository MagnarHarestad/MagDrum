#pragma once
#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

class MagDrumAudioProcessor final : public juce::AudioProcessor
{
public:
    struct SampleLayerInfo
    {
        int minimumVelocity = 1;
        int maximumVelocity = 127;
        juce::String path;
    };

    struct NoteParameters
    {
        float gainDb = 0.0f;
        float pan = 0.0f;
        float tuneSemitones = 0.0f;
    };

    MagDrumAudioProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override;
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
    bool loadSampleForVelocityRange(int midiNote, int minimumVelocity, int maximumVelocity, const juce::File&);
    bool clearSamplesForNote(int midiNote);
    bool previewSample(const juce::File&);
    void triggerNote(int midiNote, int velocity = 100);
    NoteParameters getNoteParameters(int midiNote) const;
    void setNoteParameters(int midiNote, NoteParameters);
    std::vector<SampleLayerInfo> getSampleLayersForNote(int midiNote) const;
    juce::String getSamplePathForNote(int midiNote) const;
    void setPluginEditorState(const juce::XmlElement&);
    std::unique_ptr<juce::XmlElement> getPluginEditorState() const;

private:
    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        int minimumVelocity = 1;
        int maximumVelocity = 127;
    };
    struct Voice
    {
        const SampleData* sample = nullptr;
        double position = 0.0;
        float gain = 0.0f;
        float leftPanGain = 1.0f;
        float rightPanGain = 1.0f;
        double playbackRate = 1.0;
        bool active = false;
    };

    static constexpr int maxVoices = 32;
    static constexpr int maxVelocityLayers = 8;
    double outputSampleRate = 44100.0;
    juce::AudioFormatManager formats;
    std::array<std::array<std::atomic<const SampleData*>, maxVelocityLayers>, 128> sampleSlots{};
    std::array<std::array<juce::String, maxVelocityLayers>, 128> samplePaths{};
    std::array<std::array<std::unique_ptr<SampleData>, maxVelocityLayers>, 128> ownedSamples{};
    std::array<std::atomic<float>, 128> noteGainDb{};
    std::array<std::atomic<float>, 128> notePan{};
    std::array<std::atomic<float>, 128> noteTuneSemitones{};
    std::unique_ptr<SampleData> previewData;
    juce::CriticalSection sampleLock;
    juce::CriticalSection stateLock;
    juce::CriticalSection editorStateLock;
    std::unique_ptr<juce::XmlElement> editorState = std::make_unique<juce::XmlElement>("PLUGIN_EDITOR_STATE");
    std::array<Voice, maxVoices> voices{};
    int nextVoice = 0;
    void startNoteVoice(int midiNote, int velocity);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessor)
};
