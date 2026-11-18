#pragma once
#include <JuceHeader.h>
#include "../core/DrumEvent.h"
#include "../core/MidiMapping.h"
#include "../core/SequencerPlayback.h"
#include "DeferredNoteTriggers.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class MagDrumAudioProcessorTestAccess;

class MagDrumAudioProcessor final : public juce::AudioProcessor, private juce::ChangeBroadcaster
{
public:
    struct SampleLayerInfo
    {
        int minimumVelocity = 1;
        int maximumVelocity = 127;
        juce::String path;
        int roundRobinIndex = 1;
        int roundRobinCount = 1;
    };

    struct SampleAssignmentInfo
    {
        int midiNote = 0;
        int minimumVelocity = 1;
        int maximumVelocity = 127;
        juce::String path;
        int roundRobinIndex = 1;
        int roundRobinCount = 1;
    };

    struct NoteParameters
    {
        float gainDb = 0.0f;
        float pan = 0.0f;
        float tuneSemitones = 0.0f;
        float attackMs = 0.0f;
        float decayMs = 0.0f;
        float sustainLevel = 1.0f;
        float releaseMs = 0.0f;
        float filterCutoffHz = 20000.0f;
        float driveDb = 0.0f;
        float eqLowDb = 0.0f;
        float eqMidDb = 0.0f;
        float eqHighDb = 0.0f;
        float sampleStart = 0.0f;
        float sampleEnd = 1.0f;
        float compressorThresholdDb = 0.0f;
        float compressorRatio = 1.0f;
        float compressorAttackMs = 10.0f;
        float compressorReleaseMs = 100.0f;
        float compressorMakeupDb = 0.0f;
        float phaserRateHz = 0.0f;
        float phaserDepth = 0.0f;
        float phaserFeedback = 0.0f;
        float phaserMix = 0.0f;
    };

    MagDrumAudioProcessor();
    ~MagDrumAudioProcessor() override;
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
    double getTailLengthSeconds() const override { return tailLengthSeconds.load(std::memory_order_relaxed); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadSampleForNote(int midiNote, const juce::File&);
    bool loadSampleForVelocityRange(int midiNote, int minimumVelocity, int maximumVelocity, const juce::File&,
                                    const std::function<bool()>& shouldCancel = {});
    bool clearSamplesForNote(int midiNote);
    bool previewSample(const juce::File&, const std::function<bool()>& shouldCancel = {});
    void stopSamplePreview();
    // UI-thread pad/sequencer triggers are queued for the next audio block.
    // The queue is bounded so this call never blocks the host's audio thread.
    void triggerNote(int midiNote, int velocity = 100);
    // Audio-thread note activity is accumulated lock-free and consumed by the
    // editor timer to animate the corresponding drum pad.
    std::uint32_t consumeNoteTriggerCount(int midiNote) noexcept;
    int setSequencerPattern(const std::vector<DrumEvent>& events, const MidiMappingProfile& mapping,
                            double patternLengthBeats);
    void setSequencerPlaybackEnabled(bool enabled) noexcept;
    bool isSequencerHostSynchronized() const noexcept;
    int getSequencerUnmappedEventCount() const noexcept;
    NoteParameters getNoteParameters(int midiNote) const;
    void setNoteParameters(int midiNote, NoteParameters);
    std::vector<SampleLayerInfo> getSampleLayersForNote(int midiNote) const;
    std::vector<SampleAssignmentInfo> getAllSampleAssignments() const;
    juce::String getSamplePathForNote(int midiNote) const;
    void setPluginEditorState(const juce::XmlElement&);
    std::unique_ptr<juce::XmlElement> getPluginEditorState() const;
    void addSampleStateListener(juce::ChangeListener* listener) { addChangeListener(listener); }
    void removeSampleStateListener(juce::ChangeListener* listener) { removeChangeListener(listener); }
    bool isSampleStateChangeSource(const juce::ChangeBroadcaster* source) const noexcept
    {
        return source == static_cast<const juce::ChangeBroadcaster*>(this);
    }

private:
    friend class MagDrumAudioProcessorTestAccess;

    struct SampleData
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        int minimumVelocity = 1;
        int maximumVelocity = 127;
    };
    struct PendingSampleLoad
    {
        int note = 0;
        int minimumVelocity = 1;
        int maximumVelocity = 127;
        juce::String path;
        std::shared_ptr<std::atomic<bool>> cancellation;
    };
    struct Voice
    {
        const SampleData* sample = nullptr;
        double position = 0.0;
        double endPosition = 0.0;
        float gain = 0.0f;
        float leftPanGain = 1.0f;
        float rightPanGain = 1.0f;
        double playbackRate = 1.0;
        bool active = false;
        double elapsedOutputSamples = 0.0;
        double totalOutputSamples = 0.0;
        double attackSamples = 0.0;
        double decaySamples = 0.0;
        float sustainLevel = 1.0f;
        double releaseSamples = 0.0;
        float lowPassCoefficient = 0.0f;
        float lowPassStateLeft = 0.0f;
        float lowPassStateRight = 0.0f;
        float driveGain = 1.0f;
        float eqLowGain = 1.0f, eqMidGain = 1.0f, eqHighGain = 1.0f;
        float eqLowCoefficient = 0.0f, eqHighCoefficient = 0.0f;
        float eqLowStateLeft = 0.0f, eqLowStateRight = 0.0f;
        float eqHighStateLeft = 0.0f, eqHighStateRight = 0.0f;
        float compressorEnvelope = 0.0f;
        float compressorAttackCoefficient = 0.0f, compressorReleaseCoefficient = 0.0f;
        float compressorThresholdDb = 0.0f, compressorRatio = 1.0f, compressorMakeupGain = 1.0f;
        float phaserRateHz = 0.0f, phaserDepth = 0.0f, phaserFeedback = 0.0f, phaserMix = 0.0f;
        double phaserPhase = 0.0;
        float phaserFeedbackLeft = 0.0f, phaserFeedbackRight = 0.0f;
        std::array<float, 4> phaserStateLeft{}, phaserStateRight{};
    };

    static constexpr int maxVoices = 32;
    static constexpr int maxVelocityLayers = 8;
    static constexpr int pendingTriggerCapacity = 256;
    struct PendingNoteTrigger
    {
        int midiNote = 0;
        int velocity = 100;
    };
    juce::AbstractFifo pendingTriggerFifo{pendingTriggerCapacity};
    std::array<PendingNoteTrigger, pendingTriggerCapacity> pendingTriggers{};
    magdrum::plugin::DeferredNoteTriggers deferredNoteTriggers;
    double outputSampleRate = 44100.0;
    std::atomic<double> tailLengthSeconds{0.0};
    juce::AudioFormatManager formats;
    std::array<std::array<std::atomic<const SampleData*>, maxVelocityLayers>, 128> sampleSlots{};
    std::array<std::array<juce::String, maxVelocityLayers>, 128> samplePaths{};
    std::array<std::array<std::unique_ptr<SampleData>, maxVelocityLayers>, 128> ownedSamples{};
    std::array<std::array<std::atomic<std::uint32_t>, maxVelocityLayers>, 128> roundRobinCounters{};
    std::vector<PendingSampleLoad> pendingSampleLoads;
    std::array<std::atomic<float>, 128> noteGainDb{};
    std::array<std::atomic<float>, 128> notePan{};
    std::array<std::atomic<float>, 128> noteTuneSemitones{};
    std::array<std::atomic<float>, 128> noteAttackMs{};
    std::array<std::atomic<float>, 128> noteDecayMs{};
    std::array<std::atomic<float>, 128> noteSustainLevel{};
    std::array<std::atomic<float>, 128> noteReleaseMs{};
    std::array<std::atomic<float>, 128> noteFilterCutoffHz{};
    std::array<std::atomic<float>, 128> noteDriveDb{};
    std::array<std::atomic<float>, 128> noteEqLowDb{}, noteEqMidDb{}, noteEqHighDb{};
    std::array<std::atomic<float>, 128> noteSampleStart{}, noteSampleEnd{};
    std::array<std::atomic<float>, 128> noteCompressorThresholdDb{}, noteCompressorRatio{}, noteCompressorAttackMs{}, noteCompressorReleaseMs{}, noteCompressorMakeupDb{};
    std::array<std::atomic<float>, 128> notePhaserRateHz{}, notePhaserDepth{}, notePhaserFeedback{}, notePhaserMix{};
    std::array<std::atomic<std::uint32_t>, 128> noteTriggerCounts{};
    std::unique_ptr<SampleData> previewData;
    juce::CriticalSection sampleLock;
    juce::CriticalSection stateLock;
    juce::CriticalSection programNameLock;
    juce::String programName{"Default Kit"};
    std::unique_ptr<juce::ThreadPool> sampleStateLoadWorkers;
    juce::CriticalSection sampleStateLoadWorkerLock;
    std::atomic<std::uint64_t> sampleStateLoadGeneration{0};
    juce::CriticalSection editorStateLock;
    juce::CriticalSection sequencerPatternLock;
    std::vector<magdrum::sequencer::PlaybackEvent> sequencerPlaybackEvents;
    double sequencerPatternLengthBeats = 0.0;
    std::atomic<bool> sequencerPlaybackEnabled{false};
    std::atomic<bool> sequencerHostSynchronized{false};
    std::atomic<int> sequencerUnmappedEventCount{0};
    std::unique_ptr<juce::XmlElement> editorState = std::make_unique<juce::XmlElement>("PLUGIN_EDITOR_STATE");
    std::array<Voice, maxVoices> voices{};
    int nextVoice = 0;
    void startNoteVoice(int midiNote, int velocity);
    void recalculateTailLengthSeconds() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessor)
};
