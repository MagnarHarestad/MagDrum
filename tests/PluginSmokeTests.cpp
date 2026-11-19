#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/DrumPadLayout.h"
#include "core/PluginScanProcess.h"
#include "core/ScopedFileAccess.h"
#include "ui/MagDrumLocalization.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
static_assert(magdrum::plugin::bossDr660PadLayout.size() == 16);
static_assert(magdrum::plugin::bossDr660PadLayout[12].midiNote == 36);
static_assert(magdrum::plugin::bossDr660PadLayout[13].midiNote == 38);
static_assert(magdrum::plugin::bossDr660PadLayout[14].midiNote == 44);
static_assert(magdrum::plugin::bossDr660PadLayout[15].midiNote == 56);

thread_local bool countRealtimeAllocations = false;
thread_local int realtimeAllocationCount = 0;

void recordRealtimeAllocation() noexcept
{
    if (countRealtimeAllocations)
        ++realtimeAllocationCount;
}
}

void* operator new(std::size_t size)
{
    recordRealtimeAllocation();
    if (auto* memory = std::malloc(size == 0 ? 1 : size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    return ::operator new(size);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    recordRealtimeAllocation();
    const auto alignedSize = ((size == 0 ? 1 : size) + static_cast<std::size_t>(alignment) - 1)
        / static_cast<std::size_t>(alignment) * static_cast<std::size_t>(alignment);
#if defined(_MSC_VER)
    if (auto* memory = _aligned_malloc(alignedSize, static_cast<std::size_t>(alignment)))
        return memory;
#else
    if (auto* memory = std::aligned_alloc(static_cast<std::size_t>(alignment), alignedSize))
        return memory;
#endif
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
}

void operator delete(void* memory, std::align_val_t) noexcept
{
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void operator delete[](void* memory, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete(void* memory, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

void operator delete[](void* memory, std::size_t, std::align_val_t alignment) noexcept
{
    ::operator delete(memory, alignment);
}

namespace
{
class TestAudioPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    PositionInfo position;
};

bool writeFixture(const juce::File& file, float amplitude = 0.25f, int sampleCount = 960, double frequencyHz = 220.0)
{
    constexpr double sampleRate = 48000.0;
    juce::AudioBuffer<float> audio(1, sampleCount);
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        audio.setSample(0, sample, static_cast<float>(amplitude * std::sin(2.0 * juce::MathConstants<double>::pi * frequencyHz * sample / sampleRate)));

    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream)
        return false;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

bool writeTrimImpulseFixture(const juce::File& file)
{
    juce::AudioBuffer<float> audio(1, 960);
    audio.clear();
    audio.setSample(0, 100, 0.7f);
    audio.setSample(0, 600, 0.8f);
    audio.setSample(0, 750, 0.9f);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream)
        return false;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
        .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(24));
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

std::vector<float> renderSamples(MagDrumAudioProcessor& processor, int note, int velocity, int sampleCount = 960)
{
    juce::AudioBuffer<float> audio(2, sampleCount);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(10, note, static_cast<juce::uint8>(velocity)), 0);
    processor.processBlock(audio, midi);
    std::vector<float> output(static_cast<size_t>(sampleCount));
    for (int sample = 0; sample < sampleCount; ++sample)
        output[static_cast<size_t>(sample)] = audio.getSample(0, sample);
    return output;
}

bool waitForSamplePath(MagDrumAudioProcessor& processor, int note, const juce::String& expectedPath)
{
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (processor.getSamplePathForNote(note) == expectedPath)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

bool waitForSampleLayerCount(MagDrumAudioProcessor& processor, int note, size_t expectedCount)
{
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (processor.getSampleLayersForNote(note).size() == expectedCount)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

class MagDrumAudioProcessorTestAccess
{
public:
    static juce::CriticalSection& sampleLock(MagDrumAudioProcessor& processor)
    {
        return processor.sampleLock;
    }

    static void stopVoices(MagDrumAudioProcessor& processor)
    {
        for (auto& voice : processor.voices)
            voice.active = false;
    }

    static int activeVoicesForChokeGroup(const MagDrumAudioProcessor& processor, int group)
    {
        return static_cast<int>(std::count_if(processor.voices.begin(), processor.voices.end(),
            [group](const auto& voice) { return voice.active && voice.chokeGroup == group; }));
    }

    static bool activeVoiceMatches(const MagDrumAudioProcessor& processor, int group, int note)
    {
        return std::any_of(processor.voices.begin(), processor.voices.end(),
            [group, note](const auto& voice)
            { return voice.active && voice.chokeGroup == group && voice.midiNote == note; });
    }
};

namespace
{
std::vector<std::unique_ptr<MagDrumAudioProcessor>>* activeTestProcessors = nullptr;

MagDrumAudioProcessor& makeTestProcessor()
{
    auto processor = std::make_unique<MagDrumAudioProcessor>();
    auto& result = *processor;
    activeTestProcessors->push_back(std::move(processor));
    return result;
}

juce::AudioParameterFloat* findHostFloatParameter(MagDrumAudioProcessor& processor,
                                                   const juce::String& parameterId)
{
    for (auto* parameter : processor.getParameters())
        if (const auto* withId = dynamic_cast<const juce::AudioProcessorParameterWithID*>(parameter);
            withId != nullptr && withId->paramID == parameterId)
            return dynamic_cast<juce::AudioParameterFloat*>(parameter);
    return nullptr;
}

class ParameterGestureProbe final : public juce::AudioProcessorParameter::Listener
{
public:
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool isStarting) override
    {
        if (isStarting)
            ++starts;
        else
            ++ends;
    }

    int starts = 0;
    int ends = 0;
};
}

class MagDrumAudioProcessorEditorTestAccess
{
public:
    static int checksMidiFallbackPolicyPersists(MagDrumAudioProcessorEditor& editor)
    {
        editor.midiFallbackPolicy.setSelectedId(2, juce::dontSendNotification);
        if (editor.midiFallbackPolicy.onChange)
            editor.midiFallbackPolicy.onChange();
        const auto editorState = editor.processor.getPluginEditorState();
        if (!editorState || editorState->getIntAttribute("fallbackPolicy", -1) != 1
            || !editorState->getBoolAttribute("allowArticulationFallback", false))
        {
            std::cerr << "The selected MIDI fallback policy was not saved in the plug-in editor state.\n";
            return 1;
        }

        juce::MemoryBlock serializedState;
        editor.processor.getStateInformation(serializedState);
        editor.midiFallbackPolicy.setSelectedId(3, juce::dontSendNotification);
        editor.processor.setStateInformation(serializedState.getData(),
                                             static_cast<int>(serializedState.getSize()));
        editor.restorePluginEditorState();
        if (editor.midiFallbackPolicy.getSelectedId() != 2)
        {
            std::cerr << "The MIDI fallback policy was not restored with the Cubase plug-in state.\n";
            return 2;
        }

        juce::XmlElement legacyEditorState("PLUGIN_EDITOR_STATE");
        legacyEditorState.setAttribute("allowArticulationFallback", true);
        editor.processor.setPluginEditorState(legacyEditorState);
        editor.restorePluginEditorState();
        if (editor.midiFallbackPolicy.getSelectedId() != 3)
        {
            std::cerr << "A legacy Cubase project did not restore its enabled articulation fallback.\n";
            return 3;
        }

        juce::XmlElement currentEditorState("PLUGIN_EDITOR_STATE");
        currentEditorState.setAttribute("fallbackPolicy", 1);
        currentEditorState.setAttribute("allowArticulationFallback", false);
        editor.processor.setPluginEditorState(currentEditorState);
        editor.restorePluginEditorState();
        if (editor.midiFallbackPolicy.getSelectedId() != 2)
        {
            std::cerr << "The explicit MIDI fallback mode did not take precedence over the legacy flag.\n";
            return 4;
        }
        return 0;
    }

    static int checksSampleMapCoversBeyondFirstListPage(MagDrumAudioProcessorEditor& editor,
                                                         const juce::File& validSample)
    {
        constexpr int fixtureCount = 400;
        constexpr int offPageMapIndex = 300;
        const auto expectedFile = validSample.getParentDirectory().getChildFile("Map Fixture 300.wav");
        if (!validSample.copyFileTo(expectedFile))
        {
            std::cerr << "Could not create the valid off-page sample assignment fixture.\n";
            return 1;
        }
        for (int index = 0; index < fixtureCount; ++index)
        {
            ScannedSample sample;
            sample.file = index == offPageMapIndex ? expectedFile
                : juce::File("C:/MagDrumTestFixtures/Map/Map Fixture "
                             + juce::String(index).paddedLeft('0', 3) + ".wav");
            sample.durationSeconds = index == offPageMapIndex ? 0.02 : 0.18 + 0.001 * index;
            sample.channels = 1;
            sample.sampleRate = 44100.0;
            sample.format = "WAV";
            sample.spectralCentroidHz = index == offPageMapIndex ? 15000.0 : 900.0 + index;
            sample.category = index % 2 == 0 ? "Kick" : "Snare";
            if (!editor.database.insertSample(sample))
            {
                std::cerr << "Could not seed the sample map paging fixture.\n";
                return 1;
            }
        }

        editor.sampleSearch.setText("Map Fixture", juce::dontSendNotification);
        editor.refreshSamples();
        const auto deadline = juce::Time::getMillisecondCounter() + 10000;
        auto pollForMap = std::make_shared<std::function<void()>>();
        *pollForMap = [&editor, deadline, pollForMap]
        {
            const bool ready = !editor.sampleBrowseQueryPending && !editor.sampleMapQueryPending
                && editor.samples.size() == 250 && editor.sampleMapSamples.size() == fixtureCount;
            if (ready || juce::Time::getMillisecondCounter() >= deadline)
            {
                juce::MessageManager::getInstance()->stopDispatchLoop();
                return;
            }
            juce::Timer::callAfterDelay(20, *pollForMap);
        };
        juce::Timer::callAfterDelay(20, *pollForMap);
        juce::MessageManager::getInstance()->runDispatchLoop();
        *pollForMap = {};
        pollForMap.reset();
        if (editor.samples.size() != 250 || !editor.hasMoreSamples
            || editor.sampleMapSamples.size() != fixtureCount)
        {
            std::cerr << "The sample map did not load the complete filtered library independently of list paging.\n";
            return 2;
        }

        const auto mapSelectedFile = editor.sampleMapSamples.getReference(offPageMapIndex).file;
        editor.selectSampleMapSample(offPageMapIndex);
        if (editor.selectedSample() != mapSelectedFile || editor.sampleMapSelectedFile != mapSelectedFile
            || editor.indexedList.getSelectedRow() != -1)
        {
            std::cerr << "Selecting a map point beyond the first list page did not select that sample.\n";
            return 3;
        }

        const auto previewPoint = editor.sampleMapPointForTest(offPageMapIndex);
        const auto otherPoint = editor.sampleMapPointForTest(0);
        if (previewPoint.getDistanceFrom(otherPoint) < 32.0f)
        {
            std::cerr << "The sample-map preview fixture points are not separated enough for an interaction test.\n";
            return 6;
        }
        const auto initialPreviewGeneration = editor.samplePreviewGeneration.load(std::memory_order_acquire);
        editor.dispatchSampleMapDoubleClickForTest(previewPoint);
        if (editor.samplePreviewGeneration.load(std::memory_order_acquire) != initialPreviewGeneration + 1
            || editor.selectedSample() != mapSelectedFile)
        {
            std::cerr << "Double-clicking an acoustic-map point did not select and preview its sample.\n";
            return 7;
        }
        editor.dispatchSampleMapMoveForTest(otherPoint);
        if (editor.samplePreviewGeneration.load(std::memory_order_acquire) != initialPreviewGeneration + 2)
        {
            std::cerr << "Moving off a previewed acoustic-map point did not stop its preview.\n";
            return 8;
        }
        editor.dispatchSampleMapDoubleClickForTest(previewPoint);
        editor.dispatchSampleMapExitForTest();
        if (editor.samplePreviewGeneration.load(std::memory_order_acquire) != initialPreviewGeneration + 4)
        {
            std::cerr << "Leaving the acoustic map did not stop the active sample preview.\n";
            return 9;
        }

        const auto initialViewport = editor.sampleMapViewportForTest();
        editor.dispatchSampleMapWheelForTest(previewPoint, 0.8f);
        const auto zoomedViewport = editor.sampleMapViewportForTest();
        if (zoomedViewport[0] <= initialViewport[0] || zoomedViewport[0] > 12.0f)
        {
            std::cerr << "The acoustic map wheel event did not zoom within its supported bounds.\n";
            return 10;
        }
        editor.dispatchSampleMapMiddleDragForTest({5.0f, 5.0f}, {-65.0f, -45.0f});
        const auto outsidePanViewport = editor.sampleMapViewportForTest();
        if (std::abs(outsidePanViewport[1] - zoomedViewport[1]) > 1.0e-5f
            || std::abs(outsidePanViewport[2] - zoomedViewport[2]) > 1.0e-5f)
        {
            std::cerr << "Middle-button dragging outside the acoustic plot changed its viewport.\n";
            return 11;
        }
        editor.dispatchSampleMapMiddleDragForTest(previewPoint,
            previewPoint - juce::Point<float>(70.0f, 50.0f));
        const auto pannedViewport = editor.sampleMapViewportForTest();
        if (std::abs(pannedViewport[1] - zoomedViewport[1]) < 1.0e-5f
            && std::abs(pannedViewport[2] - zoomedViewport[2]) < 1.0e-5f)
        {
            std::cerr << "Middle-button dragging did not pan the acoustic map.\n";
            return 12;
        }

        const auto addFamilyFixture = [&editor](const juce::String& name, const juce::String& category)
        {
            ScannedSample sample;
            sample.file = juce::File("C:/MagDrumTestFixtures/Family/" + name);
            sample.durationSeconds = 0.35;
            sample.channels = 1;
            sample.sampleRate = 44100.0;
            sample.format = "WAV";
            sample.category = category;
            return editor.database.insertSample(sample);
        };
        if (!addFamilyFixture("Family Fixture Kick One Shot.wav", "Kick")
            || !addFamilyFixture("Family Fixture Bass Beat Loop.wav", "Unknown")
            || !addFamilyFixture("Family Fixture Synth Loop.wav", "Unknown"))
        {
            std::cerr << "Could not seed the content-family map fixtures.\n";
            return 13;
        }
        editor.sampleSearch.setText("Family Fixture", juce::dontSendNotification);
        const std::array<std::pair<int, juce::String>, 3> familyFixtures{{
            {2, "Family Fixture Kick One Shot.wav"},
            {5, "Family Fixture Bass Beat Loop.wav"},
            {7, "Family Fixture Synth Loop.wav"}}};
        struct FamilyFilterCheckState { size_t index = 0; bool matches = true; };
        const auto checkState = std::make_shared<FamilyFilterCheckState>();
        const auto familyDeadline = juce::Time::getMillisecondCounter() + 10000;
        auto poll = std::make_shared<std::function<void()>>();
        const auto selectFamily = [&editor, &familyFixtures](size_t index)
        {
            editor.categoryFilter.setSelectedId(1, juce::dontSendNotification);
            editor.contentTypeFilter.setSelectedId(familyFixtures[index].first, juce::dontSendNotification);
            editor.refreshSamples();
        };
        selectFamily(0);
        *poll = [&editor, &familyFixtures, checkState, familyDeadline, poll, selectFamily]
        {
            if (juce::Time::getMillisecondCounter() >= familyDeadline)
            {
                checkState->matches = false;
                juce::MessageManager::getInstance()->stopDispatchLoop();
                return;
            }
            if (!editor.sampleBrowseQueryPending && !editor.sampleMapQueryPending)
            {
                const auto& expectedName = familyFixtures[checkState->index].second;
                const bool matches = editor.samples.size() == 1 && editor.sampleMapSamples.size() == 1
                    && editor.samples.getFirst().file.getFileName() == expectedName
                    && editor.sampleMapSamples.getFirst().file.getFileName() == expectedName;
                if (!matches)
                {
                    checkState->matches = false;
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                    return;
                }
                if (++checkState->index == familyFixtures.size())
                {
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                    return;
                }
                selectFamily(checkState->index);
            }
            juce::Timer::callAfterDelay(20, *poll);
        };
        juce::Timer::callAfterDelay(20, *poll);
        juce::MessageManager::getInstance()->runDispatchLoop();
        *poll = {};
        poll.reset();
        if (!checkState->matches)
        {
            std::cerr << "The sample-family selection was not applied identically to the list and acoustic map.\n";
            return 14;
        }

        editor.selectedNote = 36;
        editor.buttonClicked(&editor.assignButton);
        const auto assignmentDeadline = juce::Time::getMillisecondCounter() + 10000;
        while (editor.sampleLoadWorkers.getNumJobs() != 0
               && juce::Time::getMillisecondCounter() < assignmentDeadline)
        {
            juce::Thread::sleep(10);
        }
        if (editor.sampleLoadWorkers.getNumJobs() != 0)
        {
            std::cerr << "Assigning the off-page map sample did not finish.\n";
            return 4;
        }
        const auto assignedLayers = editor.processor.getSampleLayersForNote(36);
        if (assignedLayers.size() != 1 || juce::File(assignedLayers.front().path) != expectedFile)
        {
            std::cerr << "The sample selected beyond the first list page was not assigned to the selected pad (layers: "
                      << assignedLayers.size();
            if (!assignedLayers.empty())
                std::cerr << ", path: " << assignedLayers.front().path.toStdString();
            std::cerr << ", expected: " << expectedFile.getFullPathName().toStdString() << ").\n";
            return 5;
        }
        return 0;
    }

    static bool mappingNotePlaceholderMatchesLanguage(const MagDrumAudioProcessorEditor& editor)
    {
        return editor.mappingNote.getTextToShowWhenEmpty() == magdrum::ui::localisedText("MIDI 0-127");
    }

    static int renderPagesOffscreen(MagDrumAudioProcessorEditor& editor, const juce::File& outputDirectory)
    {
        static constexpr std::array<std::pair<int, const char*>, 5> pages{{
            {0, "sample-library"}, {1, "drum-machine"}, {2, "sequencer"},
            {3, "midi-maps"}, {4, "plugin-scanner"}}};
        editor.setSize(1280, 800);
        juce::PNGImageFormat png;
        for (const auto& [page, name] : pages)
        {
            editor.setPage(page);
            editor.resized();
            const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
            if (!image.isValid())
            {
                std::cerr << "Offscreen VST page rendering returned an empty image: " << name << ".\n";
                return 1;
            }
            const auto file = outputDirectory.getChildFile(juce::String(name) + ".png");
            if (file.existsAsFile() && !file.deleteFile())
            {
                std::cerr << "Could not replace offscreen VST page image: " << file.getFullPathName() << ".\n";
                return 2;
            }
            std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
            if (stream == nullptr || !stream->openedOk() || !png.writeImageToStream(image, *stream))
            {
                std::cerr << "Could not save offscreen VST page image: " << file.getFullPathName() << ".\n";
                return 2;
            }
            stream->flush();
        }
        editor.setPage(1);
        editor.buttonClicked(&editor.phaserTab);
        const auto phaserImage = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
        const auto phaserFile = outputDirectory.getChildFile("drum-machine-phaser.png");
        if (!phaserImage.isValid() || (phaserFile.existsAsFile() && !phaserFile.deleteFile()))
        {
            std::cerr << "Offscreen Phaser-panel rendering failed.\n";
            return 3;
        }
        std::unique_ptr<juce::FileOutputStream> phaserStream(phaserFile.createOutputStream());
        if (phaserStream == nullptr || !phaserStream->openedOk() || !png.writeImageToStream(phaserImage, *phaserStream))
        {
            std::cerr << "Could not save the offscreen Phaser-panel image: " << phaserFile.getFullPathName() << ".\n";
            return 3;
        }
        phaserStream->flush();
        const auto renderMinimumEffect = [&](juce::Button* tab, const juce::String& name)
        {
            editor.setSize(1000, 620);
            editor.setPage(1);
            editor.buttonClicked(tab);
            const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
            const auto file = outputDirectory.getChildFile(name + ".png");
            if (!image.isValid() || (file.existsAsFile() && !file.deleteFile()))
                return false;
            std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
            if (stream == nullptr || !stream->openedOk() || !png.writeImageToStream(image, *stream))
                return false;
            stream->flush();
            return true;
        };
        if (!renderMinimumEffect(&editor.compressorTab, "drum-machine-minimum-compressor")
            || !renderMinimumEffect(&editor.phaserTab, "drum-machine-minimum-phaser"))
        {
            std::cerr << "Offscreen minimum-size effects-panel rendering failed.\n";
            return 4;
        }
        editor.setSize(1280,800);editor.setPage(2);editor.sequenceEvents.clear();
        DrumEvent renderedEvent;renderedEvent.instrument=DrumInstrument::Kick;renderedEvent.articulation=DrumArticulation::Default;
        renderedEvent.velocity=100;renderedEvent.positionBeats=0.0;renderedEvent.durationBeats=0.25;renderedEvent.midiNote=36;
        editor.sequenceEvents.push_back(renderedEvent);editor.sequenceEventsInitialised=true;editor.openMidiEventEditor();
        const auto subviewImage=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
        const auto subviewFile=outputDirectory.getChildFile("sequencer-midi-events-in-app.png");
        if(!subviewImage.isValid()||(subviewFile.existsAsFile()&&!subviewFile.deleteFile()))return 5;
        std::unique_ptr<juce::FileOutputStream> subviewStream(subviewFile.createOutputStream());
        if(subviewStream==nullptr||!subviewStream->openedOk()||!png.writeImageToStream(subviewImage,*subviewStream))return 5;
        subviewStream->flush();editor.closeInAppSubview();
        editor.chooseFileInApp(magdrum::ui::localisedText("Choose a drum sample folder"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),juce::String(),
            juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,{});
        auto* picker=editor.inAppSubviewContentForTest();
        if(picker==nullptr||picker->getNumChildComponents()!=2)
        {
            std::cerr<<"The in-app file picker did not render as one embedded browser and one select action.\n";
            return 6;
        }
        bool hasSelectAction=false;
        for(int child=0;child<picker->getNumChildComponents();++child)
            if(const auto* button=dynamic_cast<const juce::TextButton*>(picker->getChildComponent(child)))
                hasSelectAction=button->getButtonText()==magdrum::ui::localisedText("Select");
        if(!hasSelectAction)return 7;
        const std::function<bool(juce::Component*,const juce::String&)> hasLabel =
            [&](juce::Component* component,const juce::String& expected)
            {
                if(const auto* label=dynamic_cast<juce::Label*>(component))
                    if(label->getText().contains(expected))return true;
                for(int child=0;child<component->getNumChildComponents();++child)
                    if(hasLabel(component->getChildComponent(child),expected))return true;
                return false;
            };
        for(const auto language:{magdrum::ui::Language::english,magdrum::ui::Language::german,
            magdrum::ui::Language::french,magdrum::ui::Language::spanish,magdrum::ui::Language::swedish,
            magdrum::ui::Language::norwegian,magdrum::ui::Language::danish,magdrum::ui::Language::finnish})
        {
            magdrum::ui::setLanguage(language,false);
            if(!hasLabel(picker,magdrum::ui::localisedText("Folder")))
            {
                std::cerr<<"The embedded folder picker label did not follow the selected language.\n";
                magdrum::ui::setLanguage(magdrum::ui::Language::english,false);
                return 9;
            }
            bool translatedSelect=false;
            for(int child=0;child<picker->getNumChildComponents();++child)
                if(const auto* button=dynamic_cast<const juce::TextButton*>(picker->getChildComponent(child)))
                    translatedSelect=button->getButtonText()==magdrum::ui::localisedText("Select");
            if(!translatedSelect)
            {
                std::cerr<<"The embedded picker action did not follow the selected language.\n";
                magdrum::ui::setLanguage(magdrum::ui::Language::english,false);
                return 10;
            }
        }
        magdrum::ui::setLanguage(magdrum::ui::Language::english,false);
        const auto pickerImage=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
        const auto pickerFile=outputDirectory.getChildFile("in-app-file-picker.png");
        if(!pickerImage.isValid()||(pickerFile.existsAsFile()&&!pickerFile.deleteFile()))return 8;
        std::unique_ptr<juce::FileOutputStream> pickerStream(pickerFile.createOutputStream());
        if(pickerStream==nullptr||!pickerStream->openedOk()||!png.writeImageToStream(pickerImage,*pickerStream))return 8;
        pickerStream->flush();editor.closeInAppSubview();
        return 0;
    }

    static int checksDr660ImportedNotesReachTheirOwnSequencerLanes(MagDrumAudioProcessorEditor& editor)
    {
        constexpr std::array<int, 16> notes{49,57,53,51,50,47,45,43,37,39,42,46,36,38,44,56};
        constexpr std::array<int, 16> lanes{6,9,8,5,10,11,12,4,13,7,2,3,0,1,14,15};
        const auto generalMidi = MidiMappingProfile::generalMidi();
        editor.sequenceEvents.clear();
        for (size_t index = 0; index < notes.size(); ++index)
        {
            auto event = generalMidi.fromMidi(static_cast<std::uint8_t>(notes[index]), 100,
                                              static_cast<double>(index) / 4.0, 0.125);
            if (!event)
            {
                std::cerr << "A default DR-660 MIDI note was not recognized by the General MIDI profile.\n";
                return 1;
            }
            editor.sequenceEvents.push_back(*event);
        }
        editor.syncSequencerGridFromEvents();
        for (int step = 0; step < static_cast<int>(lanes.size()); ++step)
            for (int lane = 0; lane < static_cast<int>(editor.sequenceStepActive.size()); ++lane)
                if (editor.sequenceStepActive[static_cast<size_t>(lane)][static_cast<size_t>(step)]
                    != (lane == lanes[static_cast<size_t>(step)]))
                {
                    std::cerr << "DR-660 MIDI note " << notes[static_cast<size_t>(step)]
                              << " was routed to the wrong sequencer lane.\n";
                    return 2;
                }
        return 0;
    }

    static int checksIndependentPadSoundControls(MagDrumAudioProcessorEditor& editor,
                                                 MagDrumAudioProcessor& processor)
    {
        auto kick = processor.getNoteParameters(36);
        kick.gainDb = -8.0f;
        kick.pan = -0.45f;
        kick.tuneSemitones = 3.5f;
        kick.driveDb = 5.0f;
        kick.chokeGroup = 3;
        processor.setNoteParameters(36, kick);
        auto snare = processor.getNoteParameters(38);
        snare.gainDb = 2.5f;
        snare.pan = 0.65f;
        snare.eqMidDb = 4.0f;
        processor.setNoteParameters(38, snare);

        editor.setNote(36);
        if (std::abs(editor.gainSlider.getValue() + 8.0) > 0.05
            || std::abs(editor.panSlider.getValue() + 0.45) > 0.02
            || std::abs(editor.tuneSlider.getValue() - 3.5) > 0.1
            || std::abs(editor.driveSlider.getValue() - 5.0) > 0.1
            || editor.chokeGroupSelect.getSelectedId() != 4)
        {
            std::cerr << "Kick controls did not load that pad's saved sound settings.\n";
            return 1;
        }

        editor.gainSlider.setValue(-4.5, juce::sendNotificationSync);
        if (std::abs(processor.getNoteParameters(36).gainDb + 4.5f) > 0.05f
            || std::abs(processor.getNoteParameters(38).gainDb - 2.5f) > 0.05f)
        {
            std::cerr << "Editing Kick gain did not stay isolated to the selected pad.\n";
            return 2;
        }
        editor.chokeGroupSelect.setSelectedId(6, juce::sendNotificationSync);
        if (processor.getNoteParameters(36).chokeGroup != 5
            || processor.getNoteParameters(38).chokeGroup != 0)
        {
            std::cerr << "Editing Kick choke group did not remain isolated to that pad.\n";
            return 4;
        }

        editor.setNote(38);
        if (std::abs(editor.gainSlider.getValue() - 2.5) > 0.05
            || std::abs(editor.panSlider.getValue() - 0.65) > 0.02
            || std::abs(editor.eqMidSlider.getValue() - 4.0) > 0.1)
        {
            std::cerr << "Snare controls did not load that pad's independent sound settings.\n";
            return 3;
        }
        return 0;
    }

    static int checksPadAutomationGesture(MagDrumAudioProcessorEditor& editor)
    {
        editor.setNote(36);
        auto* gain = findHostFloatParameter(editor.processor, "magdrum_pad_36_gain");
        if (gain == nullptr || !editor.gainSlider.onDragStart || !editor.gainSlider.onDragEnd)
            return 1;
        ParameterGestureProbe gesture;
        gain->addListener(&gesture);
        editor.gainSlider.onDragStart();
        editor.gainSlider.setValue(-3.5, juce::sendNotificationSync);
        editor.gainSlider.onDragEnd();
        gain->removeListener(&gesture);
        if (gesture.starts != 1 || gesture.ends != 1
            || std::abs(editor.processor.getNoteParameters(36).gainDb + 3.5f) > 0.05f)
        {
            std::cerr << "The Drum Machine slider did not record a complete Cubase automation gesture.\n";
            return 2;
        }
        editor.gainSlider.setValue(-4.5, juce::dontSendNotification);
        auto parameters = editor.processor.getNoteParameters(36);
        parameters.gainDb = -4.5f;
        editor.processor.setNoteParameters(36, parameters);
        return 0;
    }

    static int checksDrumMachineControlLayout(MagDrumAudioProcessorEditor& editor)
    {
        editor.setPage(1);
        const auto controlsFit = [](juce::Rectangle<int> panel,
                                    std::initializer_list<const juce::Component*> controls)
        {
            return std::all_of(controls.begin(), controls.end(), [&panel](const auto* control)
            {
                return control != nullptr && panel.contains(control->getBounds());
            });
        };
        const auto verifyCurrentSize = [&editor, &controlsFit]()
        {
            editor.resized();
            const auto local = editor.getLocalBounds();
            const auto inside = [&local](juce::Rectangle<int> area)
            {
                return !area.isEmpty() && local.contains(area);
            };
            if (!inside(editor.drumPadTrayBounds) || !inside(editor.drumEditorPanelBounds)
            || !inside(editor.drumWaveformPanelBounds) || !inside(editor.padMixPanelBounds)
            || !inside(editor.padShapePanelBounds) || !inside(editor.padEqPanelBounds)
            || !inside(editor.padFxPanelBounds))
            {
                std::cerr << "Drum Machine instrument panels extend outside the editor.\n";
                return 1;
            }
            if (!editor.drumEditorPanelBounds.contains(editor.manageLayersButton.getBounds())
                || editor.manageLayersButton.getBounds().intersects(editor.sampleDetails.getBounds()))
            {
                std::cerr << "The sample-layer editor button must fit beside the selected-pad sample details.\n";
                return 6;
            }
            if (editor.drumPadTrayBounds.intersects(editor.drumEditorPanelBounds)
            || !editor.drumEditorPanelBounds.contains(editor.drumWaveformPanelBounds)
            || !editor.drumEditorPanelBounds.contains(editor.padMixPanelBounds)
            || !editor.drumEditorPanelBounds.contains(editor.padShapePanelBounds)
            || !editor.drumEditorPanelBounds.contains(editor.padEqPanelBounds)
            || !editor.drumEditorPanelBounds.contains(editor.padFxPanelBounds))
            {
                std::cerr << "Drum Machine instrument panels overlap or escape their editor panel.\n";
                return 2;
            }
            if (editor.drumWaveformPanelBounds.getBottom() > editor.padMixHeading.getY()
            || editor.padMixPanelBounds.getBottom() > editor.padShapePanelBounds.getY()
            || editor.padShapePanelBounds.getBottom() > editor.padEqPanelBounds.getY()
            || editor.padEqPanelBounds.getBottom() > editor.padFxPanelBounds.getY())
            {
                std::cerr << "Drum Machine waveform and pad-control groups are out of order.\n";
                return 3;
            }
            for (const auto& pad : editor.pads)
                if (!editor.drumPadTrayBounds.contains(pad.getBounds()))
                {
                    std::cerr << "A DR-660 pad is outside the pad tray.\n";
                    return 4;
                }
            if (!controlsFit(editor.padMixPanelBounds,
                         {&editor.padMixHeading, &editor.gainLabel, &editor.gainSlider,
                          &editor.panLabel, &editor.panSlider, &editor.tuneLabel,
                          &editor.tuneSlider, &editor.driveLabel, &editor.driveSlider})
            || !controlsFit(editor.padShapePanelBounds,
                            {&editor.padShapeHeading, &editor.attackLabel, &editor.attackSlider,
                             &editor.decayLabel, &editor.decaySlider, &editor.sustainLabel,
                             &editor.sustainSlider, &editor.releaseLabel, &editor.releaseSlider,
                             &editor.filterCutoffLabel, &editor.filterCutoffSlider,
                             &editor.filterResonanceLabel, &editor.filterResonanceSlider})
            || !controlsFit(editor.padEqPanelBounds,
                            {&editor.padEqHeading, &editor.eqLowLabel, &editor.eqLowSlider,
                             &editor.eqMidLabel, &editor.eqMidSlider,
                             &editor.eqHighLabel, &editor.eqHighSlider})
            || !controlsFit(editor.padFxPanelBounds,
                            {&editor.padFxHeading, &editor.compressorTab, &editor.phaserTab,
                             &editor.compressorThresholdLabel, &editor.compressorThresholdSlider,
                             &editor.compressorRatioLabel, &editor.compressorRatioSlider,
                             &editor.compressorAttackLabel, &editor.compressorAttackSlider,
                             &editor.compressorReleaseLabel, &editor.compressorReleaseSlider,
                             &editor.compressorMakeupLabel, &editor.compressorMakeupSlider}))
            {
                std::cerr << "A per-pad instrument control is outside its visual group.\n";
                return 5;
            }
            return 0;
        };

        editor.setSize(1000, 620);
        if (const int minimumSizeResult = verifyCurrentSize(); minimumSizeResult != 0)
            return minimumSizeResult;
        editor.buttonClicked(&editor.phaserTab);
        if (!editor.phaserRateLabel.isVisible() || !editor.phaserDepthSlider.isVisible()
            || !controlsFit(editor.padFxPanelBounds,
                            {&editor.padFxHeading, &editor.compressorTab, &editor.phaserTab,
                             &editor.phaserRateLabel, &editor.phaserRateSlider,
                             &editor.phaserDepthLabel, &editor.phaserDepthSlider,
                             &editor.phaserFeedbackLabel, &editor.phaserFeedbackSlider,
                             &editor.phaserMixLabel, &editor.phaserMixSlider}))
        {
            std::cerr << "Phaser tab controls are hidden or outside their panel: visible="
                      << editor.phaserRateLabel.isVisible() << "," << editor.phaserDepthSlider.isVisible()
                      << " panel=" << editor.padFxPanelBounds.toString().toStdString()
                      << " rate=" << editor.phaserRateLabel.getBounds().toString().toStdString()
                      << " depth=" << editor.phaserDepthSlider.getBounds().toString().toStdString()
                      << " header=" << editor.padFxHeading.getBounds().toString().toStdString()
                      << " tabs=" << editor.compressorTab.getBounds().toString().toStdString() << ","
                      << editor.phaserTab.getBounds().toString().toStdString() << "\n";
            return 6;
        }
        editor.setSize(1120, 760);
        if (const int standardSizeResult = verifyCurrentSize(); standardSizeResult != 0)
            return standardSizeResult;
        return 0;
    }

    static int checksTriggeredMidiPadGlow(MagDrumAudioProcessorEditor& editor,
                                          MagDrumAudioProcessor& processor)
    {
        editor.setPage(1);
        processor.prepareToPlay(44100.0, 128);
        juce::AudioBuffer<float> audio(2, 128);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(audio, midi);
        editor.updatePadActivity();
        if (editor.pads[12].getFlashAmount() <= 0.0f
            || editor.pads[13].getFlashAmount() > 0.0f)
        {
            std::cerr << "Incoming MIDI did not glow only the matching DR-660 pad.\n";
            return 1;
        }

        editor.setPage(0);
        juce::MidiBuffer hiddenPageMidi;
        hiddenPageMidi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(audio, hiddenPageMidi);
        editor.updatePadActivity();
        editor.setPage(1);
        editor.updatePadActivity();
        if (editor.pads[13].getFlashAmount() > 0.0f)
        {
            std::cerr << "MIDI received outside Drum Machine was replayed as a delayed pad glow.\n";
            return 2;
        }
        return 0;
    }

    static int checksPluginFolderChooserVisibility(MagDrumAudioProcessorEditor& editor)
    {
        if (editor.choosePluginFolderButton.isVisible())
        {
            std::cerr << "Plugin-folder chooser is visible outside Plugin Scanner.\n";
            return 1;
        }
        editor.setPage(4);
        if (!editor.choosePluginFolderButton.isVisible())
        {
            std::cerr << "Plugin-folder chooser is hidden in Plugin Scanner.\n";
            return 2;
        }
        editor.pluginInventoryScanRunning = true;
        editor.resized();
        if (!editor.pluginInventoryProgress.isVisible()
            || !editor.getLocalBounds().contains(editor.pluginInventoryProgress.getBounds())
            || editor.pluginInventoryProgress.getX() < editor.scanPluginInventoryButton.getRight()
            || editor.pluginInventoryProgress.getWidth() < 100)
        {
            std::cerr << "Instrument-scan progress is missing or outside the scanner toolbar.\n";
            return 4;
        }
        editor.pluginInventoryProgressFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("magdrum-vst3-progress-test-" + juce::Uuid().toString() + ".json");
        editor.pluginInventoryProgressFile.replaceWithText("{\"completed\":40,\"total\":100,\"current\":\"Waves Factory\"}");
        editor.pollPluginInventoryProgress();
        if (std::abs(editor.pluginInventoryProgressValue - 0.4) > 0.001
            || !editor.status.getText().contains("40%")
            || !editor.status.getText().contains("Waves Factory"))
        {
            std::cerr << "Instrument-scan progress did not report its completion percentage and current module.\n";
            editor.pluginInventoryProgressFile.deleteFile();
            editor.pluginInventoryProgressFile = {};
            editor.pluginInventoryScanRunning = false;
            return 5;
        }
        editor.pluginInventoryProgressFile.replaceWithText("{\"completed\":150,\"total\":100}");
        editor.pollPluginInventoryProgress();
        if (std::abs(editor.pluginInventoryProgressValue - 1.0) > 0.001)
        {
            std::cerr << "Instrument-scan progress did not clamp at 100 percent.\n";
            editor.pluginInventoryProgressFile.deleteFile();
            editor.pluginInventoryProgressFile = {};
            editor.pluginInventoryScanRunning = false;
            return 6;
        }
        editor.pluginInventoryProgressFile.deleteFile();
        editor.pluginInventoryProgressFile = {};
        editor.pluginInventoryScanRunning = false;
        editor.resized();
        if (editor.pluginInventoryProgress.isVisible())
        {
            std::cerr << "Instrument-scan progress stayed visible after the scan stopped.\n";
            return 7;
        }
        editor.buttonClicked(&editor.libraryTab);
        if (editor.choosePluginFolderButton.isVisible())
        {
            std::cerr << "Plugin-folder chooser stayed visible after leaving Plugin Scanner.\n";
            return 3;
        }
        return 0;
    }

    static int checksSampleScanProgress(MagDrumAudioProcessorEditor& editor)
    {
        editor.setPage(0);
        editor.scanning = true;
        editor.resized();
        if (!editor.sampleScanProgress.isVisible() || !editor.sampleScanProgressLabel.isVisible()
            || !editor.getLocalBounds().contains(editor.sampleScanProgress.getBounds())
            || !editor.getLocalBounds().contains(editor.sampleScanProgressLabel.getBounds()))
        {
            std::cerr << "Sample-scan progress is not visible in the Sample Library layout.\n";
            editor.scanning = false;
            editor.resized();
            return 1;
        }

        editor.updateSampleScanProgress(juce::File("Kick.wav"), 2, 5);
        if (std::abs(editor.sampleScanProgressValue - 0.4) > 0.001
            || editor.sampleScanProgressLabel.getText() != "40% - 2 / 5 - Kick.wav"
            || editor.status.getText() != "Scanning sample: Kick.wav")
        {
            std::cerr << "Sample-scan progress did not show percentage, count and current filename.\n";
            editor.scanning = false;
            editor.resized();
            return 2;
        }

        editor.updateSampleScanProgress(juce::File("Kick.wav"), 15, 10);
        if (std::abs(editor.sampleScanProgressValue - 1.0) > 0.001
            || editor.sampleScanProgressLabel.getText() != "100% - 15 / 10 - Kick.wav")
        {
            std::cerr << "Sample-scan progress did not clamp its bar at 100 percent.\n";
            editor.scanning = false;
            editor.resized();
            return 3;
        }

        editor.updateSampleScanProgress(juce::File("Unknown.wav"), 0, 0);
        if (editor.sampleScanProgressValue != -1.0
            || editor.sampleScanProgressLabel.getText() != "Unknown.wav")
        {
            std::cerr << "Indeterminate sample-scan progress did not show the current filename.\n";
            editor.scanning = false;
            editor.resized();
            return 4;
        }

        editor.scanning = false;
        editor.resized();
        if (editor.sampleScanProgress.isVisible() || editor.sampleScanProgressLabel.isVisible())
        {
            std::cerr << "Sample-scan progress remained visible after scanning stopped.\n";
            return 5;
        }

        const auto resumeFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("magdrum-sample-scan-resume-ui-test-" + juce::Uuid().toString());
        if (!resumeFolder.createDirectory().wasOk()) return 6;
        editor.lastSampleScanFolder = resumeFolder;
        editor.scanningDefaultLibraries = false;
        WavScanner::Stats cancelled;
        cancelled.cancelled = true;
        editor.finishSampleScan(cancelled, 0, {});
        const bool canResume = editor.scanButton.isEnabled()
            && editor.scanButton.getButtonText() == magdrum::ui::localisedText("Resume scan");
        editor.lastSampleScanFolder = {};
        editor.scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));
        editor.sampleScanProgressValue = 0.0;
        editor.sampleScanProgressLabel.setText(juce::String{}, juce::dontSendNotification);
        editor.status.setText(juce::String{}, juce::dontSendNotification);
        resumeFolder.deleteRecursively();
        if (!canResume)
        {
            std::cerr << "A cancelled sample-folder scan did not offer Resume scan.\n";
            return 7;
        }
        return 0;
    }

    static int checksDefaultMidiSearchCancellation(MagDrumAudioProcessorEditor& editor)
    {
        auto cancellation = std::make_shared<std::atomic<bool>>(false);
        editor.scanning = false;
        editor.scanningDefaultLibraries = true;
        editor.defaultMidiSearchCancellation = cancellation;
        editor.scanButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));
        editor.scanButton.setEnabled(true);
        editor.chooseSampleFolder();
        const bool cancelled = cancellation->load(std::memory_order_acquire)
            && !editor.scanButton.isEnabled()
            && editor.scanButton.getButtonText() == magdrum::ui::localisedText("Cancelling…")
            && editor.status.getText() == magdrum::ui::localisedText("Stopping the drum MIDI search…");
        editor.defaultMidiSearchCancellation.reset();
        editor.scanningDefaultLibraries = false;
        editor.scanButton.setEnabled(true);
        editor.scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));
        editor.status.setText(juce::String(), juce::dontSendNotification);
        return cancelled ? 0 : 1;
    }

    static int checksSmpteMidiImport(MagDrumAudioProcessorEditor& editor,
                                     MagDrumAudioProcessor& processor)
    {
        juce::MidiFile midi;
        midi.setSmpteTimeFormat(25, 40);
        juce::MidiMessageSequence tempoTrack, drumTrack;
        tempoTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
        tempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(1000000), 1000.0);
        tempoTrack.addEvent(juce::MidiMessage::endOfTrack(), 2000.0);
        drumTrack.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(104)), 1000.0);
        drumTrack.addEvent(juce::MidiMessage::noteOff(10, 36), 1500.0);
        drumTrack.addEvent(juce::MidiMessage::endOfTrack(), 2000.0);
        tempoTrack.sort();
        drumTrack.sort();
        midi.addTrack(tempoTrack);
        midi.addTrack(drumTrack);

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("MagDrumPluginSmokeTest").getChildFile("plugin-smpte.mid");
        auto stream = file.createOutputStream();
        if (stream == nullptr || ! midi.writeTo(*stream))
            return 1;
        stream->flush();
        stream.reset();

        editor.loadMidiPatternFile(file);
        if (! editor.sequenceEventsInitialised || editor.sequenceEvents.size() != 1
            || editor.sequenceEvents.front().instrument != DrumInstrument::Kick
            || editor.sequenceEvents.front().midiNote != 36
            || std::abs(editor.sequenceEvents.front().positionBeats - 2.0) > 0.001
            || std::abs(editor.sequenceEvents.front().durationBeats - 0.5) > 0.001
            || editor.sequenceMetadata.size() != 2
            || std::abs(editor.sequenceMetadata[1].positionBeats - 2.0) > 0.001
            || ! editor.reviewMidiButton.isEnabled() || ! editor.editMidiEventsButton.isEnabled())
        {
            std::cerr << "The VST MIDI editor did not import SMPTE events on the tempo map.\n";
            return 2;
        }
        const auto importedEventRow = magdrum::plugin::midiEventRowText(
            editor.sequenceEvents.front(), 0);
        if (! importedEventRow.contains("MIDI 36 (C1)"))
        {
            std::cerr << "The MIDI event list did not show the imported source pitch.\n";
            return 7;
        }
        const auto state = processor.getPluginEditorState();
        if (state == nullptr || state->getNumChildElements() == 0
            || state->getChildByName("EVENT") == nullptr
            || state->getChildByName("EVENT")->getIntAttribute("midiNote", -1) != 36)
            return 3;

        editor.sequenceEvents.clear();
        editor.sequenceEventsInitialised = false;
        editor.restorePluginEditorState();
        if (editor.sequenceEvents.size() != 1 || editor.sequenceEvents.front().midiNote != 36)
        {
            std::cerr << "The VST editor did not restore an imported source MIDI pitch.\n";
            return 4;
        }

        auto legacyState = std::make_unique<juce::XmlElement>(*state);
        auto* legacyEvent = legacyState->getChildByName("EVENT");
        if (legacyEvent == nullptr) return 5;
        legacyEvent->removeAttribute("midiNote");
        processor.setPluginEditorState(*legacyState);
        editor.sequenceEvents.clear();
        editor.sequenceEventsInitialised = false;
        editor.restorePluginEditorState();
        if (editor.sequenceEvents.size() != 1 || editor.sequenceEvents.front().midiNote != -1)
        {
            std::cerr << "A legacy VST project without a source MIDI pitch did not restore safely.\n";
            return 6;
        }
        if (magdrum::plugin::midiEventRowText(editor.sequenceEvents.front(), 0).contains("MIDI "))
        {
            std::cerr << "The MIDI event list showed a source pitch that legacy state did not contain.\n";
            return 8;
        }

        return 0;
    }

    static int checksLiveLanguageSwitching(MagDrumAudioProcessorEditor& editor,
                                           const juce::File& firstSample, const juce::File& secondSample)
    {
        using magdrum::ui::Language;
        MagDrumAudioProcessor::SampleLayerInfo previewLayer;
        previewLayer.path = firstSample.getFullPathName();
        editor.handleSampleLayerMenuAction(36, previewLayer, 0);
        if (editor.status.getText() != magdrum::ui::localisedText("Previewing ") + firstSample.getFileName())
        {
            std::cerr << "The drum-machine layer menu did not start preview for the selected layer.\n";
            return 11;
        }
        editor.stopSamplePreview();
        const juce::Point<int> originalSize{editor.getWidth(), editor.getHeight()};
        const int originalPage = editor.page;
        editor.setSize(1000, 620);
        editor.setPage(1);
        if (!editor.drumEditorPanelBounds.contains(editor.selectedPadTitle.getBounds())
            || !editor.drumEditorPanelBounds.contains(editor.chokeGroupLabel.getBounds())
            || !editor.drumEditorPanelBounds.contains(editor.chokeGroupSelect.getBounds())
            || !editor.drumEditorPanelBounds.contains(editor.manageLayersButton.getBounds())
            || editor.manageLayersButton.getBounds().intersects(editor.sampleDetails.getBounds())
            || editor.chokeGroupLabel.getBounds().getCentreY()
                != editor.chokeGroupSelect.getBounds().getCentreY())
        {
            std::cerr << "The choke selector must remain aligned in the pad header at minimum editor size.\n";
            return 9;
        }
        const std::array<const juce::Slider*, 12> padSliders{
            &editor.gainSlider, &editor.panSlider, &editor.tuneSlider, &editor.driveSlider,
            &editor.attackSlider, &editor.decaySlider, &editor.sustainSlider, &editor.releaseSlider,
            &editor.filterCutoffSlider, &editor.eqLowSlider, &editor.eqMidSlider, &editor.eqHighSlider};
        for (const auto* slider : padSliders)
        {
            if (slider->getWidth() <= 0 || slider->getTextBoxWidth() > slider->getWidth())
            {
                std::cerr << "A pad control value field exceeds its column at the minimum VST editor size.\n";
                editor.setPage(originalPage);
                editor.setSize(originalSize.x, originalSize.y);
                return 7;
            }
        }
        editor.setPage(originalPage);
        editor.setSize(originalSize.x, originalSize.y);
        editor.openSampleComparisonWindow(firstSample, secondSample);
        if (editor.inAppSubviewContentForTest() == nullptr)
            return 2;
        auto* comparisonContent = editor.inAppSubviewContentForTest();
        const std::array<Language, 8> languages{
            Language::english, Language::german, Language::french, Language::spanish,
            Language::swedish, Language::norwegian, Language::danish, Language::finnish};
        struct LabelCheck { const char* name; juce::String visible, expected; };
        for (const auto language : languages)
        {
            magdrum::ui::setLanguage(language, false);
            const std::array<LabelCheck, 45> checks{{
                {"sample-library tab", editor.libraryTab.getButtonText(), magdrum::ui::localisedText("SAMPLE LIBRARY")},
                {"drum-machine tab", editor.machineTab.getButtonText(), magdrum::ui::localisedText("DRUM MACHINE")},
                {"sequencer tab", editor.sequencerTab.getButtonText(), magdrum::ui::localisedText("SEQUENCER")},
                {"MIDI-maps tab", editor.mapsTab.getButtonText(), magdrum::ui::localisedText("MIDI MAPS")},
                {"plugin-scanner tab", editor.pluginScannerTab.getButtonText(), magdrum::ui::localisedText("PLUGIN SCANNER")},
                {"sample search placeholder", editor.sampleSearch.getTextToShowWhenEmpty(), magdrum::ui::localisedText("Search indexed samples")},
                {"sample scan button", editor.scanButton.getButtonText(), magdrum::ui::localisedText("Add sample folder")},
                {"sample load button", editor.loadButton.getButtonText(), magdrum::ui::localisedText("Load sample")},
                {"sequencer play button", editor.playSequenceButton.getButtonText(), magdrum::ui::localisedText("Play")},
                {"MIDI import button", editor.importMidiButton.getButtonText(), magdrum::ui::localisedText("Import MIDI")},
                {"mapping action button", editor.setMappingButton.getButtonText(), magdrum::ui::localisedText("Set mapping")},
                {"plugin-folder button", editor.choosePluginFolderButton.getButtonText(), magdrum::ui::localisedText("Choose VST3 folder")},
                {"sample category filter", editor.categoryFilter.getText(), magdrum::ui::localisedText("All categories")},
                {"sample content filter", editor.contentTypeFilter.getText(), magdrum::ui::localisedText("Drum one-shots")},
                {"all content types option", editor.contentTypeFilter.getItemText(0), magdrum::ui::localisedText("All sounds")},
                {"bass one-shot family filter", editor.contentTypeFilter.getItemText(3), magdrum::ui::localisedText("Bass one-shots")},
                {"bass loop family filter", editor.contentTypeFilter.getItemText(4), magdrum::ui::localisedText("Bass loops & beats")},
                {"synth one-shot family filter", editor.contentTypeFilter.getItemText(5), magdrum::ui::localisedText("Synth one-shots")},
                {"synth loop family filter", editor.contentTypeFilter.getItemText(6), magdrum::ui::localisedText("Synth loops")},
                {"other loop family filter", editor.contentTypeFilter.getItemText(7), magdrum::ui::localisedText("Other loops")},
                {"sample sort filter", editor.sortFilter.getText(), magdrum::ui::localisedText("Name A-Z")},
                {"pad attack control", editor.attackLabel.getText(), magdrum::ui::localisedText("PAD ATTACK")},
                {"pad decay control", editor.decayLabel.getText(), magdrum::ui::localisedText("PAD DECAY")},
                {"pad sustain control", editor.sustainLabel.getText(), magdrum::ui::localisedText("PAD SUSTAIN")},
                {"pad release control", editor.releaseLabel.getText(), magdrum::ui::localisedText("PAD RELEASE")},
                {"pad filter control", editor.filterCutoffLabel.getText(), magdrum::ui::localisedText("FILTER CUTOFF")},
                {"pad filter resonance", editor.filterResonanceLabel.getText(), magdrum::ui::localisedText("FILTER Q")},
                {"pad filter resonance tooltip", editor.filterResonanceSlider.getTooltip(),
                    magdrum::ui::localisedText("Filter Q controls resonance. Higher Q emphasizes frequencies near the cutoff.")},
                {"pad choke control", editor.chokeGroupLabel.getText(), magdrum::ui::localisedText("CHOKE")},
                {"pad choke tooltip", editor.chokeGroupSelect.getTooltip(),
                    magdrum::ui::localisedText("Pads in the same choke group stop one another when triggered.")},
                {"pad layers button", editor.manageLayersButton.getButtonText(),
                    magdrum::ui::localisedText("Layers")},
                {"pad layers tooltip", editor.manageLayersButton.getTooltip(),
                    magdrum::ui::localisedText("Manage velocity layers for the selected pad.")},
                {"pad drive control", editor.driveLabel.getText(), magdrum::ui::localisedText("PAD DRIVE")},
                {"pad low EQ control", editor.eqLowLabel.getText(), magdrum::ui::localisedText("EQ LOW")},
                {"pad mid EQ control", editor.eqMidLabel.getText(), magdrum::ui::localisedText("EQ MID")},
                {"pad high EQ control", editor.eqHighLabel.getText(), magdrum::ui::localisedText("EQ HIGH")},
                {"pad mix heading", editor.padMixHeading.getText(), magdrum::ui::localisedText("PAD MIX")},
                {"pad envelope heading", editor.padShapeHeading.getText(), magdrum::ui::localisedText("ENVELOPE / FILTER")},
                {"pad EQ heading", editor.padEqHeading.getText(), magdrum::ui::localisedText("PAD EQUALIZER")},
                {"pad effects heading", editor.padFxHeading.getText(), magdrum::ui::localisedText("COMPRESSOR / PHASER")},
                {"compressor selector", editor.compressorTab.getButtonText(), magdrum::ui::localisedText("COMPRESSOR")},
                {"phaser selector", editor.phaserTab.getButtonText(), magdrum::ui::localisedText("PHASER")},
                {"compressor threshold", editor.compressorThresholdLabel.getText(), magdrum::ui::localisedText("COMP THRESHOLD")},
                {"phaser rate", editor.phaserRateLabel.getText(), magdrum::ui::localisedText("PHASER RATE")},
                {"sample trim tooltip", editor.currentWaveformTooltip(), magdrum::ui::localisedText("Drag waveform markers to set the sample playback range. Double-click to reset.")}
            }};
            for (const auto& check : checks)
            {
                if (check.visible != check.expected)
                {
                    std::cerr << "Live localization failed for " << magdrum::ui::languageName(language)
                              << " " << check.name << ": expected '" << check.expected
                              << "', got '" << check.visible << "'.\n";
                    magdrum::ui::setLanguage(Language::english, false);
                    return 1;
                }
            }
            int previewButtons = 0, stopButtons = 0;
            for (int childIndex = 0; childIndex < comparisonContent->getNumChildComponents(); ++childIndex)
            {
                auto* button = dynamic_cast<juce::TextButton*>(comparisonContent->getChildComponent(childIndex));
                if (button == nullptr) continue;
                if (button->getButtonText() == magdrum::ui::localisedText("Preview")) ++previewButtons;
                else if (button->getButtonText() == magdrum::ui::localisedText("Stop")) ++stopButtons;
                else
                {
                    std::cerr << "Live localization failed for " << magdrum::ui::languageName(language)
                              << " sample-comparison button: '" << button->getButtonText() << "'.\n";
                    magdrum::ui::setLanguage(Language::english, false);
                    return 3;
                }
            }
            if (previewButtons != 2 || stopButtons != 2
                || editor.inAppSubviewTitleForTest() != magdrum::ui::localisedText("Compare drum samples"))
            {
                std::cerr << "Live localization failed in the open sample-comparison window for "
                          << magdrum::ui::languageName(language) << ".\n";
                magdrum::ui::setLanguage(Language::english, false);
                return 4;
            }

        }
        editor.closeInAppSubview();
        editor.openMidiEventEditor();
        if (editor.inAppSubviewContentForTest() == nullptr)
            return 5;
        auto* midiEventContent = editor.inAppSubviewContentForTest();
        int midiEventTextFields = 0;
        for (int childIndex = 0; childIndex < midiEventContent->getNumChildComponents(); ++childIndex)
            if (const auto* field = dynamic_cast<const juce::TextEditor*>(midiEventContent->getChildComponent(childIndex)))
            {
                ++midiEventTextFields;
                if (!field->getJustificationType().testFlags(juce::Justification::verticallyCentred))
                {
                    std::cerr << "A MIDI-event editor text field is not vertically centred.\n";
                    return 9;
                }
            }
        if (midiEventTextFields != 3)
        {
            std::cerr << "Expected three MIDI-event editor text fields, found " << midiEventTextFields << ".\n";
            return 10;
        }
        for (const auto language : languages)
        {
            magdrum::ui::setLanguage(language, false);
            if (editor.inAppSubviewTitleForTest() != magdrum::ui::localisedText("Edit MIDI events"))
            {
                std::cerr << "The in-app MIDI event view title did not follow the selected language.\n";
                magdrum::ui::setLanguage(Language::english, false);
                return 11;
            }
        }
        magdrum::ui::setLanguage(Language::english, false);
        const int pageBeforeSubviewClose = editor.page;
        editor.closeInAppSubview();
        if (editor.inAppSubviewContentForTest() != nullptr || editor.page != pageBeforeSubviewClose)
        {
            std::cerr << "Closing the MIDI editor view should return to the same plugin page.\n";
            return 12;
        }
        editor.categoryFilter.setSelectedId(2, juce::sendNotificationSync);
        editor.contentTypeFilter.setSelectedId(4, juce::sendNotificationSync);
        if (editor.categoryFilter.isEnabled() || editor.categoryFilter.getSelectedId() != 1)
        {
            std::cerr << "Switching to Bass one-shots must clear and disable the drum-category filter.\n";
            return 281;
        }
        editor.contentTypeFilter.setSelectedId(6, juce::sendNotificationSync);
        if (editor.categoryFilter.isEnabled() || editor.categoryFilter.getSelectedId() != 1)
        {
            std::cerr << "Switching between non-drum families must keep the drum-category filter cleared.\n";
            return 282;
        }
        editor.contentTypeFilter.setSelectedId(2, juce::sendNotificationSync);
        if (!editor.categoryFilter.isEnabled() || editor.categoryFilter.getSelectedId() != 2)
        {
            std::cerr << "Returning to Drum one-shots must restore the previously selected drum category.\n";
            return 283;
        }
        editor.categoryFilter.setSelectedId(1, juce::sendNotificationSync);
        magdrum::ui::setLanguage(Language::english, false);
        return 0;
    }

    static int checksVirtualizedSequencerBar(MagDrumAudioProcessorEditor& editor,
                                              MagDrumAudioProcessor& processor)
    {
        if (editor.languageButton.getButtonText().isNotEmpty()) return 8;
        constexpr auto verticallyCentred = juce::Justification::verticallyCentred;
        const std::array<const juce::TextEditor*, 13> textFields{
            &editor.sampleSearch, &editor.velocityRange, &editor.tempoEditor, &editor.metadataBeat,
            &editor.metadataValue, &editor.profileName, &editor.mappingNote, &editor.pluginPresetKitName,
            &editor.pluginVelocities, &editor.pluginNoteDuration, &editor.pluginRelease,
            &editor.pluginCaptureLength, &editor.pluginRepeatCount};
        for (const auto* field : textFields)
            if (!field->getJustificationType().testFlags(verticallyCentred))
            {
                std::cerr << "A VST text field is not vertically centred.\n";
                return 13;
            }
        if (editor.steps.size() != 16 || editor.steps.front().size() != 32) return 1;
        constexpr std::array<const char*, 16> expectedLaneNames{
            "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Tom 4", "Ride", "Crash 1", "Clap",
            "Bell", "Crash 2", "Tom 1", "Tom 2", "Tom 3", "Side stick", "Pedal hi-hat", "Cowbell"};
        constexpr std::array<int, 16> expectedLaneNotes{
            36, 38, 42, 46, 43, 51, 49, 39, 53, 57, 50, 47, 45, 37, 44, 56};
        for (size_t lane = 0; lane < expectedLaneNames.size(); ++lane)
        {
            if (editor.laneLabels[lane].getText() != expectedLaneNames[lane])
            {
                std::cerr << "Sequencer lane " << lane << " label mismatch: expected " << expectedLaneNames[lane]
                          << ", got " << editor.laneLabels[lane].getText() << ".\n";
                return 2;
            }
            if (editor.mappedLaneNotes[lane] != expectedLaneNotes[lane])
            {
                std::cerr << "Sequencer lane " << lane << " MIDI note mismatch: expected " << expectedLaneNotes[lane]
                          << ", got " << editor.mappedLaneNotes[lane] << ".\n";
                return 9;
            }
        }
        if (magdrum::plugin::bossDr660PadLayout[15].instrument != DrumInstrument::Percussion
            || magdrum::plugin::bossDr660PadLayout[15].articulation != DrumArticulation::Bell
            || magdrum::plugin::padArticulations[15] != DrumArticulation::Bell)
        {
            std::cerr << "The Cowbell sequencer lane does not use the General MIDI Cowbell articulation.\n";
            return 10;
        }
        if (magdrum::plugin::bossDr660PadLayout[1].articulation != DrumArticulation::Edge
            || magdrum::plugin::bossDr660PadLayout[7].articulation != DrumArticulation::Default)
        {
            std::cerr << "Crash 2 and Tom 4 do not retain their distinct MIDI pad articulations.\n";
            return 11;
        }
        editor.pads[0].flash();
        if (editor.pads[0].getFlashAmount() < 0.9f) return 12;
        if (editor.getNumChildComponents() >= 800)
        {
            std::cerr << "Unexpectedly high sequencer component count: " << editor.getNumChildComponents() << "\n";
            return 7;
        }

        editor.lengthSelect.setSelectedId(2);
        editor.barSelect.setSelectedId(2);
        editor.setPage(2);
        if (editor.visibleStepIndex(0) != 16 || !editor.steps[0][0].isVisible()) return 3;

        editor.steps[0][0].setToggleState(true, juce::dontSendNotification);
        editor.buttonClicked(&editor.steps[0][0]);
        if (!editor.sequenceStepActive[0][16]) return 4;

        const auto state = processor.getPluginEditorState();
        if (state == nullptr) return 5;
        for (auto* child = state->getFirstChildElement(); child != nullptr; child = child->getNextElement())
            if (child->hasTagName("STEP") && child->getIntAttribute("lane", -1) == 0
                && child->getIntAttribute("step", -1) == 16)
            {
                editor.sequenceStepActive[8][0] = true;
                editor.sequenceStepActive[15][1] = true;
                editor.syncSequenceEventsFromGrid();
                const auto bell = std::find_if(editor.sequenceEvents.begin(), editor.sequenceEvents.end(),
                    [](const DrumEvent& event){ return event.midiNote == 53; });
                const auto cowbell = std::find_if(editor.sequenceEvents.begin(), editor.sequenceEvents.end(),
                    [](const DrumEvent& event){ return event.midiNote == 56; });
                if (bell == editor.sequenceEvents.end() || cowbell == editor.sequenceEvents.end()) return 10;
                if (editor.mappingProfile.toMidi(*bell, false).note != std::optional<std::uint8_t>(static_cast<std::uint8_t>(53))
                    || editor.mappingProfile.toMidi(*cowbell, false).note != std::optional<std::uint8_t>(static_cast<std::uint8_t>(56))) return 11;
                return 0;
            }
        return 6;
    }
};

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--magdrum-plugin-scan-worker") == 0)
        return PluginScanProcess::runWorker(juce::File(argv[2]));

    if (argc == 6 && std::strcmp(argv[1], "--magdrum-test-write-plugin-state") == 0)
    {
        auto fixture = std::make_unique<MagDrumAudioProcessor>();
        if (!fixture->loadSampleForNote(36, juce::File(argv[2]))
            || !fixture->loadSampleForNote(38, juce::File(argv[3]))
            || !fixture->loadSampleForNote(42, juce::File(argv[4]))) return 201;
        juce::XmlElement editorState("PLUGIN_EDITOR_STATE");
        editorState.setAttribute("eventModel", 1);
        editorState.setAttribute("meter", "4/4");
        editorState.setAttribute("bars", 1);
        const auto addEvent=[&editorState](DrumInstrument instrument,DrumArticulation articulation,
            int velocity,double beat)
        {
            auto* event=editorState.createNewChildElement("EVENT");
            event->setAttribute("instrument",static_cast<int>(instrument));
            event->setAttribute("articulation",static_cast<int>(articulation));
            event->setAttribute("velocity",velocity);
            event->setAttribute("beat",beat);
            event->setAttribute("duration",0.1);
        };
        addEvent(DrumInstrument::Kick,DrumArticulation::Default,100,0.001);
        addEvent(DrumInstrument::Snare,DrumArticulation::Default,90,0.501);
        addEvent(DrumInstrument::HiHat,DrumArticulation::Closed,80,1.001);
        fixture->setPluginEditorState(editorState);
        juce::MemoryBlock processorState;
        fixture->getStateInformation(processorState);
        if (processorState.isEmpty()) return 202;

        // A host's AudioPluginInstance state wraps processor state in a VST3 envelope.
        juce::XmlElement hostedState("VST3PluginState");
        hostedState.createNewChildElement("IComponent")->addTextElement(processorState.toBase64Encoding());
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary(hostedState, state);
        return !state.isEmpty() && juce::File(argv[5]).replaceWithData(state.getData(), state.getSize()) ? 0 : 203;
    }

#if JUCE_WINDOWS
    if (argc == 3 && std::strcmp(argv[1], "--magdrum-render-ui") == 0)
    {
        juce::ScopedJuceInitialiser_GUI guiInitialiser;
        const auto outputDirectory = juce::File(argv[2]);
        if (!outputDirectory.isDirectory() && !outputDirectory.createDirectory().wasOk()) return 204;
        auto processor = std::make_unique<MagDrumAudioProcessor>();
        const auto editorDatabase = outputDirectory.getChildFile("snapshot-index.sqlite3");
        auto editor = std::make_unique<MagDrumAudioProcessorEditor>(*processor, editorDatabase);
        return MagDrumAudioProcessorEditorTestAccess::renderPagesOffscreen(*editor, outputDirectory);
    }
#endif

    std::vector<std::unique_ptr<MagDrumAudioProcessor>> testProcessors;
    activeTestProcessors = &testProcessors;

    magdrum::plugin::DeferredNoteTriggers deferredTriggers;
    if (!deferredTriggers.push(36, 96) || !deferredTriggers.push(38, 110)
        || deferredTriggers.size() != 2) return 204;
    std::array<magdrum::plugin::DeferredNoteTrigger, 2> drainedTriggers{};
    size_t drainedCount = 0;
    deferredTriggers.drain([&](const auto& trigger)
    {
        drainedTriggers[drainedCount++] = trigger;
    });
    if (drainedCount != 2 || drainedTriggers[0].midiNote != 36 || drainedTriggers[0].velocity != 96
        || drainedTriggers[1].midiNote != 38 || drainedTriggers[1].velocity != 110
        || deferredTriggers.size() != 0) return 205;
    for (size_t index = 0; index < magdrum::plugin::DeferredNoteTriggers::capacity; ++index)
        if (!deferredTriggers.push(36, 100)) return 206;
    if (deferredTriggers.push(40, 100) || deferredTriggers.getOverflowCount() != 1) return 207;

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrumPluginSmokeTest");
    root.deleteRecursively();
    if (!root.createDirectory().wasOk()) return 1;

    using magdrum::ui::Language;
    struct LocalizationFixture
    {
        Language language;
        std::array<const char*, 12> expected;
    };
    const std::array<LocalizationFixture, 8> localizedTabs{{
        {Language::english, {"DRUM MACHINE", "Sample Library", "Duration shortest", "Name A-Z", "Off-grid hits in this step remain; use Edit hits to review them.", "While the host transport is playing, the sequencer follows its tempo and position.", "Sequencer skips {count} hits with no exact MIDI mapping. Review MIDI Maps.", "No MIDI mapping", "Load sample", "Sample index unavailable. Choose an audio file or scan a folder here.", "Hi-hat", "Side stick"}},
        {Language::german, {"Drum-Maschine", "Sample-Bibliothek", "Kürzeste zuerst", "Name A-Z", "Schläge außerhalb des Rasters in diesem Schritt bleiben erhalten; überprüfe sie unter „Schläge bearbeiten“.", "Während der Host-Transport läuft, folgt der Sequenzer dessen Tempo und Position.", "Der Sequenzer überspringt {count} Schläge ohne exakte MIDI-Zuordnung. Überprüfe MIDI-Zuordnungen.", "Keine MIDI-Zuordnung", "Sample laden", "Sampleindex nicht verfügbar. Lade eine Audiodatei oder scanne hier einen Ordner.", "Hi-Hat", "Seitenschlag"}},
        {Language::french, {"Boîte à rythmes", "Bibliothèque de sons", "Durée la plus courte", "Nom A-Z", "Les frappes hors grille de ce pas sont conservées ; vérifiez-les avec Modifier les frappes.", "Pendant la lecture du transport hôte, le séquenceur suit son tempo et sa position.", "Le séquenceur ignore {count} frappes sans correspondance MIDI exacte. Vérifiez les correspondances MIDI.", "Aucune correspondance MIDI", "Charger un son", "Index des sons indisponible. Choisissez un fichier audio ou analysez un dossier ici.", "Charleston", "Coup sur le bord"}},
        {Language::spanish, {"Caja de ritmos", "Biblioteca de sonidos", "Duración más corta", "Nombre A-Z", "Los golpes fuera de la cuadrícula de este paso se conservan; revísalos en Editar golpes.", "Mientras el transporte del anfitrión está en marcha, el secuenciador sigue su tempo y posición.", "El secuenciador omite {count} golpes sin una asignación MIDI exacta. Revisa los mapas MIDI.", "Sin asignación MIDI", "Cargar muestra", "Índice de muestras no disponible. Elige un archivo de audio o analiza una carpeta aquí.", "Hi-hat", "Golpe lateral"}},
        {Language::swedish, {"Trummaskin", "Samplebibliotek", "Kortast först", "Namn A-Ö", "Slag utanför rutnätet i det här steget finns kvar; granska dem med Redigera slag.", "När värdens transport spelar följer sequencern dess tempo och position.", "Sequencern hoppar över {count} slag utan exakt MIDI-mappning. Granska MIDI-mappningarna.", "Ingen MIDI-mappning", "Ladda sample", "Sampleindexet är inte tillgängligt. Välj en ljudfil eller skanna en mapp här.", "Hi-hat", "Kantslag"}},
        {Language::norwegian, {"Trommemaskin", "Lydbibliotek", "Korteste først", "Navn A-Å", "Slag utenfor rutenettet i dette trinnet blir beholdt; se dem under Rediger slag.", "Når vertens transport spiller, følger sequenceren tempoet og posisjonen.", "Sequenceren hopper over {count} slag uten nøyaktig MIDI-tilordning. Se gjennom MIDI-kartene.", "Ingen MIDI-tilordning", "Last inn sample", "Sampleindeksen er ikke tilgjengelig. Velg en lydfil eller skann en mappe her.", "Hi-hat", "Kantslag"}},
        {Language::danish, {"Trommemaskine", "Samplebibliotek", "Korteste først", "Navn A-Å", "Slag uden for gitteret i dette trin bevares; gennemgå dem med Rediger slag.", "Når værtens transport afspiller, følger sequenceren dens tempo og position.", "Sequenceren springer {count} slag over uden en præcis MIDI-tilknytning. Gennemgå MIDI-kortene.", "Ingen MIDI-tilknytning", "Indlæs sample", "Sampleindekset er ikke tilgængeligt. Vælg en lydfil eller scan en mappe her.", "Hi-hat", "Kantslag"}},
        {Language::finnish, {"Rumpukone", "Samplekirjasto", "Lyhimmät ensin", "Nimi A-Ö", "Tämän askeleen ruudukon ulkopuoliset iskut säilyvät; tarkista ne kohdasta Muokkaa iskuja.", "Kun isännän kuljetus toistaa, sekvensseri seuraa sen tempoa ja sijaintia.", "Sekvensseri ohittaa {count} iskua, joilla ei ole tarkkaa MIDI-määritystä. Tarkista MIDI-kartat.", "Ei MIDI-määritystä", "Lataa näyte", "Näyteindeksi ei ole käytettävissä. Valitse äänitiedosto tai skannaa kansio tässä.", "Hi-hat", "Sivulyönti"}}
    }};
    for (const auto& [language, expected] : localizedTabs)
    {
        magdrum::ui::setLanguage(language, false);
        const std::array<juce::String, 12> actual{
            magdrum::ui::localisedText("DRUM MACHINE"),
            magdrum::ui::localisedText("Sample Library"),
            magdrum::ui::localisedText("Duration shortest"),
            magdrum::ui::localisedText("Name A-Z"),
            magdrum::ui::localisedText("Off-grid hits in this step remain; use Edit hits to review them."),
            magdrum::ui::localisedText("While the host transport is playing, the sequencer follows its tempo and position."),
            magdrum::ui::localisedText("Sequencer skips {count} hits with no exact MIDI mapping. Review MIDI Maps."),
            magdrum::ui::localisedText("No MIDI mapping"),
            magdrum::ui::localisedText("Load sample"),
            magdrum::ui::localisedText("Sample index unavailable. Choose an audio file or scan a folder here."),
            magdrum::ui::localisedText("Hi-hat"),
            magdrum::ui::localisedText("Side stick")
        };
        for (size_t item = 0; item < actual.size(); ++item)
        {
            if (actual[item] != expected[item])
            {
                std::cerr << "Localization mismatch for " << magdrum::ui::languageName(language)
                          << " item " << item << ": expected '" << expected[item]
                          << "', got '" << actual[item] << "'.\n";
                return 31;
            }
        }
    }
    constexpr const char* containerWarningKey = "Detected %1 Steinberg VST Sound containers and %2 Kontakt NCW files. Use Plugin Scanner to capture sounds from installed instruments.";
    constexpr const char* containerWarningTitle = "Some installed library sounds need Plugin Scanner";
    const std::array<Language, 8> allLanguages{
        Language::english, Language::german, Language::french, Language::spanish,
        Language::swedish, Language::norwegian, Language::danish, Language::finnish};
    for (const auto language : allLanguages)
    {
        magdrum::ui::setLanguage(language, false);
        const auto warning = magdrum::ui::localisedText(containerWarningKey);
        const auto title = magdrum::ui::localisedText(containerWarningTitle);
        if (!warning.contains("%1") || !warning.contains("%2") || title.isEmpty()
            || (language != Language::english && (warning == containerWarningKey || title == containerWarningTitle)))
            return 32;
    }
    magdrum::ui::setLanguage(Language::english, false);

    const std::array<std::pair<Language, const char*>, 7> pluginFolderLabels{{
        {Language::german, "VST3-Ordner wählen"},
        {Language::french, "Choisir un dossier VST3"},
        {Language::spanish, "Elegir carpeta VST3"},
        {Language::swedish, "Välj VST3-mapp"},
        {Language::norwegian, "Velg VST3-mappe"},
        {Language::danish, "Vælg VST3-mappe"},
        {Language::finnish, "Valitse VST3-kansio"}
    }};
    for (const auto& [language, expected] : pluginFolderLabels)
    {
        magdrum::ui::setLanguage(language, false);
        if (magdrum::ui::localisedText("Choose VST3 folder") != expected)
            return 38;
    }
    magdrum::ui::setLanguage(Language::english, false);

    const auto sample = root.getChildFile("kick.wav");
    if (!writeFixture(sample)) return 2;
    const auto resonantFilterSample = root.getChildFile("filter-resonance.wav");
    if (!writeFixture(resonantFilterSample, 0.15f, 48000, 4000.0)) return 2;
    const auto trimSample = root.getChildFile("trim-impulses.wav");
    if (!writeTrimImpulseFixture(trimSample)) return 2;
    const auto comparisonSample = root.getChildFile("comparison.wav");
    if (!writeFixture(comparisonSample, 0.18f)) return 3;

#if JUCE_WINDOWS
    {
        juce::ScopedJuceInitialiser_GUI guiInitialiser;
        MagDrumAudioProcessor editorProcessor;
        const auto editorDatabase = root.getChildFile("plugin-editor-db").getChildFile("samples.sqlite3");
        MagDrumAudioProcessorEditor editor(editorProcessor, editorDatabase);
        if (!MagDrumAudioProcessorEditorTestAccess::mappingNotePlaceholderMatchesLanguage(editor))
        {
            std::cerr << "The MIDI note placeholder does not match the selected language key.\n";
            return 77;
        }
        if (editor.getWidth() < 1000 || editor.getHeight() < 620) return 41;
        const int independentPadCheck = MagDrumAudioProcessorEditorTestAccess::checksIndependentPadSoundControls(editor, editorProcessor);
        if (independentPadCheck != 0) return 65 + independentPadCheck;
        const int automationGestureCheck = MagDrumAudioProcessorEditorTestAccess::checksPadAutomationGesture(editor);
        if (automationGestureCheck != 0) return 330 + automationGestureCheck;
        const int folderButtonCheck = MagDrumAudioProcessorEditorTestAccess::checksPluginFolderChooserVisibility(editor);
        if (folderButtonCheck != 0) return 44;
        const int sampleScanProgressCheck = MagDrumAudioProcessorEditorTestAccess::checksSampleScanProgress(editor);
        if (sampleScanProgressCheck != 0) return 48 + sampleScanProgressCheck;
        const int drumLayoutCheck = MagDrumAudioProcessorEditorTestAccess::checksDrumMachineControlLayout(editor);
        if (drumLayoutCheck != 0) return 60 + drumLayoutCheck;
        const int editorCheck=MagDrumAudioProcessorEditorTestAccess::checksVirtualizedSequencerBar(editor, editorProcessor);
        if(editorCheck!=0){std::cerr<<"Virtualized sequencer editor smoke stage "<<editorCheck<<" failed.\n";return 41;}
        const int languageCheck=MagDrumAudioProcessorEditorTestAccess::checksLiveLanguageSwitching(editor, sample, comparisonSample);
        if(languageCheck!=0)return 42;
        const int midiSearchCancellationCheck=MagDrumAudioProcessorEditorTestAccess::checksDefaultMidiSearchCancellation(editor);
        if(midiSearchCancellationCheck!=0)return 43;
        const int smpteImportCheck = MagDrumAudioProcessorEditorTestAccess::checksSmpteMidiImport(editor, editorProcessor);
        if (smpteImportCheck != 0) return 288 + smpteImportCheck;
        const int padActivityCheck = MagDrumAudioProcessorEditorTestAccess::checksTriggeredMidiPadGlow(editor, editorProcessor);
        if (padActivityCheck != 0) return 59 + padActivityCheck;
        const int dr660LaneCheck = MagDrumAudioProcessorEditorTestAccess::checksDr660ImportedNotesReachTheirOwnSequencerLanes(editor);
        if (dr660LaneCheck != 0) return 300 + dr660LaneCheck;
        const int sampleMapPagingCheck = MagDrumAudioProcessorEditorTestAccess::checksSampleMapCoversBeyondFirstListPage(editor, sample);
        if (sampleMapPagingCheck != 0) return 320 + sampleMapPagingCheck;
        const int midiFallbackPersistenceCheck = MagDrumAudioProcessorEditorTestAccess::checksMidiFallbackPolicyPersists(editor);
        if (midiFallbackPersistenceCheck != 0) return 325 + midiFallbackPersistenceCheck;
    }
#endif

    auto previewProcessor = std::make_unique<MagDrumAudioProcessor>();
    if (previewProcessor->previewSample(sample, [] { return true; })) return 250;
    previewProcessor->prepareToPlay(44100.0, 128);
    if (!previewProcessor->previewSample(sample)) return 251;
    juce::AudioBuffer<float> previewAudio(2, 128);
    juce::MidiBuffer previewMidi;
    previewProcessor->processBlock(previewAudio, previewMidi);
    double previewPeak = 0.0;
    for (int channel = 0; channel < previewAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < previewAudio.getNumSamples(); ++sampleIndex)
            previewPeak = juce::jmax(previewPeak,
                std::abs(static_cast<double>(previewAudio.getSample(channel, sampleIndex))));
    if (previewPeak <= 0.05) return 252;
    const auto quieterPreview = root.getChildFile("quieter-preview.wav");
    if (!writeFixture(quieterPreview, 0.05f) || !previewProcessor->previewSample(quieterPreview)) return 253;
    previewAudio.clear();
    previewProcessor->processBlock(previewAudio, previewMidi);
    double quieterPreviewPeak = 0.0;
    for (int channel = 0; channel < previewAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < previewAudio.getNumSamples(); ++sampleIndex)
            quieterPreviewPeak = juce::jmax(quieterPreviewPeak,
                std::abs(static_cast<double>(previewAudio.getSample(channel, sampleIndex))));
    if (quieterPreviewPeak <= 0.005 || quieterPreviewPeak >= previewPeak * 0.5) return 254;
    previewProcessor->stopSamplePreview();
    previewAudio.clear();
    previewProcessor->processBlock(previewAudio, previewMidi);
    double stoppedPreviewPeak = 0.0;
    for (int channel = 0; channel < previewAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < previewAudio.getNumSamples(); ++sampleIndex)
            stoppedPreviewPeak = juce::jmax(stoppedPreviewPeak,
                std::abs(static_cast<double>(previewAudio.getSample(channel, sampleIndex))));
    if (stoppedPreviewPeak > 0.00001) return 255;

    auto& contendedSampleUpdate = makeTestProcessor();
    if (!contendedSampleUpdate.loadSampleForNote(36, sample)) return 208;
    contendedSampleUpdate.prepareToPlay(48000.0, 128);
    auto& sampleLock = MagDrumAudioProcessorTestAccess::sampleLock(contendedSampleUpdate);
    std::atomic<bool> sampleLockHeld{false}, releaseSampleLock{false};
    std::thread sampleUpdate([&]
    {
        sampleLock.enter();
        sampleLockHeld.store(true, std::memory_order_release);
        while (!releaseSampleLock.load(std::memory_order_acquire))
            std::this_thread::yield();
        sampleLock.exit();
    });
    while (!sampleLockHeld.load(std::memory_order_acquire))
        std::this_thread::yield();

    juce::AudioBuffer<float> contendedAudio(2, 128);
    juce::MidiBuffer contendedMidi;
    contendedMidi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0);
    contendedSampleUpdate.processBlock(contendedAudio, contendedMidi);
    double contendedPeak = 0.0;
    for (int channel = 0; channel < contendedAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < contendedAudio.getNumSamples(); ++sampleIndex)
            contendedPeak = juce::jmax(contendedPeak,
                std::abs(static_cast<double>(contendedAudio.getSample(channel, sampleIndex))));
    releaseSampleLock.store(true, std::memory_order_release);
    sampleUpdate.join();
    if (!contendedMidi.isEmpty() || contendedPeak != 0.0) return 209;

    juce::AudioBuffer<float> deferredAudio(2, 128);
    juce::MidiBuffer noDeferredMidi;
    contendedSampleUpdate.processBlock(deferredAudio, noDeferredMidi);
    double deferredPeak = 0.0;
    for (int channel = 0; channel < deferredAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < deferredAudio.getNumSamples(); ++sampleIndex)
            deferredPeak = juce::jmax(deferredPeak,
                std::abs(static_cast<double>(deferredAudio.getSample(channel, sampleIndex))));
    if (deferredPeak < 0.05) return 210;

    juce::AudioFormatManager fixtureFormats;
    fixtureFormats.registerBasicFormats();
    auto* aiffFormat = fixtureFormats.findFormatForFileExtension("aiff");
    auto* flacFormat = fixtureFormats.findFormatForFileExtension("flac");
    if (aiffFormat == nullptr || flacFormat == nullptr) return 32;
    const auto writeFormatFixture = [](juce::AudioFormat& format, const juce::File& file)
    {
        constexpr double sampleRate = 48000.0;
        juce::AudioBuffer<float> audio(1, 960);
        for (int sampleIndex = 0; sampleIndex < audio.getNumSamples(); ++sampleIndex)
            audio.setSample(0, sampleIndex, static_cast<float>(0.25 * std::sin(
                2.0 * juce::MathConstants<double>::pi * 220.0 * sampleIndex / sampleRate)));
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (!stream) return false;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions()
            .withSampleRate(sampleRate).withNumChannels(1).withBitsPerSample(24));
        return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
    };
    const auto aiffSample = root.getChildFile("kick.aiff");
    const auto flacSample = root.getChildFile("kick.flac");
    if (!writeFormatFixture(*aiffFormat, aiffSample) || !writeFormatFixture(*flacFormat, flacSample)) return 33;
    for (const auto& encodedSample : {aiffSample, flacSample})
    {
        auto& encodedProcessor = makeTestProcessor();
        if (!encodedProcessor.loadSampleForNote(36, encodedSample)) return 34;
        encodedProcessor.prepareToPlay(48000.0, 128);
        if (renderPeak(encodedProcessor, 36, 100) < 0.05) return 35;
    }

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

    auto& processor = makeTestProcessor();
    if (processor.getNumPrograms() != 1 || processor.getCurrentProgram() != 0
        || processor.getProgramName(0) != "Default Kit") return 254;
    auto& automatedPadProcessor = makeTestProcessor();
    if (automatedPadProcessor.getParameters().size() != 16 * 25)
    {
        std::cerr << "Expected 400 host-automatable pad parameters, found "
                  << automatedPadProcessor.getParameters().size() << ".\n";
        return 257;
    }
    auto* kickGainParameter = findHostFloatParameter(automatedPadProcessor, "magdrum_pad_36_gain");
    if (kickGainParameter == nullptr)
    {
        std::cerr << "The C1 Kick Gain parameter was not registered with the host.\n";
        return 258;
    }
    auto* kickChokeParameter = findHostFloatParameter(automatedPadProcessor, "magdrum_pad_36_choke_group");
    if (kickChokeParameter == nullptr) return 292;
    kickChokeParameter->setValueNotifyingHost(kickChokeParameter->range.convertTo0to1(3.0f));
    if (automatedPadProcessor.getNoteParameters(36).chokeGroup != 3
        || automatedPadProcessor.getNoteParameters(38).chokeGroup != 0) return 293;
    kickGainParameter->setValueNotifyingHost(kickGainParameter->range.convertTo0to1(-12.0f));
    if (std::abs(automatedPadProcessor.getNoteParameters(36).gainDb + 12.0f) > 0.001f)
    {
        std::cerr << "Host automation did not update the C1 Kick's audio parameter.\n";
        return 259;
    }
    auto snareParameters = automatedPadProcessor.getNoteParameters(38);
    snareParameters.gainDb = -8.0f;
    automatedPadProcessor.setNoteParameters(38, snareParameters);
    auto* snareGainParameter = findHostFloatParameter(automatedPadProcessor, "magdrum_pad_38_gain");
    if (snareGainParameter == nullptr || std::abs(snareGainParameter->get() + 8.0f) > 0.001f
        || std::abs(automatedPadProcessor.getNoteParameters(36).gainDb + 12.0f) > 0.001f)
    {
        std::cerr << "The editor-to-host parameter update or per-pad isolation failed.\n";
        return 260;
    }
    if (!processor.loadSampleForNote(36, sample) || processor.getSamplePathForNote(36) != sample.getFullPathName()) return 8;
    auto& chokeProcessor = makeTestProcessor();
    if (!chokeProcessor.loadSampleForNote(36, sample)
        || !chokeProcessor.loadSampleForNote(38, sample)
        || !chokeProcessor.loadSampleForNote(40, sample)) return 294;
    chokeProcessor.prepareToPlay(48000.0, 128);
    auto kickChoke = chokeProcessor.getNoteParameters(36);
    kickChoke.chokeGroup = 2;
    chokeProcessor.setNoteParameters(36, kickChoke, false);
    auto snareChoke = chokeProcessor.getNoteParameters(38);
    snareChoke.chokeGroup = 2;
    chokeProcessor.setNoteParameters(38, snareChoke, false);
    juce::AudioBuffer<float> chokeAudio(2, 128);
    juce::MidiBuffer chokeMidi;
    chokeMidi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0);
    chokeProcessor.processBlock(chokeAudio, chokeMidi);
    if (MagDrumAudioProcessorTestAccess::activeVoicesForChokeGroup(chokeProcessor, 2) != 1) return 295;
    chokeMidi.addEvent(juce::MidiMessage::noteOn(10, 38, static_cast<juce::uint8>(100)), 0);
    chokeProcessor.processBlock(chokeAudio, chokeMidi);
    if (MagDrumAudioProcessorTestAccess::activeVoicesForChokeGroup(chokeProcessor, 2) != 1
        || !MagDrumAudioProcessorTestAccess::activeVoiceMatches(chokeProcessor, 2, 38)) return 296;
    auto ungrouped = chokeProcessor.getNoteParameters(40);
    chokeProcessor.setNoteParameters(40, ungrouped, false);
    chokeMidi.addEvent(juce::MidiMessage::noteOn(10, 40, static_cast<juce::uint8>(100)), 0);
    chokeProcessor.processBlock(chokeAudio, chokeMidi);
    if (MagDrumAudioProcessorTestAccess::activeVoicesForChokeGroup(chokeProcessor, 2) != 1) return 297;
    auto& automationReference = makeTestProcessor();
    if (!automationReference.loadSampleForNote(36, sample)
        || !automatedPadProcessor.loadSampleForNote(36, sample)) return 262;
    automationReference.prepareToPlay(48000.0, 128);
    automatedPadProcessor.prepareToPlay(48000.0, 128);
    const auto referenceAutomationPeak = renderPeak(automationReference, 36, 127);
    const auto automatedKickPeak = renderPeak(automatedPadProcessor, 36, 127);
    if (referenceAutomationPeak <= 0.0
        || std::abs(automatedKickPeak / referenceAutomationPeak - std::pow(10.0, -12.0 / 20.0)) > 0.002)
    {
        std::cerr << "Host-automated Kick gain did not produce the expected audio level.\n";
        return 263;
    }
    ParameterGestureProbe dragGesture;
    kickGainParameter->addListener(&dragGesture);
    automatedPadProcessor.beginNoteParameterGesture(36, 0);
    auto draggedKickParameters = automatedPadProcessor.getNoteParameters(36);
    draggedKickParameters.gainDb = -15.0f;
    automatedPadProcessor.setNoteParameters(36, draggedKickParameters);
    if (dragGesture.starts != 1 || dragGesture.ends != 0)
    {
        std::cerr << "A pad-control drag must keep its host gesture open while values change.\n";
        return 266;
    }
    automatedPadProcessor.endNoteParameterGesture(36, 0);
    kickGainParameter->removeListener(&dragGesture);
    if (dragGesture.starts != 1 || dragGesture.ends != 1)
    {
        std::cerr << "A pad-control drag did not close its host gesture.\n";
        return 267;
    }
    auto* panParameter = findHostFloatParameter(automatedPadProcessor, "magdrum_pad_36_pan");
    if (panParameter == nullptr) return 268;
    ParameterGestureProbe singleEditGesture;
    panParameter->addListener(&singleEditGesture);
    draggedKickParameters = automatedPadProcessor.getNoteParameters(36);
    draggedKickParameters.pan = 0.2f;
    automatedPadProcessor.setNoteParameters(36, draggedKickParameters);
    panParameter->removeListener(&singleEditGesture);
    if (singleEditGesture.starts != 1 || singleEditGesture.ends != 1)
    {
        std::cerr << "A one-shot host parameter edit must be bracketed by a complete gesture.\n";
        return 269;
    }
    struct HostAutomationProbe
    {
        const char* parameterSuffix;
        float value;
        float MagDrumAudioProcessor::NoteParameters::* field;
    };
    constexpr std::array<HostAutomationProbe, 24> hostAutomationProbes{{
        {"gain", -6.0f, &MagDrumAudioProcessor::NoteParameters::gainDb},
        {"pan", -0.35f, &MagDrumAudioProcessor::NoteParameters::pan},
        {"tune", 7.0f, &MagDrumAudioProcessor::NoteParameters::tuneSemitones},
        {"attack", 40.0f, &MagDrumAudioProcessor::NoteParameters::attackMs},
        {"decay", 320.0f, &MagDrumAudioProcessor::NoteParameters::decayMs},
        {"sustain", 0.55f, &MagDrumAudioProcessor::NoteParameters::sustainLevel},
        {"release", 480.0f, &MagDrumAudioProcessor::NoteParameters::releaseMs},
        {"filter_cutoff", 4321.0f, &MagDrumAudioProcessor::NoteParameters::filterCutoffHz},
        {"filter_resonance", 3.4f, &MagDrumAudioProcessor::NoteParameters::filterResonance},
        {"drive", 4.5f, &MagDrumAudioProcessor::NoteParameters::driveDb},
        {"eq_low", -3.2f, &MagDrumAudioProcessor::NoteParameters::eqLowDb},
        {"eq_mid", 2.3f, &MagDrumAudioProcessor::NoteParameters::eqMidDb},
        {"eq_high", -1.4f, &MagDrumAudioProcessor::NoteParameters::eqHighDb},
        {"sample_start", 0.2f, &MagDrumAudioProcessor::NoteParameters::sampleStart},
        {"sample_end", 0.85f, &MagDrumAudioProcessor::NoteParameters::sampleEnd},
        {"compressor_threshold", -18.0f, &MagDrumAudioProcessor::NoteParameters::compressorThresholdDb},
        {"compressor_ratio", 5.5f, &MagDrumAudioProcessor::NoteParameters::compressorRatio},
        {"compressor_attack", 25.0f, &MagDrumAudioProcessor::NoteParameters::compressorAttackMs},
        {"compressor_release", 140.0f, &MagDrumAudioProcessor::NoteParameters::compressorReleaseMs},
        {"compressor_makeup", 3.5f, &MagDrumAudioProcessor::NoteParameters::compressorMakeupDb},
        {"phaser_rate", 1.75f, &MagDrumAudioProcessor::NoteParameters::phaserRateHz},
        {"phaser_depth", 0.65f, &MagDrumAudioProcessor::NoteParameters::phaserDepth},
        {"phaser_feedback", -0.25f, &MagDrumAudioProcessor::NoteParameters::phaserFeedback},
        {"phaser_mix", 0.45f, &MagDrumAudioProcessor::NoteParameters::phaserMix}
    }};
    for (const auto& probe : hostAutomationProbes)
    {
        const auto parameterId = juce::String("magdrum_pad_36_") + probe.parameterSuffix;
        auto* parameter = findHostFloatParameter(automatedPadProcessor, parameterId);
        if (parameter == nullptr)
        {
            std::cerr << "Missing host automation parameter: " << parameterId << ".\n";
            return 264;
        }
        parameter->setValueNotifyingHost(parameter->range.convertTo0to1(probe.value));
    }
    const auto automatedKickParameters = automatedPadProcessor.getNoteParameters(36);
    for (const auto& probe : hostAutomationProbes)
        if (std::abs(automatedKickParameters.*(probe.field) - probe.value) > 0.002f)
        {
            std::cerr << "Host automation parameter " << probe.parameterSuffix
                      << " did not reach the matching Kick sound control.\n";
            return 265;
        }
    const auto sampleAssignments = processor.getAllSampleAssignments();
    if (sampleAssignments.size() != 1 || sampleAssignments.front().midiNote != 36
        || sampleAssignments.front().minimumVelocity != 1 || sampleAssignments.front().maximumVelocity != 127
        || sampleAssignments.front().path != sample.getFullPathName()) return 35;
    const auto generalMidi = MidiMappingProfile::generalMidi();
    const std::vector<DrumEvent> unmappedSequence{{DrumInstrument::Percussion,DrumArticulation::Edge,100,0.0,0.1}};
    if (processor.setSequencerPattern(unmappedSequence,generalMidi,4.0)!=1
        || processor.getSequencerUnmappedEventCount()!=1) return 34;
    juce::XmlElement sequencerState("PLUGIN_EDITOR_STATE");
    sequencerState.setAttribute("eventModel",1);
    sequencerState.setAttribute("profileCsv",juce::String::fromUTF8(generalMidi.toCsv().c_str()));
    sequencerState.setAttribute("tempo","137");
    sequencerState.setAttribute("meter","4/4");
    sequencerState.setAttribute("bars",2);
    sequencerState.setAttribute("gridResolution",8);
    auto* sequencerEvent=sequencerState.createNewChildElement("EVENT");
    sequencerEvent->setAttribute("instrument",static_cast<int>(DrumInstrument::Kick));
    sequencerEvent->setAttribute("articulation",static_cast<int>(DrumArticulation::Default));
    sequencerEvent->setAttribute("velocity",100);
    sequencerEvent->setAttribute("beat",0.0);
    sequencerEvent->setAttribute("duration",0.1);
    sequencerEvent->setAttribute("midiNote",35);
    auto* laterKickEvent=sequencerState.createNewChildElement("EVENT");
    laterKickEvent->setAttribute("instrument",static_cast<int>(DrumInstrument::Kick));
    laterKickEvent->setAttribute("articulation",static_cast<int>(DrumArticulation::Default));
    laterKickEvent->setAttribute("velocity",88);
    laterKickEvent->setAttribute("beat",4.5);
    laterKickEvent->setAttribute("duration",0.1);
    auto* tempoMetadata=sequencerState.createNewChildElement("META");
    tempoMetadata->setAttribute("kind","tempo");
    tempoMetadata->setAttribute("beat",1.5);
    tempoMetadata->setAttribute("microsecondsPerQuarterNote",500000);
    auto* meterMetadata=sequencerState.createNewChildElement("META");
    meterMetadata->setAttribute("kind","meter");
    meterMetadata->setAttribute("beat",4.0);
    meterMetadata->setAttribute("numerator",3);
    meterMetadata->setAttribute("denominator",4);
    processor.setPluginEditorState(sequencerState);
    if (processor.getSequencerUnmappedEventCount()!=0) return 35;
    auto* missingMappedEvent=sequencerState.createNewChildElement("EVENT");
    missingMappedEvent->setAttribute("instrument",static_cast<int>(DrumInstrument::Percussion));
    missingMappedEvent->setAttribute("articulation",static_cast<int>(DrumArticulation::Edge));
    missingMappedEvent->setAttribute("velocity",100);
    missingMappedEvent->setAttribute("beat",0.5);
    missingMappedEvent->setAttribute("duration",0.1);
    processor.setPluginEditorState(sequencerState);
    if (processor.getSequencerUnmappedEventCount()!=1) return 36;
    if (processor.loadSampleForVelocityRange(37, 1, 127, sample, [] { return true; })
        || !processor.getSamplePathForNote(37).isEmpty()) return 253;
    if (processor.loadSampleForNote(-1, sample) || processor.loadSampleForNote(128, sample)
        || processor.loadSampleForNote(37, root.getChildFile("missing.wav"))) return 9;
    if (processor.getSamplePathForNote(37).isNotEmpty()) return 10;

    processor.changeProgramName(0, "  Studio Kit  ");
    if (processor.getProgramName(0) != "Studio Kit") return 255;
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    if (state.isEmpty()) return 11;

    auto& restored = makeTestProcessor();
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (restored.getProgramName(0) != "Studio Kit") return 256;
    juce::MemoryBlock stateSavedWhileRestoring;
    restored.getStateInformation(stateSavedWhileRestoring);
    auto& restoredAgain = makeTestProcessor();
    restoredAgain.setStateInformation(stateSavedWhileRestoring.getData(),
                                      static_cast<int>(stateSavedWhileRestoring.getSize()));
    if (!waitForSamplePath(restored, 36, sample.getFullPathName())
        || !waitForSamplePath(restoredAgain, 36, sample.getFullPathName())) return 12;
    const auto restoredEditorState=restored.getPluginEditorState();
    if(!restoredEditorState||restoredEditorState->getIntAttribute("gridResolution",4)!=8
        ||restoredEditorState->getStringAttribute("tempo")!="137"
        ||restored.getSequencerUnmappedEventCount()!=1)return 37;
    int restoredEventCount=0,restoredMetadataCount=0;
    for(auto* child=restoredEditorState->getFirstChildElement();child!=nullptr;child=child->getNextElement())
    {
        if(child->hasTagName("EVENT"))++restoredEventCount;
        else if(child->hasTagName("META"))++restoredMetadataCount;
    }
    if(restoredEditorState->getIntAttribute("bars",0)!=2||restoredEventCount!=3||restoredMetadataCount!=2)
    {
        std::cerr << "Sequencer state restore mismatch: bars=" << restoredEditorState->getIntAttribute("bars",0)
                  << ", events=" << restoredEventCount << ", metadata=" << restoredMetadataCount << ".\n";
        return 40;
    }
    bool restoredRawPitch=false,olderEventStillHasDefaultPitch=false;
    for(auto* child=restoredEditorState->getFirstChildElement();child!=nullptr;child=child->getNextElement())
        if(child->hasTagName("EVENT"))
        {
            const int beat=juce::roundToInt(child->getDoubleAttribute("beat",-1.0)*2.0);
            if(beat==0)restoredRawPitch=child->getIntAttribute("midiNote",-1)==35;
            if(beat==9)olderEventStillHasDefaultPitch=child->getIntAttribute("midiNote",-1)==-1;
        }
    if(!restoredRawPitch||!olderEventStillHasDefaultPitch)
    {
        std::cerr << "Sequencer state did not preserve a raw MIDI pitch or legacy default.\n";
        return 289;
    }
    restored.prepareToPlay(44100.0, 128);
    const double fullVelocityPeak = renderPeak(restored, 36, 127);
    auto& lowVelocity = makeTestProcessor();
    lowVelocity.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    if (!waitForSamplePath(lowVelocity, 36, sample.getFullPathName())) return 13;
    lowVelocity.prepareToPlay(44100.0, 128);
    const double lowVelocityPeak = renderPeak(lowVelocity, 36, 64);
    if (fullVelocityPeak <= 0.05 || fullVelocityPeak > 0.26 || lowVelocityPeak <= 0.0
        || lowVelocityPeak >= fullVelocityPeak || std::abs(lowVelocityPeak / fullVelocityPeak - 64.0 / 127.0) > 0.03)
    {
        std::cerr << "Velocity render check failed: full=" << fullVelocityPeak << ", low=" << lowVelocityPeak
                  << ", ratio=" << (fullVelocityPeak > 0.0 ? lowVelocityPeak / fullVelocityPeak : 0.0) << '\n';
        return 13;
    }

    auto& queuedTrigger = makeTestProcessor();
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
    if (queuedTrigger.consumeNoteTriggerCount(36) != 1
        || queuedTrigger.consumeNoteTriggerCount(36) != 0)
    {
        std::cerr << "Queued UI trigger activity was not delivered exactly once to the editor.\n";
        return 57;
    }

    auto& midiActivity = makeTestProcessor();
    midiActivity.prepareToPlay(44100.0, 128);
    juce::AudioBuffer<float> midiActivityAudio(2, 128);
    juce::MidiBuffer midiActivityEvents;
    midiActivityEvents.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0);
    midiActivity.processBlock(midiActivityAudio, midiActivityEvents);
    if (midiActivity.consumeNoteTriggerCount(36) != 1
        || midiActivity.consumeNoteTriggerCount(36) != 0)
    {
        std::cerr << "Incoming host MIDI activity was not delivered exactly once to the editor.\n";
        return 58;
    }

    auto& hostSyncedSequencer = makeTestProcessor();
    if (!hostSyncedSequencer.loadSampleForNote(36, sample)) return 37;
    hostSyncedSequencer.prepareToPlay(48000.0, 128);
    const std::vector<DrumEvent> hostEvents{{DrumInstrument::Kick, DrumArticulation::Default, 100, 0.001, 0.1}};
    if (hostSyncedSequencer.setSequencerPattern(hostEvents, generalMidi, 4.0) != 0) return 37;
    TestAudioPlayHead hostPlayHead;
    hostPlayHead.position.setIsPlaying(true);
    hostPlayHead.position.setPpqPosition(0.0);
    hostPlayHead.position.setBpm(120.0);
    hostSyncedSequencer.setPlayHead(&hostPlayHead);
    hostSyncedSequencer.setSequencerPlaybackEnabled(true);
    juce::AudioBuffer<float> hostAudio(2, 128);
    juce::MidiBuffer hostMidi;
    hostSyncedSequencer.processBlock(hostAudio, hostMidi);
    if (hostSyncedSequencer.consumeNoteTriggerCount(36) != 1)
    {
        std::cerr << "Host-synced sequencer activity was not delivered to the editor.\n";
        return 59;
    }
    double preHitPeak = 0.0;
    double postHitPeak = 0.0;
    for (int channel = 0; channel < hostAudio.getNumChannels(); ++channel)
        for (int sampleIndex = 0; sampleIndex < hostAudio.getNumSamples(); ++sampleIndex)
        {
            const double magnitude = std::abs(static_cast<double>(hostAudio.getSample(channel, sampleIndex)));
            if (sampleIndex < 24)
                preHitPeak = juce::jmax(preHitPeak, magnitude);
            else
                postHitPeak = juce::jmax(postHitPeak, magnitude);
        }
    if (!hostSyncedSequencer.isSequencerHostSynchronized() || preHitPeak > 1.0e-7 || postHitPeak <= 0.05)
    {
        std::cerr << "Host-synced sequencer render check failed: synchronized="
                  << hostSyncedSequencer.isSequencerHostSynchronized()
                  << ", pre-hit peak=" << preHitPeak << ", post-hit peak=" << postHitPeak << '\n';
        return 37;
    }

    realtimeAllocationCount = 0;
    countRealtimeAllocations = true;
    hostSyncedSequencer.processBlock(hostAudio, hostMidi);
    countRealtimeAllocations = false;
    const auto audioThreadAllocations = realtimeAllocationCount;
    if (audioThreadAllocations != 0)
    {
        std::cerr << "Audio callback allocated " << audioThreadAllocations
                  << " time(s) while host-synced sequencer playback was active.\n";
        return 39;
    }

    auto& hostTempoChangeSequencer = makeTestProcessor();
    if (!hostTempoChangeSequencer.loadSampleForNote(36, sample)) return 61;
    hostTempoChangeSequencer.prepareToPlay(48000.0, 128);
    const std::vector<DrumEvent> tempoChangeEvents{{DrumInstrument::Kick, DrumArticulation::Default, 100, 0.009, 0.1}};
    if (hostTempoChangeSequencer.setSequencerPattern(tempoChangeEvents, generalMidi, 4.0) != 0) return 61;
    TestAudioPlayHead changingTempoPlayHead;
    changingTempoPlayHead.position.setIsPlaying(true);
    changingTempoPlayHead.position.setPpqPosition(0.0);
    changingTempoPlayHead.position.setBpm(120.0);
    hostTempoChangeSequencer.setPlayHead(&changingTempoPlayHead);
    hostTempoChangeSequencer.setSequencerPlaybackEnabled(true);
    juce::AudioBuffer<float> hostTempoAudio(2, 128);
    juce::MidiBuffer hostTempoMidi;
    hostTempoChangeSequencer.processBlock(hostTempoAudio, hostTempoMidi);
    if (hostTempoChangeSequencer.consumeNoteTriggerCount(36) != 0) return 61;
    changingTempoPlayHead.position.setPpqPosition(128.0 * 120.0 / (60.0 * 48000.0));
    changingTempoPlayHead.position.setBpm(60.0);
    hostTempoChangeSequencer.processBlock(hostTempoAudio, hostTempoMidi);
    if (hostTempoChangeSequencer.consumeNoteTriggerCount(36) != 0) return 61;
    changingTempoPlayHead.position.setPpqPosition(0.008);
    hostTempoChangeSequencer.processBlock(hostTempoAudio, hostTempoMidi);
    if (hostTempoChangeSequencer.consumeNoteTriggerCount(36) != 1) return 61;
    changingTempoPlayHead.position.setPpqPosition(0.010666666666666666);
    hostTempoChangeSequencer.processBlock(hostTempoAudio, hostTempoMidi);
    if (hostTempoChangeSequencer.consumeNoteTriggerCount(36) != 0) return 61;

    auto& hostLoopSequencer = makeTestProcessor();
    if (!hostLoopSequencer.loadSampleForNote(36, sample)) return 62;
    hostLoopSequencer.prepareToPlay(48000.0, 128);
    const std::vector<DrumEvent> hostLoopEvents{
        {DrumInstrument::Kick, DrumArticulation::Default, 100, 0.0, 0.1},
        {DrumInstrument::Kick, DrumArticulation::Default, 100, 3.998, 0.1}};
    if (hostLoopSequencer.setSequencerPattern(hostLoopEvents, generalMidi, 4.0) != 0) return 62;
    TestAudioPlayHead loopingPlayHead;
    loopingPlayHead.position.setIsPlaying(true);
    loopingPlayHead.position.setPpqPosition(3.997);
    loopingPlayHead.position.setBpm(120.0);
    hostLoopSequencer.setPlayHead(&loopingPlayHead);
    hostLoopSequencer.setSequencerPlaybackEnabled(true);
    juce::AudioBuffer<float> hostLoopAudio(2, 128);
    juce::MidiBuffer hostLoopMidi;
    hostLoopSequencer.processBlock(hostLoopAudio, hostLoopMidi);
    if (hostLoopSequencer.consumeNoteTriggerCount(36) != 2) return 62;
    loopingPlayHead.position.setPpqPosition(4.002333333333333);
    hostLoopSequencer.processBlock(hostLoopAudio, hostLoopMidi);
    if (hostLoopSequencer.consumeNoteTriggerCount(36) != 0) return 62;
    loopingPlayHead.position.setPpqPosition(0.0);
    hostLoopSequencer.processBlock(hostLoopAudio, hostLoopMidi);
    if (hostLoopSequencer.consumeNoteTriggerCount(36) != 1) return 62;
    loopingPlayHead.position.setIsPlaying(false);
    hostLoopSequencer.processBlock(hostLoopAudio, hostLoopMidi);
    if (hostLoopSequencer.isSequencerHostSynchronized()
        || hostLoopSequencer.consumeNoteTriggerCount(36) != 0) return 62;

    hostPlayHead.position.setIsPlaying(false);
    juce::AudioBuffer<float> stoppedHostAudio(2, 128);
    juce::MidiBuffer stoppedHostMidi;
    hostSyncedSequencer.processBlock(stoppedHostAudio, stoppedHostMidi);
    if (hostSyncedSequencer.isSequencerHostSynchronized()) return 38;

    auto& controlStateSource = makeTestProcessor();
    if (!controlStateSource.loadSampleForNote(36, sample)) return 22;
    controlStateSource.setNoteParameters(36, {-6.0f, -1.0f, 12.0f, 25.0f, 80.0f, 0.4f, 350.0f, 8000.0f, 12.5f, -6.0f, 3.0f, 4.0f, 0.2f, 0.9f});
    controlStateSource.setNoteParameters(37, {99.0f, -99.0f, 99.0f, 2000.0f, 6000.0f, -0.5f, 6000.0f, 25000.0f, 99.0f, -99.0f, 99.0f, -99.0f, 1.2f, 0.5f});
    auto configuredEffects = controlStateSource.getNoteParameters(36);
    configuredEffects.compressorThresholdDb = -18.0f;
    configuredEffects.compressorRatio = 4.0f;
    configuredEffects.compressorAttackMs = 2.5f;
    configuredEffects.compressorReleaseMs = 75.0f;
    configuredEffects.compressorMakeupDb = 3.0f;
    configuredEffects.phaserRateHz = 1.25f;
    configuredEffects.phaserDepth = 0.65f;
    configuredEffects.phaserFeedback = 0.3f;
    configuredEffects.phaserMix = 0.8f;
    configuredEffects.filterResonance = 4.75f;
    configuredEffects.chokeGroup = 3;
    controlStateSource.setNoteParameters(36, configuredEffects);
    auto extremeEffects = controlStateSource.getNoteParameters(37);
    extremeEffects.compressorThresholdDb = -100.0f;
    extremeEffects.compressorRatio = 30.0f;
    extremeEffects.compressorAttackMs = 0.0f;
    extremeEffects.compressorReleaseMs = 1000.0f;
    extremeEffects.compressorMakeupDb = 30.0f;
    extremeEffects.phaserRateHz = 20.0f;
    extremeEffects.phaserDepth = 2.0f;
    extremeEffects.phaserFeedback = -2.0f;
    extremeEffects.phaserMix = 2.0f;
    extremeEffects.filterResonance = 99.0f;
    extremeEffects.chokeGroup = 99;
    controlStateSource.setNoteParameters(37, extremeEffects);
    const auto clampedParameters = controlStateSource.getNoteParameters(37);
    if (std::abs(clampedParameters.gainDb - 6.0f) > 0.001f
        || std::abs(clampedParameters.pan + 1.0f) > 0.001f
        || std::abs(clampedParameters.tuneSemitones - 24.0f) > 0.001f
        || std::abs(clampedParameters.attackMs - 1000.0f) > 0.001f
        || std::abs(clampedParameters.decayMs - 5000.0f) > 0.001f
        || std::abs(clampedParameters.sustainLevel) > 0.001f
        || std::abs(clampedParameters.releaseMs - 5000.0f) > 0.001f
        || std::abs(clampedParameters.filterCutoffHz - 20000.0f) > 0.001f
        || std::abs(clampedParameters.driveDb - 24.0f) > 0.001f
        || std::abs(clampedParameters.eqLowDb + 18.0f) > 0.001f
        || std::abs(clampedParameters.eqMidDb - 18.0f) > 0.001f
        || std::abs(clampedParameters.eqHighDb + 18.0f) > 0.001f
        || std::abs(clampedParameters.sampleStart - 0.99f) > 0.001f
        || std::abs(clampedParameters.sampleEnd - 1.0f) > 0.001f
        || std::abs(clampedParameters.compressorThresholdDb + 36.0f) > 0.001f
        || std::abs(clampedParameters.compressorRatio - 12.0f) > 0.001f
        || std::abs(clampedParameters.compressorAttackMs - 0.1f) > 0.001f
        || std::abs(clampedParameters.compressorReleaseMs - 500.0f) > 0.001f
        || std::abs(clampedParameters.compressorMakeupDb - 12.0f) > 0.001f
        || std::abs(clampedParameters.phaserRateHz - 8.0f) > 0.001f
        || std::abs(clampedParameters.phaserDepth - 1.0f) > 0.001f
        || std::abs(clampedParameters.phaserFeedback + 0.8f) > 0.001f
        || std::abs(clampedParameters.phaserMix - 1.0f) > 0.001f
        || std::abs(clampedParameters.filterResonance - 10.0f) > 0.001f
        || clampedParameters.chokeGroup != 16) return 23;
    juce::MemoryBlock controlState;
    controlStateSource.getStateInformation(controlState);
    auto& controlStateRestored = makeTestProcessor();
    controlStateRestored.setStateInformation(controlState.getData(), static_cast<int>(controlState.getSize()));
    if (!waitForSamplePath(controlStateRestored, 36, sample.getFullPathName())) return 24;
    const auto restoredParameters = controlStateRestored.getNoteParameters(36);
    const auto* restoredGainParameter = findHostFloatParameter(controlStateRestored, "magdrum_pad_36_gain");
    const auto* restoredChokeParameter = findHostFloatParameter(controlStateRestored,
        "magdrum_pad_36_choke_group");
    if (restoredGainParameter == nullptr || std::abs(restoredGainParameter->get() + 6.0f) > 0.001f
        || restoredChokeParameter == nullptr || std::abs(restoredChokeParameter->get() - 3.0f) > 0.001f) return 261;
    if (std::abs(restoredParameters.gainDb + 6.0f) > 0.001f
        || std::abs(restoredParameters.pan + 1.0f) > 0.001f
        || std::abs(restoredParameters.tuneSemitones - 12.0f) > 0.001f
        || std::abs(restoredParameters.attackMs - 25.0f) > 0.001f
        || std::abs(restoredParameters.decayMs - 80.0f) > 0.001f
        || std::abs(restoredParameters.sustainLevel - 0.4f) > 0.001f
        || std::abs(restoredParameters.releaseMs - 350.0f) > 0.001f
        || std::abs(restoredParameters.filterCutoffHz - 8000.0f) > 0.001f
        || std::abs(restoredParameters.driveDb - 12.5f) > 0.001f
        || std::abs(restoredParameters.eqLowDb + 6.0f) > 0.001f
        || std::abs(restoredParameters.eqMidDb - 3.0f) > 0.001f
        || std::abs(restoredParameters.eqHighDb - 4.0f) > 0.001f
        || std::abs(restoredParameters.sampleStart - 0.2f) > 0.001f
        || std::abs(restoredParameters.sampleEnd - 0.9f) > 0.001f
        || std::abs(restoredParameters.compressorThresholdDb + 18.0f) > 0.001f
        || std::abs(restoredParameters.compressorRatio - 4.0f) > 0.001f
        || std::abs(restoredParameters.compressorAttackMs - 2.5f) > 0.001f
        || std::abs(restoredParameters.compressorReleaseMs - 75.0f) > 0.001f
        || std::abs(restoredParameters.compressorMakeupDb - 3.0f) > 0.001f
        || std::abs(restoredParameters.phaserRateHz - 1.25f) > 0.001f
        || std::abs(restoredParameters.phaserDepth - 0.65f) > 0.001f
        || std::abs(restoredParameters.phaserFeedback - 0.3f) > 0.001f
        || std::abs(restoredParameters.phaserMix - 0.8f) > 0.001f
        || std::abs(restoredParameters.filterResonance - 4.75f) > 0.001f
        || restoredParameters.chokeGroup != 3
        || std::abs(controlStateRestored.getNoteParameters(37).gainDb - 6.0f) > 0.001f) return 24;
    controlStateSource.setNoteParameters(38, {std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity(),
                                               -std::numeric_limits<float>::infinity(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::infinity(),
                                               -std::numeric_limits<float>::infinity(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN(),
                                               std::numeric_limits<float>::quiet_NaN()});
    const auto finiteParameters = controlStateSource.getNoteParameters(38);
    if (std::abs(finiteParameters.gainDb) > 0.001f || std::abs(finiteParameters.pan) > 0.001f
        || std::abs(finiteParameters.tuneSemitones) > 0.001f) return 31;
    const auto differs = [](float actual, float expected)
    { return std::abs(actual - expected) > 0.001f; };
    if (differs(finiteParameters.attackMs, 0.0f) || differs(finiteParameters.decayMs, 0.0f)
        || differs(finiteParameters.sustainLevel, 1.0f) || differs(finiteParameters.releaseMs, 0.0f)
        || differs(finiteParameters.filterCutoffHz, 20000.0f) || differs(finiteParameters.driveDb, 0.0f)
        || differs(finiteParameters.eqLowDb, 0.0f) || differs(finiteParameters.eqMidDb, 0.0f)
        || differs(finiteParameters.eqHighDb, 0.0f) || differs(finiteParameters.sampleStart, 0.0f)
        || differs(finiteParameters.sampleEnd, 1.0f) || differs(finiteParameters.compressorThresholdDb, 0.0f)
        || differs(finiteParameters.compressorRatio, 1.0f) || differs(finiteParameters.compressorAttackMs, 10.0f)
        || differs(finiteParameters.compressorReleaseMs, 100.0f) || differs(finiteParameters.compressorMakeupDb, 0.0f)
        || differs(finiteParameters.phaserRateHz, 0.0f) || differs(finiteParameters.phaserDepth, 0.0f)
        || differs(finiteParameters.phaserFeedback, 0.0f) || differs(finiteParameters.phaserMix, 0.0f)
        || std::abs(finiteParameters.filterResonance - 0.707f) > 0.001f)
    {
        std::cerr << "Non-finite parameter normalization mismatch: gain=" << finiteParameters.gainDb
                  << ", pan=" << finiteParameters.pan << ", tune=" << finiteParameters.tuneSemitones
                  << ", attack=" << finiteParameters.attackMs << ", decay=" << finiteParameters.decayMs
                  << ", sustain=" << finiteParameters.sustainLevel << ", release=" << finiteParameters.releaseMs
                  << ", cutoff=" << finiteParameters.filterCutoffHz << ", resonance=" << finiteParameters.filterResonance
                  << ", drive=" << finiteParameters.driveDb << ", eq=" << finiteParameters.eqLowDb << '/'
                  << finiteParameters.eqMidDb << '/' << finiteParameters.eqHighDb << ", trim="
                  << finiteParameters.sampleStart << '/' << finiteParameters.sampleEnd << ", comp="
                  << finiteParameters.compressorThresholdDb << '/' << finiteParameters.compressorRatio << '/'
                  << finiteParameters.compressorAttackMs << '/' << finiteParameters.compressorReleaseMs << '/'
                  << finiteParameters.compressorMakeupDb << ", phaser=" << finiteParameters.phaserRateHz << '/'
                  << finiteParameters.phaserDepth << '/' << finiteParameters.phaserFeedback << '/'
                  << finiteParameters.phaserMix << ".\n";
        return 40;
    }

    auto& gainReference = makeTestProcessor();
    auto& gainReduced = makeTestProcessor();
    if (!gainReference.loadSampleForNote(36, sample) || !gainReduced.loadSampleForNote(36, sample)) return 25;
    gainReference.prepareToPlay(44100.0, 128);
    gainReduced.prepareToPlay(44100.0, 128);
    gainReduced.setNoteParameters(36, {-6.0f, 0.0f, 0.0f});
    const double unattenuatedPeak = renderPeak(gainReference, 36, 127);
    const double attenuatedPeak = renderPeak(gainReduced, 36, 127);
    if (unattenuatedPeak <= 0.05 || std::abs(attenuatedPeak / unattenuatedPeak - 0.501187) > 0.02) return 26;

    auto& attackReference = makeTestProcessor();
    auto& attackShaped = makeTestProcessor();
    if (!attackReference.loadSampleForNote(36, sample) || !attackShaped.loadSampleForNote(36, sample)) return 41;
    attackReference.prepareToPlay(48000.0, 128);
    attackShaped.prepareToPlay(48000.0, 128);
    attackShaped.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 100.0f, 0.0f, 1.0f, 0.0f, 20000.0f});
    const double naturalAttackPeak = renderPeakInRange(attackReference, 36, 127, 0, 128);
    const double shapedAttackPeak = renderPeakInRange(attackShaped, 36, 127, 0, 128);
    if (naturalAttackPeak < 0.05 || shapedAttackPeak / naturalAttackPeak > 0.05) return 42;

    auto& envelopeReference = makeTestProcessor();
    auto& envelopeShaped = makeTestProcessor();
    if (!envelopeReference.loadSampleForNote(36, sample) || !envelopeShaped.loadSampleForNote(36, sample)) return 45;
    envelopeReference.prepareToPlay(48000.0, 128);
    envelopeShaped.prepareToPlay(48000.0, 128);
    envelopeShaped.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 0.0f, 8.0f, 0.25f, 0.0f, 20000.0f});
    const double naturalSustainPeak = renderPeakInRange(envelopeReference, 36, 127, 600, 128);
    const double shapedSustainPeak = renderPeakInRange(envelopeShaped, 36, 127, 600, 128);
    if (naturalSustainPeak < 0.05 || std::abs(shapedSustainPeak / naturalSustainPeak - 0.25) > 0.03) return 46;

    auto& releaseReference = makeTestProcessor();
    auto& releasedPad = makeTestProcessor();
    if (!releaseReference.loadSampleForNote(36, sample) || !releasedPad.loadSampleForNote(36, sample)) return 47;
    releaseReference.prepareToPlay(48000.0, 128);
    releasedPad.prepareToPlay(48000.0, 128);
    releasedPad.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 8.0f, 20000.0f});
    const double naturalReleasePeak = renderPeakInRange(releaseReference, 36, 127, 900, 60);
    const double shapedReleasePeak = renderPeakInRange(releasedPad, 36, 127, 900, 60);
    if (naturalReleasePeak < 0.05 || shapedReleasePeak / naturalReleasePeak > 0.25) return 48;

    auto& overlappingEnvelopeReference = makeTestProcessor();
    auto& overlappingEnvelopePad = makeTestProcessor();
    if (!overlappingEnvelopeReference.loadSampleForNote(36, sample)
        || !overlappingEnvelopePad.loadSampleForNote(36, sample)) return 49;
    overlappingEnvelopeReference.prepareToPlay(48000.0, 128);
    overlappingEnvelopePad.prepareToPlay(48000.0, 128);
    overlappingEnvelopePad.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 1.0f, 100.0f, 20000.0f});
    const double overlappingNaturalPeak = renderPeak(overlappingEnvelopeReference, 36, 127);
    const double overlappingEnvelopePeak = renderPeak(overlappingEnvelopePad, 36, 127);
    if (overlappingNaturalPeak < 0.05 || overlappingEnvelopePeak / overlappingNaturalPeak < 0.02) return 50;

    auto& filterReference = makeTestProcessor();
    auto& filteredPad = makeTestProcessor();
    if (!filterReference.loadSampleForNote(36, sample) || !filteredPad.loadSampleForNote(36, sample)) return 43;
    filterReference.prepareToPlay(48000.0, 128);
    filteredPad.prepareToPlay(48000.0, 128);
    filteredPad.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f});
    const double naturalFilterPeak = renderPeak(filterReference, 36, 127);
    const double filteredPeak = renderPeak(filteredPad, 36, 127);
    if (naturalFilterPeak < 0.05 || filteredPeak / naturalFilterPeak > 0.6) return 44;

    auto& driveReference = makeTestProcessor();
    auto& drivenPad = makeTestProcessor();
    if (!driveReference.loadSampleForNote(36, sample) || !drivenPad.loadSampleForNote(36, sample)) return 51;
    driveReference.prepareToPlay(48000.0, 128);
    drivenPad.prepareToPlay(48000.0, 128);
    drivenPad.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20000.0f, 12.0f});
    const double cleanDrivePeak = renderPeak(driveReference, 36, 127);
    const double saturatedDrivePeak = renderPeak(drivenPad, 36, 127);
    if (cleanDrivePeak < 0.05 || saturatedDrivePeak >= cleanDrivePeak * 0.95) return 52;

    auto& eqReference = makeTestProcessor();
    auto& eqLowReduced = makeTestProcessor();
    if (!eqReference.loadSampleForNote(36, sample) || !eqLowReduced.loadSampleForNote(36, sample)) return 53;
    eqReference.prepareToPlay(48000.0, 128);
    eqLowReduced.prepareToPlay(48000.0, 128);
    eqLowReduced.setNoteParameters(36, {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 20000.0f, 0.0f, -18.0f});
    const double unfilteredEqPeak = renderPeak(eqReference, 36, 127);
    const double lowReducedPeak = renderPeak(eqLowReduced, 36, 127);
    if (unfilteredEqPeak < 0.05 || lowReducedPeak >= unfilteredEqPeak * 0.8) return 54;

    auto& neutralFilter = makeTestProcessor();
    auto& resonantFilter = makeTestProcessor();
    if (!neutralFilter.loadSampleForNote(36, resonantFilterSample)
        || !resonantFilter.loadSampleForNote(36, resonantFilterSample)) return 75;
    neutralFilter.prepareToPlay(48000.0, 128);
    resonantFilter.prepareToPlay(48000.0, 128);
    auto neutralFilterParameters = neutralFilter.getNoteParameters(36);
    neutralFilterParameters.filterCutoffHz = 4000.0f;
    neutralFilterParameters.filterResonance = 0.707f;
    neutralFilter.setNoteParameters(36, neutralFilterParameters);
    auto resonantFilterParameters = resonantFilter.getNoteParameters(36);
    resonantFilterParameters.filterCutoffHz = 4000.0f;
    resonantFilterParameters.filterResonance = 8.0f;
    resonantFilter.setNoteParameters(36, resonantFilterParameters);
    const auto neutralFilterSignal = renderSamples(neutralFilter, 36, 127, 48000);
    const auto resonantFilterSignal = renderSamples(resonantFilter, 36, 127, 48000);
    double neutralFilterEnergy = 0.0, resonantFilterEnergy = 0.0;
    for (size_t index = 0; index < neutralFilterSignal.size(); ++index)
    {
        neutralFilterEnergy += neutralFilterSignal[index] * neutralFilterSignal[index];
        resonantFilterEnergy += resonantFilterSignal[index] * resonantFilterSignal[index];
    }
    if (neutralFilterEnergy <= 0.0 || resonantFilterEnergy < neutralFilterEnergy * 16.0)
    {
        std::cerr << "Per-pad filter resonance did not emphasize frequencies around the cutoff.\n";
        return 76;
    }

    auto& compressorReference = makeTestProcessor();
    auto& compressedPad = makeTestProcessor();
    if (!compressorReference.loadSampleForNote(36, sample) || !compressedPad.loadSampleForNote(36, sample)) return 65;
    compressorReference.prepareToPlay(48000.0, 128);
    compressedPad.prepareToPlay(48000.0, 128);
    auto compressorParameters = compressedPad.getNoteParameters(36);
    compressorParameters.compressorThresholdDb = -24.0f;
    compressorParameters.compressorRatio = 8.0f;
    compressorParameters.compressorAttackMs = 0.1f;
    compressedPad.setNoteParameters(36, compressorParameters);
    const double cleanCompressorPeak = renderPeak(compressorReference, 36, 127);
    const double reducedCompressorPeak = renderPeak(compressedPad, 36, 127);
    if (cleanCompressorPeak < 0.05 || reducedCompressorPeak >= cleanCompressorPeak * 0.8) return 66;

    auto& phaserReference = makeTestProcessor();
    auto& phasedPad = makeTestProcessor();
    if (!phaserReference.loadSampleForNote(36, sample) || !phasedPad.loadSampleForNote(36, sample)) return 67;
    phaserReference.prepareToPlay(48000.0, 128);
    phasedPad.prepareToPlay(48000.0, 128);
    auto phaserParameters = phasedPad.getNoteParameters(36);
    phaserParameters.phaserRateHz = 1.2f;
    phaserParameters.phaserDepth = 0.9f;
    phaserParameters.phaserFeedback = 0.45f;
    phaserParameters.phaserMix = 0.9f;
    phasedPad.setNoteParameters(36, phaserParameters);
    const auto drySignal = renderSamples(phaserReference, 36, 127);
    const auto wetSignal = renderSamples(phasedPad, 36, 127);
    double phaserDifference = 0.0;
    for (size_t index = 0; index < drySignal.size(); ++index)
        phaserDifference += std::abs(static_cast<double>(drySignal[index] - wetSignal[index]));
    if (phaserDifference < 1.0) return 68;

    auto& trimmedPlayback = makeTestProcessor();
    if (!trimmedPlayback.loadSampleForNote(36, trimSample)) return 55;
    trimmedPlayback.prepareToPlay(48000.0, 128);
    auto trimParameters = trimmedPlayback.getNoteParameters(36);
    trimParameters.sampleStart = 0.25f;
    trimParameters.sampleEnd = 0.75f;
    trimmedPlayback.setNoteParameters(36, trimParameters);
    juce::AudioBuffer<float> trimAudio(2, 960);
    int firstTrimmedSignal = -1, lastTrimmedSignal = -1;
    for (int blockStart = 0; blockStart < trimAudio.getNumSamples(); blockStart += 128)
    {
        const int blockSize = juce::jmin(128, trimAudio.getNumSamples() - blockStart);
        juce::AudioBuffer<float> block(2, blockSize);
        juce::MidiBuffer midi;
        if (blockStart == 0)
            midi.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(127)), 0);
        trimmedPlayback.processBlock(block, midi);
        for (int frame = 0; frame < blockSize; ++frame)
        {
            const float value = juce::jmax(std::abs(block.getSample(0, frame)), std::abs(block.getSample(1, frame)));
            if (value > 0.0001f)
            {
                const int absoluteFrame = blockStart + frame;
                if (firstTrimmedSignal < 0) firstTrimmedSignal = absoluteFrame;
                lastTrimmedSignal = absoluteFrame;
                trimAudio.setSample(0, absoluteFrame, value);
            }
        }
    }
    if (firstTrimmedSignal != 360 || lastTrimmedSignal >= 400
        || trimAudio.getSample(0, 360) < 0.4f)
    {
        std::cerr << "Sample trim output range: first=" << firstTrimmedSignal
                  << " last=" << lastTrimmedSignal << " peak=" << trimAudio.getSample(0, 360) << "\n";
        return 56;
    }

    auto& leftPan = makeTestProcessor();
    auto& rightPan = makeTestProcessor();
    if (!leftPan.loadSampleForNote(36, sample) || !rightPan.loadSampleForNote(36, sample)) return 27;
    leftPan.prepareToPlay(44100.0, 128);
    rightPan.prepareToPlay(44100.0, 128);
    leftPan.setNoteParameters(36, {0.0f, -1.0f, 0.0f});
    rightPan.setNoteParameters(36, {0.0f, 1.0f, 0.0f});
    const auto leftPeaks = renderChannelPeaks(leftPan, 36, 127);
    const auto rightPeaks = renderChannelPeaks(rightPan, 36, 127);
    if (leftPeaks[0] < 0.05 || leftPeaks[1] > 0.001
        || rightPeaks[1] < 0.05 || rightPeaks[0] > 0.001) return 28;

    auto& octaveUp = makeTestProcessor();
    if (!octaveUp.loadSampleForNote(36, sample)) return 29;
    octaveUp.prepareToPlay(44100.0, 128);
    octaveUp.setNoteParameters(36, {0.0f, 0.0f, 12.0f});
    if (renderPeakInRange(octaveUp, 36, 127, 500, 100) > 0.0) return 30;

    const auto softLayer = root.getChildFile("kick-soft.wav");
    const auto hardLayer = root.getChildFile("kick-hard.wav");
    if (!writeFixture(softLayer, 0.10f) || !writeFixture(hardLayer, 0.22f)) return 15;
    auto& layered = makeTestProcessor();
    if (!layered.loadSampleForVelocityRange(36, 64, 127, hardLayer)
        || !layered.loadSampleForVelocityRange(36, 1, 63, softLayer)
        || layered.loadSampleForVelocityRange(36, 50, 80, sample)
        || layered.loadSampleForNote(36, sample)
        || layered.getSamplePathForNote(36) != softLayer.getFullPathName()) return 16;
    const auto layeredAssignments = layered.getAllSampleAssignments();
    bool hasSoftAssignment = false, hasHardAssignment = false;
    for (const auto& assignment : layeredAssignments)
    {
        if (assignment.midiNote != 36) return 36;
        hasSoftAssignment = hasSoftAssignment || (assignment.minimumVelocity == 1 && assignment.maximumVelocity == 63);
        hasHardAssignment = hasHardAssignment || (assignment.minimumVelocity == 64 && assignment.maximumVelocity == 127);
    }
    if (layeredAssignments.size() != 2 || !hasSoftAssignment || !hasHardAssignment) return 37;
    juce::MemoryBlock layeredState;
    layered.getStateInformation(layeredState);
    auto& restoredLayers = makeTestProcessor();
    restoredLayers.setStateInformation(layeredState.getData(), static_cast<int>(layeredState.getSize()));
    if (!waitForSamplePath(restoredLayers, 36, softLayer.getFullPathName())
        || !waitForSampleLayerCount(restoredLayers, 36, 2)) return 17;
    const auto restoredLayerInfo = restoredLayers.getSampleLayersForNote(36);
    if (restoredLayers.getSamplePathForNote(36) != softLayer.getFullPathName() || restoredLayerInfo.size() != 2
        || restoredLayerInfo[0].minimumVelocity != 1 || restoredLayerInfo[0].maximumVelocity != 63
        || restoredLayerInfo[1].minimumVelocity != 64 || restoredLayerInfo[1].maximumVelocity != 127) return 17;
    if (restoredLayerInfo[0].slotIndex < 0 || restoredLayerInfo[1].slotIndex < 0
        || restoredLayerInfo[0].slotIndex == restoredLayerInfo[1].slotIndex
        || !restoredLayers.updateSampleLayerVelocityRange(36, restoredLayerInfo[0].slotIndex, 1, 55)
        || restoredLayers.updateSampleLayerVelocityRange(36, restoredLayerInfo[0].slotIndex, 55, 70)
        || restoredLayers.updateSampleLayerVelocityRange(36, restoredLayerInfo[0].slotIndex, 0, 55)) return 44;
    const auto editedLayerInfo = restoredLayers.getSampleLayersForNote(36);
    if (editedLayerInfo.size() != 2 || editedLayerInfo[0].minimumVelocity != 1
        || editedLayerInfo[0].maximumVelocity != 55 || editedLayerInfo[1].minimumVelocity != 64) return 45;
    juce::MemoryBlock editedLayerState;
    restoredLayers.getStateInformation(editedLayerState);
    auto& restoredEditedLayers = makeTestProcessor();
    restoredEditedLayers.setStateInformation(editedLayerState.getData(),
        static_cast<int>(editedLayerState.getSize()));
    if (!waitForSampleLayerCount(restoredEditedLayers, 36, 2)) return 46;
    const auto roundTrippedLayerInfo = restoredEditedLayers.getSampleLayersForNote(36);
    if (roundTrippedLayerInfo.size() != 2 || roundTrippedLayerInfo[0].minimumVelocity != 1
        || roundTrippedLayerInfo[0].maximumVelocity != 55 || roundTrippedLayerInfo[1].minimumVelocity != 64)
        return 47;
    restoredLayers.prepareToPlay(44100.0, 128);
    const double softPeak = renderPeak(restoredLayers, 36, 32);
    const double hardPeak = renderPeak(restoredLayers, 36, 96);
    if (softPeak <= 0.0 || hardPeak <= softPeak * 3.0) return 18;
    if (!restoredLayers.removeSampleLayer(36, restoredLayerInfo[1].slotIndex)
        || restoredLayers.removeSampleLayer(36, restoredLayerInfo[1].slotIndex)
        || restoredLayers.getSampleLayersForNote(36).size() != 1) return 48;
    MagDrumAudioProcessorTestAccess::stopVoices(restoredLayers);
    if (renderPeak(restoredLayers, 36, 96) != 0.0) return 48;
    if (!restoredLayers.clearSamplesForNote(36) || restoredLayers.clearSamplesForNote(36)
        || restoredLayers.getSamplePathForNote(36).isNotEmpty() || renderPeak(restoredLayers, 36, 96) != 0.0) return 19;

    const auto roundRobinFirstFile = root.getChildFile("kick-round-robin-1.wav");
    const auto roundRobinSecondFile = root.getChildFile("kick-round-robin-2.wav");
    const auto roundRobinHighVelocityFile = root.getChildFile("kick-round-robin-high-velocity.wav");
    if (!writeFixture(roundRobinFirstFile, 0.10f) || !writeFixture(roundRobinSecondFile, 0.22f)
        || !writeFixture(roundRobinHighVelocityFile, 0.17f)) return 38;
    auto& roundRobin = makeTestProcessor();
    if (!roundRobin.loadSampleForVelocityRange(36, 1, 127, roundRobinFirstFile)
        || !roundRobin.loadSampleForVelocityRange(36, 1, 127, roundRobinSecondFile)) return 38;
    const auto roundRobinLayers = roundRobin.getSampleLayersForNote(36);
    if (roundRobinLayers.size() != 2 || roundRobinLayers[0].roundRobinIndex != 1
        || roundRobinLayers[0].roundRobinCount != 2 || roundRobinLayers[1].roundRobinIndex != 2
        || roundRobinLayers[1].roundRobinCount != 2) return 39;
    roundRobin.prepareToPlay(44100.0, 128);
    const double firstRoundRobinPeak = renderPeak(roundRobin, 36, 100);
    MagDrumAudioProcessorTestAccess::stopVoices(roundRobin);
    const double secondRoundRobinPeak = renderPeak(roundRobin, 36, 100);
    MagDrumAudioProcessorTestAccess::stopVoices(roundRobin);
    const double thirdRoundRobinPeak = renderPeak(roundRobin, 36, 100);
    if (firstRoundRobinPeak <= 0.0 || secondRoundRobinPeak < firstRoundRobinPeak * 1.8
        || std::abs(thirdRoundRobinPeak - firstRoundRobinPeak) > 0.0001) return 40;

    auto& independentRoundRobin = makeTestProcessor();
    if (!independentRoundRobin.loadSampleForVelocityRange(36, 1, 63, roundRobinFirstFile)
        || !independentRoundRobin.loadSampleForVelocityRange(36, 1, 63, roundRobinSecondFile)
        || !independentRoundRobin.loadSampleForVelocityRange(36, 64, 127, roundRobinHighVelocityFile)) return 282;
    independentRoundRobin.prepareToPlay(44100.0, 128);
    const double independentFirstPeak = renderPeak(independentRoundRobin, 36, 60);
    MagDrumAudioProcessorTestAccess::stopVoices(independentRoundRobin);
    const double otherVelocityPeak = renderPeak(independentRoundRobin, 36, 100);
    MagDrumAudioProcessorTestAccess::stopVoices(independentRoundRobin);
    const double independentSecondPeak = renderPeak(independentRoundRobin, 36, 60);
    MagDrumAudioProcessorTestAccess::stopVoices(independentRoundRobin);
    if (independentFirstPeak <= 0.0 || otherVelocityPeak <= 0.0
        || independentSecondPeak < independentFirstPeak * 1.8) return 283;

    const auto longTailFile = root.getChildFile("long-tail-sample.wav");
    const auto shortTailFile = root.getChildFile("short-tail-sample.wav");
    if (!writeFixture(longTailFile, 0.15f, 12 * 48000) || !writeFixture(shortTailFile, 0.1f, 48000)) return 284;
    auto& reportedTail = makeTestProcessor();
    if (reportedTail.getTailLengthSeconds() != 0.0
        || !reportedTail.loadSampleForNote(36, longTailFile)
        || !reportedTail.loadSampleForNote(38, shortTailFile)
        || std::abs(reportedTail.getTailLengthSeconds() - 12.0) > 0.001) return 285;
    if (!reportedTail.clearSamplesForNote(36) || std::abs(reportedTail.getTailLengthSeconds() - 1.0) > 0.001
        || !reportedTail.clearSamplesForNote(38) || reportedTail.getTailLengthSeconds() != 0.0) return 286;
    if (!reportedTail.previewSample(longTailFile) || std::abs(reportedTail.getTailLengthSeconds() - 12.0) > 0.001)
        return 287;
    reportedTail.stopSamplePreview();
    if (reportedTail.getTailLengthSeconds() != 0.0) return 288;

    juce::MemoryBlock roundRobinState;
    roundRobin.getStateInformation(roundRobinState);
    auto& restoredRoundRobin = makeTestProcessor();
    restoredRoundRobin.setStateInformation(roundRobinState.getData(), static_cast<int>(roundRobinState.getSize()));
    if (!waitForSampleLayerCount(restoredRoundRobin, 36, 2)) return 41;
    const auto restoredRoundRobinLayers = restoredRoundRobin.getSampleLayersForNote(36);
    if (restoredRoundRobinLayers.size() != 2 || restoredRoundRobinLayers[0].roundRobinCount != 2
        || restoredRoundRobinLayers[1].roundRobinCount != 2) return 42;
    restoredRoundRobin.prepareToPlay(44100.0, 128);
    const double restoredFirstPeak = renderPeak(restoredRoundRobin, 36, 100);
    MagDrumAudioProcessorTestAccess::stopVoices(restoredRoundRobin);
    const double restoredSecondPeak = renderPeak(restoredRoundRobin, 36, 100);
    if (restoredFirstPeak <= 0.0 || restoredSecondPeak < restoredFirstPeak * 1.8) return 43;

    auto& concurrentSwaps = makeTestProcessor();
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

    auto& emptyNote = makeTestProcessor();
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
    previewProcessor->releaseResources();
    root.deleteRecursively();
    std::cout << "Scoped file copy, cancellation, MIDI playback, velocity, pad gain/pan/tune, state restoration and empty-note checks passed.\n";
    return 0;
}
