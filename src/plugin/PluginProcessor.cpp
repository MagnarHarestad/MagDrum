#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <limits>

MagDrumAudioProcessor::MagDrumAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    for (auto& slot : sampleSlots) slot.store(nullptr);
}

void MagDrumAudioProcessor::prepareToPlay(double rate, int)
{
    outputSampleRate = rate > 0.0 ? rate : 44100.0;
}

bool MagDrumAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto output = layout.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

bool MagDrumAudioProcessor::loadSampleForNote(int note, const juce::File& file)
{
    if (note < 0 || note > 127 || !file.existsAsFile()) return false;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0 || reader->numChannels == 0) return false;
    constexpr double maxSampleSeconds = 600.0;
    if (reader->lengthInSamples > reader->sampleRate * maxSampleSeconds || reader->lengthInSamples > std::numeric_limits<int>::max()) return false;

    auto sample = std::make_unique<SampleData>();
    sample->sampleRate = reader->sampleRate;
    const int channels = static_cast<int>(juce::jmin(2u, reader->numChannels));
    const int sampleCount = static_cast<int>(reader->lengthInSamples);
    sample->audio.setSize(channels, sampleCount);
    if (!reader->read(sample->audio.getArrayOfWritePointers(), channels, 0, sampleCount)) return false;

    const auto* stable = sample.get();
    ownedSamples.push_back(std::move(sample)); // Keep old buffers alive while the audio thread may still use them.
    sampleSlots[static_cast<size_t>(note)].store(stable, std::memory_order_release);
    {
        const juce::ScopedLock lock(stateLock);
        samplePaths[static_cast<size_t>(note)] = file.getFullPathName();
    }
    return true;
}

juce::String MagDrumAudioProcessor::getSamplePathForNote(int note) const
{
    if (note < 0 || note > 127) return {};
    const juce::ScopedLock lock(stateLock);
    return samplePaths[static_cast<size_t>(note)];
}

void MagDrumAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
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
                const auto* sample = sampleSlots[static_cast<size_t>(note)].load(std::memory_order_acquire);
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
            if (samplePaths[static_cast<size_t>(note)].isNotEmpty())
                state.setAttribute("sample" + juce::String(note), samplePaths[static_cast<size_t>(note)]);
    }
    copyXmlToBinary(state, destination);
}

void MagDrumAudioProcessor::setStateInformation(const void* data, int size)
{
    auto state = getXmlFromBinary(data, size);
    if (!state || !state->hasTagName("MAGDRUM_STATE")) return;
    std::array<juce::String, 128> paths;
    for (int note = 0; note < 128; ++note)
    {
        paths[static_cast<size_t>(note)] = state->getStringAttribute("sample" + juce::String(note));
        sampleSlots[static_cast<size_t>(note)].store(nullptr, std::memory_order_release);
    }
    {
        const juce::ScopedLock lock(stateLock);
        samplePaths.fill(juce::String{});
    }
    for (int note = 0; note < 128; ++note)
        if (paths[static_cast<size_t>(note)].isNotEmpty())
            loadSampleForNote(note, juce::File(paths[static_cast<size_t>(note)]));
}

juce::AudioProcessorEditor* MagDrumAudioProcessor::createEditor()
{
    return new MagDrumAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MagDrumAudioProcessor();
}
