#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include "plugin/DrumPadLayout.h"
#include "core/PluginScanProcess.h"
#include "core/ScopedFileAccess.h"
#include "ui/MagDrumLocalization.h"
#include <cmath>
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

bool writeFixture(const juce::File& file, float amplitude = 0.25f, int sampleCount = 960)
{
    constexpr double sampleRate = 48000.0;
    juce::AudioBuffer<float> audio(1, sampleCount);
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
}

class MagDrumAudioProcessorEditorTestAccess
{
public:
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
        editor.buttonClicked(&editor.libraryTab);
        if (editor.choosePluginFolderButton.isVisible())
        {
            std::cerr << "Plugin-folder chooser stayed visible after leaving Plugin Scanner.\n";
            return 3;
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
            || std::abs(editor.sequenceEvents.front().positionBeats - 2.0) > 0.001
            || std::abs(editor.sequenceEvents.front().durationBeats - 0.5) > 0.001
            || editor.sequenceMetadata.size() != 2
            || std::abs(editor.sequenceMetadata[1].positionBeats - 2.0) > 0.001
            || ! editor.reviewMidiButton.isEnabled() || ! editor.editMidiEventsButton.isEnabled())
        {
            std::cerr << "The VST MIDI editor did not import SMPTE events on the tempo map.\n";
            return 2;
        }
        const auto state = processor.getPluginEditorState();
        if (state == nullptr || state->getNumChildElements() == 0)
            return 3;
        return 0;
    }

    static int checksLiveLanguageSwitching(MagDrumAudioProcessorEditor& editor,
                                           const juce::File& firstSample, const juce::File& secondSample)
    {
        using magdrum::ui::Language;
        editor.openSampleComparisonWindow(firstSample, secondSample);
        if (editor.comparisonWindow == nullptr || editor.comparisonWindow->getContentComponent() == nullptr)
            return 2;
        auto* comparisonContent = editor.comparisonWindow->getContentComponent();
        editor.openMidiEventEditor();
        if (editor.midiEventWindow == nullptr || editor.midiEventWindow->getContentComponent() == nullptr)
            return 5;
        auto* midiEventContent = editor.midiEventWindow->getContentComponent();
        const std::array<Language, 8> languages{
            Language::english, Language::german, Language::french, Language::spanish,
            Language::swedish, Language::norwegian, Language::danish, Language::finnish};
        struct LabelCheck { const char* name; juce::String visible, expected; };
        for (const auto language : languages)
        {
            magdrum::ui::setLanguage(language, false);
            const std::array<LabelCheck, 14> checks{{
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
                {"sample category filter", editor.categoryFilter.getText(), magdrum::ui::localisedText("All sounds")},
                {"sample sort filter", editor.sortFilter.getText(), magdrum::ui::localisedText("Name A-Z")}
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
                || editor.comparisonWindow->getName() != magdrum::ui::localisedText("Compare drum samples"))
            {
                std::cerr << "Live localization failed in the open sample-comparison window for "
                          << magdrum::ui::languageName(language) << ".\n";
                magdrum::ui::setLanguage(Language::english, false);
                return 4;
            }

            const juce::StringArray expectedEventButtons{
                magdrum::ui::localisedText("Apply edit"),
                magdrum::ui::localisedText("Add hit"),
                magdrum::ui::localisedText("Delete hit")};
            juce::StringArray actualEventButtons;
            for (int childIndex = 0; childIndex < midiEventContent->getNumChildComponents(); ++childIndex)
                if (auto* button = dynamic_cast<juce::TextButton*>(midiEventContent->getChildComponent(childIndex)))
                    actualEventButtons.add(button->getButtonText());
            actualEventButtons.sort(false);
            auto sortedExpectedEventButtons = expectedEventButtons;
            sortedExpectedEventButtons.sort(false);
            if (actualEventButtons != sortedExpectedEventButtons
                || editor.midiEventWindow->getName() != magdrum::ui::localisedText("Edit MIDI events"))
            {
                std::cerr << "Live localization failed in the open MIDI-event window for "
                          << magdrum::ui::languageName(language) << ".\n";
                magdrum::ui::setLanguage(Language::english, false);
                return 6;
            }
        }
        magdrum::ui::setLanguage(Language::english, false);
        return 0;
    }

    static int checksVirtualizedSequencerBar(MagDrumAudioProcessorEditor& editor,
                                              MagDrumAudioProcessor& processor)
    {
        if (editor.languageButton.getButtonText().isNotEmpty()) return 8;
        if (editor.steps.size() != 16 || editor.steps.front().size() != 32) return 1;
        if (editor.laneLabels[8].getText() != "Bell" || editor.laneLabels[15].getText() != "Cowbell") return 2;
        if (editor.mappedLaneNotes[8] != 53 || editor.mappedLaneNotes[15] != 56) return 9;
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

    if (argc == 4 && std::strcmp(argv[1], "--magdrum-test-write-plugin-state") == 0)
    {
        auto fixture = std::make_unique<MagDrumAudioProcessor>();
        if (!fixture->loadSampleForNote(36, juce::File(argv[2]))) return 201;
        juce::MemoryBlock processorState;
        fixture->getStateInformation(processorState);
        if (processorState.isEmpty()) return 202;

        // A host's AudioPluginInstance state wraps processor state in a VST3 envelope.
        juce::XmlElement hostedState("VST3PluginState");
        hostedState.createNewChildElement("IComponent")->addTextElement(processorState.toBase64Encoding());
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary(hostedState, state);
        return !state.isEmpty() && juce::File(argv[3]).replaceWithData(state.getData(), state.getSize()) ? 0 : 203;
    }

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
    const auto comparisonSample = root.getChildFile("comparison.wav");
    if (!writeFixture(comparisonSample, 0.18f)) return 3;

#if JUCE_WINDOWS
    {
        juce::ScopedJuceInitialiser_GUI guiInitialiser;
        MagDrumAudioProcessor editorProcessor;
        const auto editorDatabase = root.getChildFile("plugin-editor-db").getChildFile("samples.sqlite3");
        MagDrumAudioProcessorEditor editor(editorProcessor, editorDatabase);
        if (editor.getWidth() < 1000 || editor.getHeight() < 620) return 41;
        const int folderButtonCheck = MagDrumAudioProcessorEditorTestAccess::checksPluginFolderChooserVisibility(editor);
        if (folderButtonCheck != 0) return 44;
        const int editorCheck=MagDrumAudioProcessorEditorTestAccess::checksVirtualizedSequencerBar(editor, editorProcessor);
        if(editorCheck!=0){std::cerr<<"Virtualized sequencer editor smoke stage "<<editorCheck<<" failed.\n";return 41;}
        const int languageCheck=MagDrumAudioProcessorEditorTestAccess::checksLiveLanguageSwitching(editor, sample, comparisonSample);
        if(languageCheck!=0)return 42;
        const int midiSearchCancellationCheck=MagDrumAudioProcessorEditorTestAccess::checksDefaultMidiSearchCancellation(editor);
        if(midiSearchCancellationCheck!=0)return 43;
        const int smpteImportCheck = MagDrumAudioProcessorEditorTestAccess::checksSmpteMidiImport(editor, editorProcessor);
        if (smpteImportCheck != 0) return 288 + smpteImportCheck;
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
    if (!processor.loadSampleForNote(36, sample) || processor.getSamplePathForNote(36) != sample.getFullPathName()) return 8;
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
    if(restoredEditorState->getIntAttribute("bars",0)!=2||restoredEventCount!=3||restoredMetadataCount!=2)return 40;
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

    hostPlayHead.position.setIsPlaying(false);
    juce::AudioBuffer<float> stoppedHostAudio(2, 128);
    juce::MidiBuffer stoppedHostMidi;
    hostSyncedSequencer.processBlock(stoppedHostAudio, stoppedHostMidi);
    if (hostSyncedSequencer.isSequencerHostSynchronized()) return 38;

    auto& controlStateSource = makeTestProcessor();
    if (!controlStateSource.loadSampleForNote(36, sample)) return 22;
    controlStateSource.setNoteParameters(36, {-6.0f, -1.0f, 12.0f});
    controlStateSource.setNoteParameters(37, {99.0f, -99.0f, 99.0f});
    const auto clampedParameters = controlStateSource.getNoteParameters(37);
    if (std::abs(clampedParameters.gainDb - 6.0f) > 0.001f
        || std::abs(clampedParameters.pan + 1.0f) > 0.001f
        || std::abs(clampedParameters.tuneSemitones - 24.0f) > 0.001f) return 23;
    juce::MemoryBlock controlState;
    controlStateSource.getStateInformation(controlState);
    auto& controlStateRestored = makeTestProcessor();
    controlStateRestored.setStateInformation(controlState.getData(), static_cast<int>(controlState.getSize()));
    if (!waitForSamplePath(controlStateRestored, 36, sample.getFullPathName())) return 24;
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

    auto& gainReference = makeTestProcessor();
    auto& gainReduced = makeTestProcessor();
    if (!gainReference.loadSampleForNote(36, sample) || !gainReduced.loadSampleForNote(36, sample)) return 25;
    gainReference.prepareToPlay(44100.0, 128);
    gainReduced.prepareToPlay(44100.0, 128);
    gainReduced.setNoteParameters(36, {-6.0f, 0.0f, 0.0f});
    const double unattenuatedPeak = renderPeak(gainReference, 36, 127);
    const double attenuatedPeak = renderPeak(gainReduced, 36, 127);
    if (unattenuatedPeak <= 0.05 || std::abs(attenuatedPeak / unattenuatedPeak - 0.501187) > 0.02) return 26;

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
    restoredLayers.prepareToPlay(44100.0, 128);
    const double softPeak = renderPeak(restoredLayers, 36, 32);
    const double hardPeak = renderPeak(restoredLayers, 36, 96);
    if (softPeak <= 0.0 || hardPeak <= softPeak * 3.0) return 18;
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
