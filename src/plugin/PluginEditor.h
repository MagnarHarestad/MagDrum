#pragma once
#include "PluginProcessor.h"
#include "../core/SampleDatabase.h"
#include "../core/MidiPatternIO.h"
#include "../core/SequencerGrid.h"
#include "../core/WavScanner.h"
#include "../ui/MagDrumLookAndFeel.h"
#include <functional>
#include <vector>

class MagDrumSampleMap;
class MagDrumWaveform;
class MagDrumSampleComparison;

class MagDrumStepButton final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onRightClick;
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown()) { if (onRightClick) onRightClick(); return; }
        juce::TextButton::mouseDown(event);
    }
};

class MagDrumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener,
                                          private juce::ListBoxModel,
                                          private juce::Timer
{
public:
    explicit MagDrumAudioProcessorEditor(MagDrumAudioProcessor&);
    ~MagDrumAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;

private:
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    void updateLayerSummary();
    void refreshSamples();
    void setPage(int);
    void setNote(int);
    void updateDrumNotes();
    int stepsPerBar() const;
    int patternStepCount() const;
    int sequenceTempoAtBeat(double beat) const;
    void refreshSequenceMetadata();
    bool editSequenceMetadata(bool remove);
    MidiPattern currentSequencePattern() const;
    void reviewMidiChanges();
    int timeSignatureNumerator() const;
    int timeSignatureDenominator() const;
    void updateSequencerGrid();
    void refreshMappingSummary();
    void savePluginEditorState();
    void restorePluginEditorState();
    void importMidiPattern();
    void loadMidiPatternFile(const juce::File&);
    void searchDefaultMidiPatterns();
    void finishDefaultMidiPatternSearch(std::vector<juce::File>);
    void browseDefaultMidiPatterns();
    void chooseMappingCsv(bool exportCsv);
    void chooseSampleFolder();
    void scanDefaultLibraryFolders();
    void launchInstrumentScanner();
    void startNextDefaultLibraryFolder();
    void startSampleScan(const juce::File&);
    void finishSampleScan(const WavScanner::Stats&, std::int64_t, const juce::String&);
    void loadSamplePage(bool reset);
    void refreshSourceKitFilters();
    void showLibraryTools();
    void handleLibraryTool(int);
    juce::File selectedSample() const;

    magdrum::ui::LookAndFeel theme;
    MagDrumAudioProcessor& processor;
    SampleDatabase database;
    juce::File databaseFile;
    juce::Array<SampleBrowseRecord> samples;
    int page = 0, selectedNote = 36, sequenceStep = 0, sequenceTempo = 120;
    int sampleOffset = 0, previousMeterId = 404, previousPatternBars = 16;
    bool sequencerPlaying = false, databaseReady = false, scanning = false, hasMoreSamples = false;
    bool scanningDefaultLibraries = false;
    std::vector<juce::File> defaultLibraryFolders;
    std::vector<juce::File> defaultMidiPatterns;
    size_t defaultLibraryFolderIndex = 0;
    std::int64_t defaultLibraryIndexedCount = 0;
    std::int64_t defaultLibraryErrorCount = 0;
    std::shared_ptr<std::atomic<bool>> scanCancellation;
    juce::ThreadPool scanWorkers{1};
    juce::TextButton libraryTab{"SAMPLE LIBRARY"}, machineTab{"DRUM MACHINE"}, sequencerTab{"SEQUENCER"};
    juce::TextButton mapsTab{"MIDI MAPS"};
    juce::TextEditor sampleSearch;
    juce::ComboBox categoryFilter, featureFilter, sortFilter, confidenceFilter, sourceFilter, kitFilter;
    juce::TextButton scanButton{"Add sample folder"}, scanDefaultsButton{"Scan installed libraries"}, instrumentScannerButton{"Plugin instruments"}, searchButton{"Search"}, previewButton{"Preview"}, assignButton{"Assign to pad"}, libraryToolsButton{"Sample tools"}, moreSamplesButton{"More samples"};
    juce::ListBox indexedList;
    std::unique_ptr<MagDrumSampleMap> sampleMap;
    std::unique_ptr<MagDrumWaveform> waveform;
    std::array<juce::TextButton, 16> pads;
    juce::Label selectedPadTitle, layerSummary, sampleDetails, status;
    juce::TextEditor velocityRange;
    juce::TextButton clearPadButton{"Clear pad"}, loadButton{"Load WAV"};
    juce::TextButton playSequenceButton{"Play"}, clearSequenceButton{"Clear pattern"}, exportSequenceButton{"Export MIDI"};
    juce::TextEditor tempoEditor, metadataBeat, metadataValue;
    juce::ComboBox metadataType;
    juce::TextButton setMetadataButton{"Set event"}, removeMetadataButton{"Remove event"};
    juce::Label metadataSummary;
    std::vector<MidiMetaEvent> sequenceMetadata;
    MidiPattern importedMidiBaseline;
    bool hasImportedMidiBaseline = false;
    juce::TextButton importMidiButton{"Import MIDI"}, browseMidiButton{"Find drum MIDI"}, reviewMidiButton{"Review changes"};
    std::array<juce::Label, 8> laneLabels;
    std::array<std::array<MagDrumStepButton, 512>, 8> steps;
    std::array<std::array<int, 512>, 8> stepVelocities{};
    juce::ComboBox barSelect, meterSelect, lengthSelect;
    MidiMappingProfile mappingProfile = MidiMappingProfile::generalMidi();
    std::array<int, 16> mappedPadNotes{49,57,53,51,50,47,45,43,37,39,42,46,36,38,44,56};
    std::array<int, 8> mappedLaneNotes{36,38,42,46,43,51,49,39};
    juce::ComboBox profileSelect, mappingInstrument, mappingArticulation;
    juce::TextEditor profileName, mappingNote;
    juce::TextButton loadProfileButton{"Load profile"}, saveProfileButton{"Save profile"}, importCsvButton{"Import CSV"}, exportCsvButton{"Export CSV"}, setMappingButton{"Set mapping"}, removeMappingButton{"Remove mapping"};
    juce::Label mappingSummary;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> comparisonWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)
};
