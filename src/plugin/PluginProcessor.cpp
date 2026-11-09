#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <limits>

MagDrumAudioProcessor::MagDrumAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    for (auto& noteSlots : sampleSlots)
        for (auto& slot : noteSlots)
            slot.store(nullptr);
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
    const juce::File& file)
{
    if (note < 0 || note > 127 || minimumVelocity < 1 || maximumVelocity > 127
        || minimumVelocity > maximumVelocity || !file.existsAsFile()) return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
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
    if (!reader->read(sample->audio.getArrayOfWritePointers(), channels, 0, sampleCount)) return false;

    {
        const juce::ScopedLock sampleGuard(sampleLock);
        const juce::ScopedLock stateGuard(stateLock);
        int targetLayer = -1;
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
                targetLayer = layer;
                break;
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
        oldSample = std::move(sample);
        sampleSlots[static_cast<size_t>(note)][static_cast<size_t>(targetLayer)].store(oldSample.get(), std::memory_order_release);
        samplePaths[static_cast<size_t>(note)][static_cast<size_t>(targetLayer)] = file.getFullPathName();
    }
    return true;
}

bool MagDrumAudioProcessor::clearSamplesForNote(int note)
{
    if (note < 0 || note > 127)
        return false;
    bool removedAny = false;
    const juce::ScopedLock sampleGuard(sampleLock);
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
        owned.reset();
    }
    const juce::ScopedLock stateGuard(stateLock);
    for (auto& path : samplePaths[static_cast<size_t>(note)])
        path.clear();
    return removedAny;
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
        layers.push_back({sample->minimumVelocity, sample->maximumVelocity, samplePaths[noteIndex][layerIndex]});
    }
    std::sort(layers.begin(), layers.end(), [](const auto& left, const auto& right)
        { return left.minimumVelocity < right.minimumVelocity; });
    return layers;
}

void MagDrumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    if (!sampleLock.tryEnter()) { midi.clear(); return; }
    struct CallbackUnlock { juce::CriticalSection& lock; ~CallbackUnlock() { lock.exit(); } } unlock{sampleLock};
    juce::MidiBuffer::Iterator iterator(midi);
    juce::MidiMessage message;
    int eventSample = 0;
    bool hasEvent = iterator.getNextEvent(message, eventSample);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        while (hasEvent && eventSample <= i)
        {
            if (message.isNoteOn())
            {
                const int note = message.getNoteNumber();
                const int velocity = juce::jlimit(1, 127, static_cast<int>(std::lround(message.getFloatVelocity() * 127.0f)));
                const SampleData* sample = nullptr;
                for (const auto& slot : sampleSlots[static_cast<size_t>(note)])
                {
                    const auto* candidate = slot.load(std::memory_order_acquire);
                    if (candidate != nullptr && velocity >= candidate->minimumVelocity && velocity <= candidate->maximumVelocity)
                    {
                        sample = candidate;
                        break;
                    }
                }
                if (sample != nullptr && sample->audio.getNumSamples() > 0)
                {
                    auto& voice = voices[static_cast<size_t>(nextVoice++ % maxVoices)];
                    voice = {sample, 0.0, message.getFloatVelocity(), true};
                }
            }
            hasEvent = iterator.getNextEvent(message, eventSample);
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
                return (data[index] + (data[next] - data[index]) * fraction) * voice.gain;
            };
            left += readInterpolated(0);
            right += readInterpolated(audio.getNumChannels() > 1 ? 1 : 0);
            voice.position += voice.sample->sampleRate / outputSampleRate;
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
        const juce::ScopedLock lock(stateLock);
        for (int note = 0; note < 128; ++note)
        {
            const auto noteIndex = static_cast<size_t>(note);
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
    struct SavedLayer { int minimumVelocity = 1; int maximumVelocity = 127; juce::String path; };
    std::array<std::vector<SavedLayer>, 128> savedLayers;
    std::array<bool, 128> hasExplicitLayers{};
    for (auto* child = state->getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
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
    {
        const juce::ScopedLock sampleGuard(sampleLock);
        const juce::ScopedLock stateGuard(stateLock);
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
        }
        for (auto& voice : voices) voice = {};
    }
    for (int note = 0; note < 128; ++note)
        for (const auto& layer : savedLayers[static_cast<size_t>(note)])
            loadSampleForVelocityRange(note, layer.minimumVelocity, layer.maximumVelocity, juce::File(layer.path));
}

juce::AudioProcessorEditor* MagDrumAudioProcessor::createEditor()
{
    return new MagDrumAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagDrumAudioProcessor();
}
