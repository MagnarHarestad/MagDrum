#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../core/SequencerGrid.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace
{
constexpr std::array<DrumInstrument, 8> sequencerLaneInstruments{
    DrumInstrument::Kick, DrumInstrument::Snare, DrumInstrument::HiHat, DrumInstrument::HiHat,
    DrumInstrument::Tom, DrumInstrument::Ride, DrumInstrument::Crash, DrumInstrument::Clap};
constexpr std::array<DrumArticulation, 8> sequencerLaneArticulations{
    DrumArticulation::Default, DrumArticulation::Default, DrumArticulation::Closed, DrumArticulation::Open,
    DrumArticulation::Low, DrumArticulation::Bow, DrumArticulation::Default, DrumArticulation::Default};

class PluginStateSampleLoadJob final : public juce::ThreadPoolJob
{
public:
    explicit PluginStateSampleLoadJob(std::function<void()> task)
        : ThreadPoolJob("MagDrum restore project samples"), work(std::move(task)) {}

    JobStatus runJob() override
    {
        if (!shouldExit() && work)
            work();
        return jobHasFinished;
    }

private:
    std::function<void()> work;
};
}

MagDrumAudioProcessor::MagDrumAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    for (auto& noteSlots : sampleSlots)
        for (auto& slot : noteSlots)
            slot.store(nullptr);
    for (size_t note = 0; note < noteGainDb.size(); ++note)
    {
        noteGainDb[note].store(0.0f, std::memory_order_relaxed);
        notePan[note].store(0.0f, std::memory_order_relaxed);
        noteTuneSemitones[note].store(0.0f, std::memory_order_relaxed);
        noteAttackMs[note].store(0.0f, std::memory_order_relaxed);
        noteReleaseMs[note].store(0.0f, std::memory_order_relaxed);
        noteFilterCutoffHz[note].store(20000.0f, std::memory_order_relaxed);
    }
    for (auto& noteCounters : roundRobinCounters)
        for (auto& counter : noteCounters)
            counter.store(0, std::memory_order_relaxed);
}

MagDrumAudioProcessor::~MagDrumAudioProcessor()
{
    sampleStateLoadGeneration.fetch_add(1, std::memory_order_acq_rel);
    {
        const juce::ScopedLock lock(stateLock);
        for (auto& layer : pendingSampleLoads)
            if (layer.cancellation != nullptr)
                layer.cancellation->store(false, std::memory_order_release);
    }
    const juce::ScopedLock workerLock(sampleStateLoadWorkerLock);
    if (sampleStateLoadWorkers != nullptr)
        sampleStateLoadWorkers->removeAllJobs(true, -1);
}

void MagDrumAudioProcessor::recalculateTailLengthSeconds() noexcept
{
    double longestSampleSeconds = previewData != nullptr && previewData->sampleRate > 0.0
        ? previewData->audio.getNumSamples() / previewData->sampleRate : 0.0;
    for (const auto& noteLayers : ownedSamples)
        for (const auto& sample : noteLayers)
            if (sample != nullptr && sample->sampleRate > 0.0)
                longestSampleSeconds = juce::jmax(longestSampleSeconds,
                    sample->audio.getNumSamples() / sample->sampleRate);
    tailLengthSeconds.store(longestSampleSeconds, std::memory_order_relaxed);
}

void MagDrumAudioProcessor::prepareToPlay(double rate, int)
{
    outputSampleRate = rate > 0.0 ? rate : 44100.0;
}

void MagDrumAudioProcessor::releaseResources()
{
    const juce::ScopedLock lock(sampleLock);
    for (auto& voice : voices) voice = {};
}

bool MagDrumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

bool MagDrumAudioProcessor::loadSampleForNote(int note, const juce::File& file)
{
    return loadSampleForVelocityRange(note, 1, 127, file);
}

bool MagDrumAudioProcessor::loadSampleForVelocityRange(int note, int minimumVelocity, int maximumVelocity,
    const juce::File& file, const std::function<bool()>& shouldCancel)
{
    if ((shouldCancel && shouldCancel()) || note < 0 || note > 127 || minimumVelocity < 1 || maximumVelocity > 127
        || minimumVelocity > maximumVelocity || !file.existsAsFile()) return false;
    juce::AudioFormatManager sampleFormats;
    sampleFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(sampleFormats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0) return false;
    constexpr double maxSampleSeconds = 600.0;
    if (reader->lengthInSamples > reader->sampleRate * maxSampleSeconds || reader->lengthInSamples > std::numeric_limits<int>::max()) return false;

    auto sample = std::make_unique<SampleData>();
    sample->sampleRate = reader->sampleRate;
    sample->minimumVelocity = minimumVelocity;
    sample->maximumVelocity = maximumVelocity;
    const int channels = static_cast<int>(juce::jmin(2u, reader->numChannels));
    const int sampleCount = static_cast<int>(reader->lengthInSamples);
    sample->audio.setSize(channels, sampleCount);
    constexpr int sampleChunkSize = 16384;
    for (int offset = 0; offset < sampleCount; offset += sampleChunkSize)
    {
        if (shouldCancel && shouldCancel()) return false;
        const int count = juce::jmin(sampleChunkSize, sampleCount - offset);
        if (!reader->read(&sample->audio, offset, count, offset, true, true)) return false;
    }

    std::unique_ptr<SampleData> retiredSample;
    if (shouldCancel && shouldCancel()) return false;
    {
        const juce::ScopedLock sampleGuard(sampleLock);
        const juce::ScopedLock stateGuard(stateLock);
        if (shouldCancel && shouldCancel()) return false;
        int targetLayer = -1;
        int roundRobinGroup = -1;
        for (int layer = 0; layer < maxVelocityLayers; ++layer)
        {
            const auto* existing = ownedSamples[static_cast<size_t>(note)][static_cast<size_t>(layer)].get();
            if (existing == nullptr)
            {
                if (targetLayer < 0)
                    targetLayer = layer;
                continue;
            }

            const bool sameRange = existing->minimumVelocity == minimumVelocity
                && existing->maximumVelocity == maximumVelocity;
            if (sameRange)
            {
                if (roundRobinGroup < 0)
                    roundRobinGroup = layer;
                if (samplePaths[static_cast<size_t>(note)][static_cast<size_t>(layer)] == file.getFullPathName())
                    return true;
                continue;
            }
            const bool overlaps = minimumVelocity <= existing->maximumVelocity
                && maximumVelocity >= existing->minimumVelocity;
            if (overlaps)
                return false;
        }
        if (targetLayer < 0)
            return false;

        auto& oldSample = ownedSamples[static_cast<size_t>(note)][static_cast<size_t>(targetLayer)];
        const auto* oldPointer = oldSample.get();
        if (oldPointer != nullptr)
            for (auto& voice : voices)
                if (voice.sample == oldPointer)
                    voice = {};
        retiredSample = std::move(oldSample);
        oldSample = std::move(sample);
        sampleSlots[static_cast<size_t>(note)][static_cast<size_t>(targetLayer)].store(oldSample.get(), std::memory_order_release);
        samplePaths[static_cast<size_t>(note)][static_cast<size_t>(targetLayer)] = file.getFullPathName();
        if (roundRobinGroup < 0)
            roundRobinGroup = targetLayer;
        roundRobinCounters[static_cast<size_t>(note)][static_cast<size_t>(roundRobinGroup)]
            .store(0, std::memory_order_relaxed);
        recalculateTailLengthSeconds();
        for (auto it = pendingSampleLoads.begin(); it != pendingSampleLoads.end();)
        {
            if (it->note == note && it->minimumVelocity == minimumVelocity
                && it->maximumVelocity == maximumVelocity && it->path == file.getFullPathName())
            {
                if (it->cancellation != nullptr)
                    it->cancellation->store(false, std::memory_order_release);
                it = pendingSampleLoads.erase(it);
            }
            else
                ++it;
        }
    }
    return true;
}

bool MagDrumAudioProcessor::clearSamplesForNote(int note)
{
    if (note < 0 || note > 127)
        return false;
    bool removedAny = false;
    std::array<std::unique_ptr<SampleData>, maxVelocityLayers> retiredSamples;
    const juce::ScopedLock sampleGuard(sampleLock);
    const juce::ScopedLock stateGuard(stateLock);
    for (int layer = 0; layer < maxVelocityLayers; ++layer)
    {
        auto& owned = ownedSamples[static_cast<size_t>(note)][static_cast<size_t>(layer)];
        const auto* oldPointer = owned.get();
        if (oldPointer != nullptr)
        {
            removedAny = true;
            for (auto& voice : voices)
                if (voice.sample == oldPointer)
                    voice = {};
        }
        sampleSlots[static_cast<size_t>(note)][static_cast<size_t>(layer)].store(nullptr, std::memory_order_release);
        retiredSamples[static_cast<size_t>(layer)] = std::move(owned);
    }
    for (auto& path : samplePaths[static_cast<size_t>(note)])
        path.clear();
    for (auto& counter : roundRobinCounters[static_cast<size_t>(note)])
        counter.store(0, std::memory_order_relaxed);
    recalculateTailLengthSeconds();
    for (auto it = pendingSampleLoads.begin(); it != pendingSampleLoads.end();)
    {
        if (it->note == note)
        {
            if (it->cancellation != nullptr)
                it->cancellation->store(false, std::memory_order_release);
            it = pendingSampleLoads.erase(it);
        }
        else
            ++it;
    }
    return removedAny;
}

bool MagDrumAudioProcessor::previewSample(const juce::File& file, const std::function<bool()>& shouldCancel)
{
    if ((shouldCancel && shouldCancel()) || !file.existsAsFile()) return false;
    juce::AudioFormatManager previewFormats;
    previewFormats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(previewFormats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > reader->sampleRate * 600.0
        || reader->lengthInSamples > std::numeric_limits<int>::max()) return false;
    auto sample = std::make_unique<SampleData>();
    sample->sampleRate = reader->sampleRate;
    sample->audio.setSize(static_cast<int>(juce::jmin(2u, reader->numChannels)), static_cast<int>(reader->lengthInSamples));
    constexpr int previewChunkSize = 16384;
    for (int offset = 0; offset < sample->audio.getNumSamples(); offset += previewChunkSize)
    {
        if (shouldCancel && shouldCancel()) return false;
        const int count = juce::jmin(previewChunkSize, sample->audio.getNumSamples() - offset);
        if (!reader->read(&sample->audio, offset, count, offset, true, true)) return false;
    }
    std::unique_ptr<SampleData> retiredPreview;
    const juce::ScopedLock lock(sampleLock);
    if (shouldCancel && shouldCancel()) return false;
    const auto* old = previewData.get();
    if (old != nullptr)
        for (auto& voice : voices) if (voice.sample == old) voice = {};
    retiredPreview = std::move(previewData);
    previewData = std::move(sample);
    recalculateTailLengthSeconds();
    auto& voice = voices[static_cast<size_t>(nextVoice++ % maxVoices)];
    voice = {previewData.get(), 0.0, 0.85f, 1.0f, 1.0f, 1.0, true};
    return true;
}

void MagDrumAudioProcessor::stopSamplePreview()
{
    std::unique_ptr<SampleData> retiredPreview;
    const juce::ScopedLock lock(sampleLock);
    if (previewData == nullptr)
        return;

    const auto* preview = previewData.get();
    for (auto& voice : voices)
        if (voice.sample == preview)
            voice = {};
    retiredPreview = std::move(previewData);
    recalculateTailLengthSeconds();
}

void MagDrumAudioProcessor::triggerNote(int note, int velocity)
{
    if (note < 0 || note > 127)
        return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingTriggerFifo.prepareToWrite(1, start1, size1, start2, size2);
    const int writeCount = size1 + size2;
    if (size1 > 0)
        pendingTriggers[static_cast<size_t>(start1)] = {note, juce::jlimit(1, 127, velocity)};
    else if (size2 > 0)
        pendingTriggers[static_cast<size_t>(start2)] = {note, juce::jlimit(1, 127, velocity)};
    if (writeCount > 0)
        pendingTriggerFifo.finishedWrite(writeCount);
}

int MagDrumAudioProcessor::setSequencerPattern(const std::vector<DrumEvent>& events,
                                                const MidiMappingProfile& mapping,
                                                double patternLengthBeats)
{
    std::vector<magdrum::sequencer::PlaybackEvent> prepared;
    int unmappedCount = 0;
    if (std::isfinite(patternLengthBeats) && patternLengthBeats > 0.0)
    {
        prepared.reserve(events.size());
        for (const auto& event : events)
        {
            if (!std::isfinite(event.positionBeats) || event.positionBeats < 0.0
                || event.positionBeats >= patternLengthBeats)
                continue;
            const auto mapped = mapping.toMidi(event, false);
            if (!mapped.note)
            {
                ++unmappedCount;
                continue;
            }
            prepared.push_back({event.positionBeats, static_cast<int>(*mapped.note), event.velocity});
        }
        std::stable_sort(prepared.begin(), prepared.end(), [](const auto& left, const auto& right)
        {
            return left.positionBeats < right.positionBeats;
        });
    }

    const juce::ScopedLock lock(sequencerPatternLock);
    sequencerPlaybackEvents.swap(prepared);
    sequencerPatternLengthBeats = std::isfinite(patternLengthBeats) && patternLengthBeats > 0.0
        ? patternLengthBeats : 0.0;
    sequencerUnmappedEventCount.store(unmappedCount, std::memory_order_release);
    return unmappedCount;
}

void MagDrumAudioProcessor::setSequencerPlaybackEnabled(bool enabled) noexcept
{
    sequencerPlaybackEnabled.store(enabled, std::memory_order_release);
}

bool MagDrumAudioProcessor::isSequencerHostSynchronized() const noexcept
{
    return sequencerHostSynchronized.load(std::memory_order_acquire);
}

int MagDrumAudioProcessor::getSequencerUnmappedEventCount() const noexcept
{
    return sequencerUnmappedEventCount.load(std::memory_order_acquire);
}

MagDrumAudioProcessor::NoteParameters MagDrumAudioProcessor::getNoteParameters(int note) const
{
    if (note < 0 || note > 127)
        return {};
    const auto index = static_cast<size_t>(note);
    return {noteGainDb[index].load(std::memory_order_relaxed),
            notePan[index].load(std::memory_order_relaxed),
            noteTuneSemitones[index].load(std::memory_order_relaxed),
            noteAttackMs[index].load(std::memory_order_relaxed),
            noteReleaseMs[index].load(std::memory_order_relaxed),
            noteFilterCutoffHz[index].load(std::memory_order_relaxed)};
}

void MagDrumAudioProcessor::setNoteParameters(int note, NoteParameters parameters)
{
    if (note < 0 || note > 127)
        return;
    const auto index = static_cast<size_t>(note);
    const auto finiteOrZero = [](float value) { return std::isfinite(value) ? value : 0.0f; };
    noteGainDb[index].store(juce::jlimit(-60.0f, 6.0f, finiteOrZero(parameters.gainDb)), std::memory_order_relaxed);
    notePan[index].store(juce::jlimit(-1.0f, 1.0f, finiteOrZero(parameters.pan)), std::memory_order_relaxed);
    noteTuneSemitones[index].store(juce::jlimit(-24.0f, 24.0f, finiteOrZero(parameters.tuneSemitones)), std::memory_order_relaxed);
    noteAttackMs[index].store(juce::jlimit(0.0f, 1000.0f, finiteOrZero(parameters.attackMs)), std::memory_order_relaxed);
    noteReleaseMs[index].store(juce::jlimit(0.0f, 5000.0f, finiteOrZero(parameters.releaseMs)), std::memory_order_relaxed);
    const float cutoff = std::isfinite(parameters.filterCutoffHz) ? parameters.filterCutoffHz : 20000.0f;
    noteFilterCutoffHz[index].store(juce::jlimit(20.0f, 20000.0f, cutoff), std::memory_order_relaxed);
}

void MagDrumAudioProcessor::startNoteVoice(int note, int velocity)
{
    const int clampedVelocity = juce::jlimit(1, 127, velocity);
    const SampleData* sample = nullptr;
    std::array<const SampleData*, maxVelocityLayers> matchingSamples{};
    int matchingCount = 0;
    int firstMatchingLayer = -1;
    for (int layer = 0; layer < maxVelocityLayers; ++layer)
    {
        const auto* candidate = sampleSlots[static_cast<size_t>(note)][static_cast<size_t>(layer)]
            .load(std::memory_order_acquire);
        if (candidate != nullptr && clampedVelocity >= candidate->minimumVelocity && clampedVelocity <= candidate->maximumVelocity)
        {
            if (firstMatchingLayer < 0)
                firstMatchingLayer = layer;
            matchingSamples[static_cast<size_t>(matchingCount++)] = candidate;
        }
    }
    if (matchingCount > 0)
    {
        const auto roundRobinIndex = roundRobinCounters[static_cast<size_t>(note)][static_cast<size_t>(firstMatchingLayer)]
            .fetch_add(1, std::memory_order_relaxed)
            % static_cast<std::uint32_t>(matchingCount);
        sample = matchingSamples[static_cast<size_t>(roundRobinIndex)];
    }
    if (sample != nullptr && sample->audio.getNumSamples() > 0)
    {
        const auto parameters = getNoteParameters(note);
        const float pan = parameters.pan;
        float leftPanGain = 1.0f, rightPanGain = 1.0f;
        if (sample->audio.getNumChannels() == 1)
        {
            const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            leftPanGain = std::cos(angle);
            rightPanGain = std::sin(angle);
        }
        else
        {
            if (pan > 0.0f) leftPanGain = 1.0f - pan;
            else if (pan < 0.0f) rightPanGain = 1.0f + pan;
        }
        const float gain = (clampedVelocity / 127.0f) * juce::Decibels::decibelsToGain(parameters.gainDb);
        const double playbackRate = std::exp2(static_cast<double>(parameters.tuneSemitones) / 12.0);
        auto& voice = voices[static_cast<size_t>(nextVoice++ % maxVoices)];
        voice = {sample, 0.0, gain, leftPanGain, rightPanGain, playbackRate, true};
        const double sourceAdvance = (sample->sampleRate / juce::jmax(1.0, outputSampleRate)) * playbackRate;
        voice.totalOutputSamples = sample->audio.getNumSamples() / juce::jmax(1.0e-9, sourceAdvance);
        voice.attackSamples = parameters.attackMs * 0.001 * outputSampleRate;
        voice.releaseSamples = parameters.releaseMs * 0.001 * outputSampleRate;
        const float cutoff = juce::jmin(parameters.filterCutoffHz, static_cast<float>(outputSampleRate * 0.49));
        voice.lowPassCoefficient = cutoff >= outputSampleRate * 0.49
            ? 0.0f : std::exp(-2.0f * juce::MathConstants<float>::pi * cutoff / static_cast<float>(outputSampleRate));
    }
}

juce::String MagDrumAudioProcessor::getSamplePathForNote(int note) const
{
    const auto layers = getSampleLayersForNote(note);
    return layers.empty() ? juce::String{} : layers.front().path;
}

std::vector<MagDrumAudioProcessor::SampleLayerInfo> MagDrumAudioProcessor::getSampleLayersForNote(int note) const
{
    if (note < 0 || note > 127)
        return {};
    const juce::ScopedLock lock(stateLock);
    const auto noteIndex = static_cast<size_t>(note);
    std::vector<SampleLayerInfo> layers;
    for (int layer = 0; layer < maxVelocityLayers; ++layer)
    {
        const auto layerIndex = static_cast<size_t>(layer);
        if (samplePaths[noteIndex][layerIndex].isEmpty() || ownedSamples[noteIndex][layerIndex] == nullptr)
            continue;
        const auto& sample = ownedSamples[noteIndex][layerIndex];
        layers.push_back({sample->minimumVelocity, sample->maximumVelocity, samplePaths[noteIndex][layerIndex], 1, 1});
    }
    std::stable_sort(layers.begin(), layers.end(), [](const auto& left, const auto& right)
    {
        if (left.minimumVelocity != right.minimumVelocity)
            return left.minimumVelocity < right.minimumVelocity;
        return left.maximumVelocity < right.maximumVelocity;
    });
    for (size_t first = 0; first < layers.size();)
    {
        size_t end = first + 1;
        while (end < layers.size() && layers[end].minimumVelocity == layers[first].minimumVelocity
            && layers[end].maximumVelocity == layers[first].maximumVelocity)
            ++end;
        const int groupSize = static_cast<int>(end - first);
        for (size_t index = first; index < end; ++index)
        {
            layers[index].roundRobinIndex = static_cast<int>(index - first) + 1;
            layers[index].roundRobinCount = groupSize;
        }
        first = end;
    }
    return layers;
}

std::vector<MagDrumAudioProcessor::SampleAssignmentInfo> MagDrumAudioProcessor::getAllSampleAssignments() const
{
    const juce::ScopedLock lock(stateLock);
    std::vector<SampleAssignmentInfo> assignments;
    for (int note = 0; note < 128; ++note)
        for (int layer = 0; layer < maxVelocityLayers; ++layer)
        {
            const auto noteIndex = static_cast<size_t>(note);
            const auto layerIndex = static_cast<size_t>(layer);
            const auto& path = samplePaths[noteIndex][layerIndex];
            if (path.isNotEmpty())
            assignments.push_back({note, ownedSamples[noteIndex][layerIndex]->minimumVelocity,
                    ownedSamples[noteIndex][layerIndex]->maximumVelocity, path});
        }
    for (const auto& pending : pendingSampleLoads)
        assignments.push_back({pending.note, pending.minimumVelocity, pending.maximumVelocity, pending.path});
    for (size_t index = 0; index < assignments.size(); ++index)
    {
        auto& assignment = assignments[index];
        int groupCount = 0;
        int groupIndex = 0;
        for (size_t candidate = 0; candidate < assignments.size(); ++candidate)
        {
            const auto& other = assignments[candidate];
            if (other.midiNote == assignment.midiNote
                && other.minimumVelocity == assignment.minimumVelocity
                && other.maximumVelocity == assignment.maximumVelocity)
            {
                ++groupCount;
                if (candidate < index)
                    ++groupIndex;
            }
        }
        assignment.roundRobinIndex = groupIndex + 1;
        assignment.roundRobinCount = groupCount;
    }
    return assignments;
}

void MagDrumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    bool hostIsPlaying = false;
    juce::Optional<double> hostPpq;
    juce::Optional<double> hostTempo;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            hostIsPlaying = position->getIsPlaying();
            hostPpq = position->getPpqPosition();
            hostTempo = position->getBpm();
        }
    }
    const bool hostHasSequencerTime = hostIsPlaying && hostPpq.hasValue() && hostTempo.hasValue()
        && std::isfinite(*hostPpq) && std::isfinite(*hostTempo) && *hostTempo > 0.0;
    const bool hostSynced = sequencerPlaybackEnabled.load(std::memory_order_acquire) && hostHasSequencerTime;
    sequencerHostSynchronized.store(hostSynced, std::memory_order_release);
    if (!sampleLock.tryEnter())
    {
        bool sequencerLockHeld = hostSynced && sequencerPatternLock.tryEnter();
        std::optional<magdrum::sequencer::HostPlaybackCursor> deferredSequencerCursor;
        if (sequencerLockHeld && sequencerPatternLengthBeats > 0.0 && !sequencerPlaybackEvents.empty())
            deferredSequencerCursor.emplace(sequencerPlaybackEvents, sequencerPatternLengthBeats,
                                            *hostPpq, *hostTempo, outputSampleRate);
        else if (sequencerLockHeld)
        {
            sequencerPatternLock.exit();
            sequencerLockHeld = false;
        }

        auto midiIterator = midi.cbegin();
        const auto midiEnd = midi.cend();
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (deferredSequencerCursor)
                deferredSequencerCursor->triggerAtSample([this](const auto& event)
                {
                    deferredNoteTriggers.push(event.midiNote, event.velocity);
                });

            while (midiIterator != midiEnd && (*midiIterator).samplePosition <= sample)
            {
                const auto message = (*midiIterator).getMessage();
                if (message.isNoteOn())
                {
                    const int note = message.getNoteNumber();
                    const int velocity = juce::jlimit(1, 127,
                        static_cast<int>(std::lround(message.getFloatVelocity() * 127.0f)));
                    if (juce::isPositiveAndBelow(note, 128))
                        deferredNoteTriggers.push(note, velocity);
                }
                ++midiIterator;
            }
        }
        if (sequencerLockHeld)
            sequencerPatternLock.exit();
        midi.clear();
        return;
    }
    struct CallbackUnlock { juce::CriticalSection& lock; ~CallbackUnlock() { lock.exit(); } } unlock{sampleLock};

    const bool hasSequencerPatternLock = hostSynced && sequencerPatternLock.tryEnter();
    struct SequencerPatternUnlock
    {
        juce::CriticalSection& lock;
        bool isLocked;
        ~SequencerPatternUnlock() { if (isLocked) lock.exit(); }
    } sequencerUnlock{sequencerPatternLock, hasSequencerPatternLock};
    const bool scheduleHostSequencer = hasSequencerPatternLock
        && sequencerPatternLengthBeats > 0.0 && !sequencerPlaybackEvents.empty();
    std::optional<magdrum::sequencer::HostPlaybackCursor> sequencerCursor;
    if (scheduleHostSequencer)
        sequencerCursor.emplace(sequencerPlaybackEvents, sequencerPatternLengthBeats,
                                *hostPpq, *hostTempo, outputSampleRate);

    deferredNoteTriggers.drain([this](const auto& trigger)
    {
        startNoteVoice(trigger.midiNote, trigger.velocity);
    });

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingTriggerFifo.prepareToRead(pendingTriggerCapacity, start1, size1, start2, size2);
    const auto consumeQueuedTriggers = [this](int start, int count)
    {
        for (int index = 0; index < count; ++index)
        {
            const auto& trigger = pendingTriggers[static_cast<size_t>(start + index)];
            startNoteVoice(trigger.midiNote, trigger.velocity);
        }
    };
    consumeQueuedTriggers(start1, size1);
    consumeQueuedTriggers(start2, size2);
    pendingTriggerFifo.finishedRead(size1 + size2);

    auto midiIterator = midi.cbegin();
    const auto midiEnd = midi.cend();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        if (sequencerCursor)
            sequencerCursor->triggerAtSample([this](const auto& event) { startNoteVoice(event.midiNote, event.velocity); });
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= i)
        {
            const auto message = (*midiIterator).getMessage();
            if (message.isNoteOn())
            {
                const int note = message.getNoteNumber();
                const int velocity = juce::jlimit(1, 127, static_cast<int>(std::lround(message.getFloatVelocity() * 127.0f)));
                if (juce::isPositiveAndBelow(note, 128))
                    startNoteVoice(note, velocity);
            }
            ++midiIterator;
        }

        float left = 0.0f, right = 0.0f;
        for (auto& voice : voices)
        {
            if (!voice.active || voice.sample == nullptr) continue;
            const auto& audio = voice.sample->audio;
            const int index = static_cast<int>(voice.position);
            if (index >= audio.getNumSamples()) { voice.active = false; continue; }
            const int next = juce::jmin(index + 1, audio.getNumSamples() - 1);
            const float fraction = static_cast<float>(voice.position - index);
            const auto readInterpolated = [&](int channel)
            {
                const auto* data = audio.getReadPointer(juce::jmin(channel, audio.getNumChannels() - 1));
                return data[index] + (data[next] - data[index]) * fraction;
            };
            float envelope = voice.attackSamples > 0.0
                ? juce::jmin(1.0f, static_cast<float>(voice.elapsedOutputSamples / voice.attackSamples)) : 1.0f;
            if (voice.releaseSamples > 0.0)
            {
                const double remainingSamples = juce::jmax(0.0, voice.totalOutputSamples - voice.elapsedOutputSamples);
                envelope = juce::jmin(envelope, juce::jlimit(0.0f, 1.0f,
                    static_cast<float>(remainingSamples / voice.releaseSamples)));
            }
            const float leftInput = readInterpolated(0);
            const float rightInput = readInterpolated(audio.getNumChannels() > 1 ? 1 : 0);
            const float filterMix = 1.0f - voice.lowPassCoefficient;
            voice.lowPassStateLeft += filterMix * (leftInput - voice.lowPassStateLeft);
            voice.lowPassStateRight += filterMix * (rightInput - voice.lowPassStateRight);
            left += voice.lowPassStateLeft * voice.gain * envelope * voice.leftPanGain;
            right += voice.lowPassStateRight * voice.gain * envelope * voice.rightPanGain;
            voice.position += (voice.sample->sampleRate / outputSampleRate) * voice.playbackRate;
            voice.elapsedOutputSamples += 1.0;
        }
        if (buffer.getNumChannels() == 1) buffer.setSample(0, i, 0.5f * (left + right));
        else { buffer.setSample(0, i, left); buffer.setSample(1, i, right); }
    }
    midi.clear();
}

void MagDrumAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::XmlElement state("MAGDRUM_STATE");
    {
        const juce::ScopedLock lock(programNameLock);
        state.setAttribute("programName", programName);
    }
    if (auto savedEditorState = getPluginEditorState())
        state.addChildElement(savedEditorState.release());
    {
        const juce::ScopedLock lock(stateLock);
        for (int note = 0; note < 128; ++note)
        {
            const auto noteIndex = static_cast<size_t>(note);
            const auto parameters = getNoteParameters(note);
            if (std::abs(parameters.gainDb) > 0.000001f || std::abs(parameters.pan) > 0.000001f
                || std::abs(parameters.tuneSemitones) > 0.000001f || std::abs(parameters.attackMs) > 0.000001f
                || std::abs(parameters.releaseMs) > 0.000001f || std::abs(parameters.filterCutoffHz - 20000.0f) > 0.001f)
            {
                auto* savedParameters = state.createNewChildElement("PAD_SETTINGS");
                savedParameters->setAttribute("note", note);
                savedParameters->setAttribute("gainDb", parameters.gainDb);
                savedParameters->setAttribute("pan", parameters.pan);
                savedParameters->setAttribute("tuneSemitones", parameters.tuneSemitones);
                savedParameters->setAttribute("attackMs", parameters.attackMs);
                savedParameters->setAttribute("releaseMs", parameters.releaseMs);
                savedParameters->setAttribute("filterCutoffHz", parameters.filterCutoffHz);
            }
            int layerCount = 0;
            const SampleData* onlyLayer = nullptr;
            juce::String onlyPath;
            for (int layer = 0; layer < maxVelocityLayers; ++layer)
            {
                const auto layerIndex = static_cast<size_t>(layer);
                const auto& path = samplePaths[noteIndex][layerIndex];
                const auto* sample = ownedSamples[noteIndex][layerIndex].get();
                if (path.isEmpty() || sample == nullptr)
                    continue;

                auto* savedLayer = state.createNewChildElement("SAMPLE_LAYER");
                savedLayer->setAttribute("note", note);
                savedLayer->setAttribute("minimumVelocity", sample->minimumVelocity);
                savedLayer->setAttribute("maximumVelocity", sample->maximumVelocity);
                savedLayer->setAttribute("path", path);
                ++layerCount;
                onlyLayer = sample;
                onlyPath = path;
            }
            for (const auto& pending : pendingSampleLoads)
            {
                if (pending.note != note)
                    continue;
                auto* savedLayer = state.createNewChildElement("SAMPLE_LAYER");
                savedLayer->setAttribute("note", note);
                savedLayer->setAttribute("minimumVelocity", pending.minimumVelocity);
                savedLayer->setAttribute("maximumVelocity", pending.maximumVelocity);
                savedLayer->setAttribute("path", pending.path);
                ++layerCount;
            }
            if (layerCount == 1 && onlyLayer != nullptr
                && onlyLayer->minimumVelocity == 1 && onlyLayer->maximumVelocity == 127)
                state.setAttribute("sample" + juce::String(note), onlyPath);
        }
    }
    copyXmlToBinary(state, destination);
}

void MagDrumAudioProcessor::setStateInformation(const void* data, int size)
{
    auto state = getXmlFromBinary(data, size);
    if (!state || !state->hasTagName("MAGDRUM_STATE")) return;
    {
        const auto restoredName = state->getStringAttribute("programName").trim();
        const juce::ScopedLock lock(programNameLock);
        programName = restoredName.isNotEmpty() ? restoredName : "Default Kit";
    }
    if (const auto* savedEditorState = state->getChildByName("PLUGIN_EDITOR_STATE"))
        setPluginEditorState(*savedEditorState);
    struct SavedLayer { int minimumVelocity = 1; int maximumVelocity = 127; juce::String path; };
    std::array<std::vector<SavedLayer>, 128> savedLayers;
    std::array<bool, 128> hasExplicitLayers{};
    std::array<NoteParameters, 128> savedParameters{};
    for (auto* child = state->getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->hasTagName("PAD_SETTINGS"))
        {
            const int note = child->getIntAttribute("note", -1);
            if (juce::isPositiveAndBelow(note, 128))
                savedParameters[static_cast<size_t>(note)] = {
                    static_cast<float>(child->getDoubleAttribute("gainDb", 0.0)),
                    static_cast<float>(child->getDoubleAttribute("pan", 0.0)),
                    static_cast<float>(child->getDoubleAttribute("tuneSemitones", 0.0)),
                    static_cast<float>(child->getDoubleAttribute("attackMs", 0.0)),
                    static_cast<float>(child->getDoubleAttribute("releaseMs", 0.0)),
                    static_cast<float>(child->getDoubleAttribute("filterCutoffHz", 20000.0))};
            continue;
        }
        if (!child->hasTagName("SAMPLE_LAYER"))
            continue;
        const int note = child->getIntAttribute("note", -1);
        const int minimum = child->getIntAttribute("minimumVelocity", -1);
        const int maximum = child->getIntAttribute("maximumVelocity", -1);
        const auto path = child->getStringAttribute("path");
        if (note < 0 || note > 127 || minimum < 1 || maximum > 127 || minimum > maximum || path.isEmpty())
            continue;
        auto& layers = savedLayers[static_cast<size_t>(note)];
        if (layers.size() >= static_cast<size_t>(maxVelocityLayers))
            continue;
        layers.push_back({minimum, maximum, path});
        hasExplicitLayers[static_cast<size_t>(note)] = true;
    }
    for (int note = 0; note < 128; ++note)
    {
        const auto index = static_cast<size_t>(note);
        if (!hasExplicitLayers[index])
        {
            const auto legacyPath = state->getStringAttribute("sample" + juce::String(note));
            if (legacyPath.isNotEmpty())
                savedLayers[index].push_back({1, 127, legacyPath});
        }
    }
    struct RestoreRequest
    {
        int note = 0;
        int minimumVelocity = 1;
        int maximumVelocity = 127;
        juce::File file;
        std::shared_ptr<std::atomic<bool>> cancellation;
    };
    std::vector<RestoreRequest> restoreRequests;
    const auto generation = sampleStateLoadGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    {
        const juce::ScopedLock sampleGuard(sampleLock);
        const juce::ScopedLock stateGuard(stateLock);
        for (auto& pending : pendingSampleLoads)
            if (pending.cancellation != nullptr)
                pending.cancellation->store(false, std::memory_order_release);
        pendingSampleLoads.clear();
        for (int note = 0; note < 128; ++note)
        {
            const auto noteIndex = static_cast<size_t>(note);
            for (int layer = 0; layer < maxVelocityLayers; ++layer)
            {
                const auto layerIndex = static_cast<size_t>(layer);
                sampleSlots[noteIndex][layerIndex].store(nullptr, std::memory_order_release);
                ownedSamples[noteIndex][layerIndex].reset();
                samplePaths[noteIndex][layerIndex].clear();
            }
            for (auto& counter : roundRobinCounters[noteIndex])
                counter.store(0, std::memory_order_relaxed);
            for (const auto& layer : savedLayers[noteIndex])
            {
                auto cancellation = std::make_shared<std::atomic<bool>>(true);
                pendingSampleLoads.push_back(
                    {note, layer.minimumVelocity, layer.maximumVelocity, layer.path, cancellation});
                restoreRequests.push_back({note, layer.minimumVelocity, layer.maximumVelocity,
                                           juce::File(layer.path), std::move(cancellation)});
            }
        }
        recalculateTailLengthSeconds();
        for (auto& voice : voices) voice = {};
    }
    for (int note = 0; note < 128; ++note)
        setNoteParameters(note, savedParameters[static_cast<size_t>(note)]);
    if (restoreRequests.empty())
    {
        sendChangeMessage();
        return;
    }

    const juce::ScopedLock workerLock(sampleStateLoadWorkerLock);
    if (sampleStateLoadWorkers == nullptr)
        sampleStateLoadWorkers = std::make_unique<juce::ThreadPool>(1);
    sampleStateLoadWorkers->addJob(new PluginStateSampleLoadJob(
        [this, generation, requests = std::move(restoreRequests)]() mutable
        {
            for (const auto& request : requests)
            {
                if (sampleStateLoadGeneration.load(std::memory_order_acquire) != generation)
                    return;
                const auto shouldCancel = [this, generation, &request]
                {
                    return sampleStateLoadGeneration.load(std::memory_order_acquire) != generation
                        || request.cancellation == nullptr
                        || !request.cancellation->load(std::memory_order_acquire);
                };
                if (!shouldCancel())
                    loadSampleForVelocityRange(request.note, request.minimumVelocity,
                        request.maximumVelocity, request.file, shouldCancel);
            }
            if (sampleStateLoadGeneration.load(std::memory_order_acquire) == generation)
                sendChangeMessage();
        }), true);
}

const juce::String MagDrumAudioProcessor::getProgramName(int index)
{
    if (index != 0)
        return {};
    const juce::ScopedLock lock(programNameLock);
    return programName;
}

void MagDrumAudioProcessor::changeProgramName(int index, const juce::String& newName)
{
    if (index != 0)
        return;
    const auto trimmedName = newName.trim();
    if (trimmedName.isEmpty())
        return;
    {
        const juce::ScopedLock lock(programNameLock);
        if (programName == trimmedName)
            return;
        programName = trimmedName;
    }
    updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
}

void MagDrumAudioProcessor::setPluginEditorState(const juce::XmlElement& value)
{
    if (!value.hasTagName("PLUGIN_EDITOR_STATE")) return;
    {
        const juce::ScopedLock lock(editorStateLock);
        editorState = std::make_unique<juce::XmlElement>(value);
    }

    auto mapping = MidiMappingProfile::generalMidi();
    const auto profileCsv = value.getStringAttribute("profileCsv");
    if (profileCsv.isNotEmpty())
        if (const auto parsed = MidiMappingProfile::fromCsv(profileCsv.toStdString()))
            mapping = *parsed;

    const bool hasSemanticEvents = value.getIntAttribute("eventModel", 0) == 1;
    std::vector<DrumEvent> events;
    std::vector<magdrum::sequencer::MeterChange> meterChanges;
    for (auto* child = value.getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->hasTagName("EVENT") && hasSemanticEvents)
        {
            const int instrument = child->getIntAttribute("instrument", -1);
            const int articulation = child->getIntAttribute("articulation", -1);
            const int velocity = child->getIntAttribute("velocity", 0);
            const double beat = child->getDoubleAttribute("beat", -1.0);
            const double duration = child->getDoubleAttribute("duration", 0.0);
            if (juce::isPositiveAndBelow(instrument, 9) && juce::isPositiveAndBelow(articulation, 13)
                && velocity >= 1 && velocity <= 127 && std::isfinite(beat) && beat >= 0.0
                && std::isfinite(duration) && duration > 0.0)
                events.push_back({static_cast<DrumInstrument>(instrument), static_cast<DrumArticulation>(articulation),
                                  static_cast<std::uint8_t>(velocity), beat, duration});
        }
        else if (child->hasTagName("STEP") && !hasSemanticEvents)
        {
            const int lane = child->getIntAttribute("lane", -1);
            const int step = child->getIntAttribute("step", -1);
            const int velocity = juce::jlimit(1, 127, child->getIntAttribute("velocity", 100));
            if (juce::isPositiveAndBelow(lane, static_cast<int>(sequencerLaneInstruments.size()))
                && juce::isPositiveAndBelow(step, magdrum::sequencer::maximumSteps))
                events.push_back({sequencerLaneInstruments[static_cast<size_t>(lane)],
                                  sequencerLaneArticulations[static_cast<size_t>(lane)],
                                  static_cast<std::uint8_t>(velocity), step * 0.25, 0.12});
        }
        else if (child->hasTagName("META") && child->getStringAttribute("kind") == "meter")
        {
            const double beat = child->getDoubleAttribute("beat", -1.0);
            const int numerator = child->getIntAttribute("numerator", 0);
            const int denominator = child->getIntAttribute("denominator", 0);
            if (std::isfinite(beat) && beat >= 0.0
                && magdrum::sequencer::stepsPerBar(numerator, denominator) > 0)
                meterChanges.push_back({beat, numerator, denominator});
        }
    }

    const auto meterText = value.getStringAttribute("meter", "4/4");
    const int slash = meterText.indexOfChar('/');
    const int numerator = slash > 0 ? meterText.substring(0, slash).getIntValue() : 4;
    const int denominator = slash > 0 ? meterText.substring(slash + 1).getIntValue() : 4;
    const int bars = juce::jlimit(1, magdrum::sequencer::maximumBars,
                                  value.getIntAttribute("bars", magdrum::sequencer::maximumBars));
    auto barMap = magdrum::sequencer::buildBarMap(numerator, denominator, bars, meterChanges);
    double patternLengthBeats = barMap.valid ? barMap.lengthBeats : 0.0;
    if (patternLengthBeats <= 0.0 && magdrum::sequencer::stepsPerBar(numerator, denominator) > 0)
        patternLengthBeats = static_cast<double>(numerator) * 4.0 * bars / static_cast<double>(denominator);
    setSequencerPattern(events, mapping, patternLengthBeats);
}

std::unique_ptr<juce::XmlElement> MagDrumAudioProcessor::getPluginEditorState() const
{
    const juce::ScopedLock lock(editorStateLock);
    return editorState ? std::make_unique<juce::XmlElement>(*editorState) : nullptr;
}

juce::AudioProcessorEditor* MagDrumAudioProcessor::createEditor()
{
    return new MagDrumAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagDrumAudioProcessor();
}
