#pragma once
#include "PluginProcessor.h"
#include "../core/SampleDatabase.h"
#include "../core/MidiPatternIO.h"
#include "../core/SequencerGrid.h"
#include "../core/WavScanner.h"
#include "../ui/MagDrumLookAndFeel.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
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

class MagDrumPadActivityTimer final : private juce::Timer
{
public:
    std::function<void()> onTick;
    void start() { startTimerHz(30); }
    void stop() { stopTimer(); }

private:
    void timerCallback() override { if (onTick) onTick(); }
};

class MagDrumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener,
                                          private juce::ListBoxModel,
                                          private juce::ChangeListener,
                                          private juce::Timer
{
public:
    explicit MagDrumAudioProcessorEditor(MagDrumAudioProcessor&, juce::File databaseFileOverride = {});
    ~MagDrumAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;

private:
    friend class MagDrumAudioProcessorEditorTestAccess;
    void dispatchSampleMapDoubleClickForTest(juce::Point<float> position);
    void dispatchSampleMapMoveForTest(juce::Point<float> position);
    void dispatchSampleMapExitForTest();
    void dispatchSampleMapWheelForTest(juce::Point<float> position, float deltaY);
    void dispatchSampleMapMiddleDragForTest(juce::Point<float> start, juce::Point<float> end);
    juce::Point<float> sampleMapPointForTest(int sampleIndex) const;
    std::array<float, 3> sampleMapViewportForTest() const;
    void buttonClicked(juce::Button*) override;
    void timerCallback() override;
    void updatePadActivity();
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void updateLayerSummary();
    void manageSampleLayers();
    void handleSampleLayerMenuAction(int note, const MagDrumAudioProcessor::SampleLayerInfo&, int action);
    void editSampleLayerVelocityRange(const MagDrumAudioProcessor::SampleLayerInfo&);
    void refreshSamples();
    void selectSampleMapSample(int index);
    void refreshSampleAssignmentLabels();
    void previewSampleAsync(const juce::File&);
    void stopSamplePreview();
    void loadSampleAsync(const juce::File&, int note, int minimumVelocity, int maximumVelocity,
                         bool fromLibrary);
    void setPage(int);
    void setNote(int);
    void beginPadParameterGesture(size_t parameterIndex);
    void endPadParameterGesture(size_t parameterIndex);
    void updateDrumNotes();
    int patternStepCount() const;
    magdrum::sequencer::BarMap currentBarMap() const;
    int sequenceTempoAtBeat(double beat) const;
    void refreshSequenceMetadata();
    bool editSequenceMetadata(bool remove);
    MidiPattern currentSequencePattern() const;
    void reviewMidiChanges();
    void syncSequenceEventsFromGrid();
    bool syncSequenceEventForCell(int lane, int step);
    void syncSequencerGridFromEvents();
    void openMidiEventEditor();
    void openSampleComparisonWindow(const juce::File&, const juce::File&);
    int timeSignatureNumerator() const;
    int timeSignatureDenominator() const;
    int gridResolutionSubdivisions() const;
    void updateSequencerGrid();
    int visibleStepIndex(int localStep) const;
    void refreshStepTooltips();
    void refreshMappingSummary();
    void savePluginEditorState();
    void restorePluginEditorState();
    juce::String currentWaveformTooltip() const;
    void importMidiPattern();
    void loadMidiPatternFile(const juce::File&);
    void searchDefaultMidiPatterns();
    void finishDefaultMidiPatternSearch(std::vector<juce::File>, bool cancelled);
    void browseDefaultMidiPatterns();
    void chooseMappingCsv(bool exportCsv);
    void chooseSampleFolder(bool chooseDifferentFolder = false);
    void scanDefaultLibraryFolders();
    void launchInstrumentScanner();
    void openSourceKitForSelectedSample();
    void startNextDefaultLibraryFolder();
    void startSampleScan(const juce::File&);
    void updateSampleScanProgress(const juce::File&, int completed, int total);
    void finishSampleScan(const WavScanner::Stats&, std::int64_t, const juce::String&);
    void loadSamplePage(bool reset, std::uint64_t requestGeneration = 0);
    void refreshSourceKitFilters();
    void refreshPluginInventory();
    void refreshPluginKits();
    void choosePluginFolder();
    void startPluginInventoryScan(const juce::File& additionalFolder = {});
    void pollPluginInventoryProgress();
    void finishPluginInventoryScan(int exitCode, bool cancelled, int skippedAfterCrash);
    void choosePluginPreset();
    void startPluginPresetImport();
    void startPluginProgramDiscovery();
    void finishPluginProgramDiscovery(int exitCode, bool cancelled, juce::StringArray programs,
                                      const juce::String& message);
    void startPluginProgramImport();
    void finishPluginPresetImport(int exitCode, bool cancelled, juce::MemoryBlock state,
                                  const juce::String& message, const juce::String& pluginIdentifier,
                                  const juce::String& kitName);
    void startPluginNoteScan();
    void cancelPluginNoteScan();
    void updatePluginScanProgress(int completed, int total, int note, int velocity);
    void finishPluginNoteScan(int saved, int sounding, int silent, int failures, bool cancelled, const juce::String& error, const juce::String& summary);
    void showLibraryTools();
    void handleLibraryTool(int);
    void finishSampleLookup(int, bool, juce::Array<juce::File>,
                            std::vector<PerceptualDuplicateMatch>);
    void finishSampleCandidateReview(int, bool, SampleBrowseRecord,
                                     std::vector<PluginVelocityLayerBoundary>,
                                     std::vector<PluginConfirmedVelocityLayerRange>,
                                     std::vector<PluginRoundRobinCandidate>,
                                     std::vector<PluginConfirmedRoundRobinGroup>);
    void finishPortableMetadataArchive(bool, bool, bool, bool, const juce::File&,
                                       const PortableMetadataMergeResult&);
    void reviewVelocityLayers(const SampleBrowseRecord&,
                              const std::vector<PluginVelocityLayerBoundary>&,
                              const std::vector<PluginConfirmedVelocityLayerRange>&);
    void reviewRoundRobinGroups(const SampleBrowseRecord&,
                                const std::vector<PluginRoundRobinCandidate>&,
                                const std::vector<PluginConfirmedRoundRobinGroup>&);
    void finishMetadataFolderSync(const PortableMetadataSyncResult&);
    void applyLanguage();
    juce::File selectedSample() const;

    magdrum::ui::LookAndFeel theme;
    juce::TooltipWindow tooltipWindow;
    MagDrumPadActivityTimer padActivityTimer;
    MagDrumAudioProcessor& processor;
    SampleDatabase database;
    juce::File databaseFile;
    juce::Array<SampleBrowseRecord> samples;
    juce::Array<SampleBrowseRecord> sampleMapSamples;
    juce::File sampleMapSelectedFile;
    std::unordered_map<std::string, juce::String> sampleAssignmentLabels;
    int page = 0, selectedNote = 36, sequenceStep = 0, sequenceTempo = 120;
    int sampleOffset = 0, previousMeterId = 404, previousPatternBars = 16;
    bool sequencerPlaying = false, databaseReady = false, scanning = false, hasMoreSamples = false;
    bool sampleBrowseQueryPending = false;
    std::uint64_t sampleBrowseGeneration = 0;
    juce::ThreadPool sampleBrowseWorkers{1};
    std::uint64_t sampleMapGeneration = 0;
    bool sampleMapQueryPending = false;
    juce::ThreadPool sampleMapWorkers{1};
    std::atomic<std::uint64_t> samplePreviewGeneration{0};
    juce::ThreadPool samplePreviewWorkers{1};
    std::atomic<bool> sampleLoadCancellation{false};
    juce::ThreadPool sampleLoadWorkers{1};
    juce::ThreadPool sampleLookupWorkers{1};
    bool sampleLookupRunning = false;
    juce::ThreadPool sampleReviewWorkers{1};
    bool sampleReviewRunning = false;
    juce::ThreadPool metadataArchiveWorkers{1};
    bool metadataArchiveRunning = false;
    bool scanningDefaultLibraries = false;
    std::vector<juce::File> defaultLibraryFolders;
    std::vector<juce::File> defaultMidiPatterns;
    std::shared_ptr<std::atomic<bool>> defaultMidiSearchCancellation;
    size_t defaultLibraryFolderIndex = 0;
    std::int64_t defaultLibraryIndexedCount = 0;
    std::int64_t defaultLibraryErrorCount = 0;
    std::int64_t defaultLibraryVstSoundCount = 0;
    std::int64_t defaultLibraryKontaktNcwCount = 0;
    juce::File lastSampleScanFolder;
    bool defaultLibraryScanCanResume = false;
    double sampleScanProgressValue = 0.0;
    std::shared_ptr<std::atomic<bool>> scanCancellation;
    juce::ThreadPool scanWorkers{1};
    juce::ThreadPool metadataSyncWorkers{1};
    bool metadataSyncRunning = false;
    juce::ThreadPool pluginScanWorkers{1};
    std::atomic<bool> pluginScanCancellation{false};
    bool pluginScanRunning = false;
    bool pluginInventoryScanRunning = false;
    std::shared_ptr<std::atomic<bool>> pluginInventoryScanCancellation;
    juce::File pluginInventoryProgressFile;
    juce::ThreadPool pluginInventoryWorkers{1};
    juce::Array<juce::PluginDescription> discoveredPluginInstruments;
    juce::TextButton libraryTab{"SAMPLE LIBRARY"}, machineTab{"DRUM MACHINE"}, sequencerTab{"SEQUENCER"};
    juce::TextButton mapsTab{"MIDI MAPS"};
    juce::TextButton pluginScannerTab{"PLUGIN SCANNER"};
    magdrum::ui::MenuIconButton languageButton;
    juce::TextEditor sampleSearch;
    juce::ComboBox categoryFilter, contentTypeFilter, featureFilter, sortFilter, confidenceFilter, sourceFilter, kitFilter;
    juce::TextButton scanButton{"Add sample folder"}, scanDefaultsButton{"Scan installed libraries"}, instrumentScannerButton{"Plugin instruments"}, openSourceKitButton{"Open source kit"}, searchButton{"Search"}, previewButton{"Preview"}, stopPreviewButton{"Stop"}, assignButton{"Assign to pad"}, libraryToolsButton{"Sample tools"}, moreSamplesButton{"More samples"};
    juce::ListBox indexedList;
    std::unique_ptr<MagDrumSampleMap> sampleMap;
    juce::ProgressBar sampleScanProgress{sampleScanProgressValue};
    juce::Label sampleScanProgressLabel;
    std::unique_ptr<MagDrumWaveform> waveform;
    std::array<magdrum::ui::DrumPadButton, 16> pads;
    juce::Label selectedPadTitle, layerSummary, sampleDetails, status;
    juce::Label chokeGroupLabel;
    juce::ComboBox chokeGroupSelect;
    juce::TextButton manageLayersButton{"Layers"};
    juce::Label padMixHeading, padShapeHeading, padEqHeading, padFxHeading;
    juce::Rectangle<int> drumPadTrayBounds, drumEditorPanelBounds, drumWaveformPanelBounds;
    juce::Rectangle<int> padMixPanelBounds, padShapePanelBounds, padEqPanelBounds, padFxPanelBounds;
    juce::Label gainLabel, panLabel, tuneLabel, driveLabel, attackLabel, decayLabel, sustainLabel, releaseLabel, filterCutoffLabel, filterResonanceLabel, eqLowLabel, eqMidLabel, eqHighLabel;
    juce::Label compressorThresholdLabel, compressorRatioLabel, compressorAttackLabel, compressorReleaseLabel, compressorMakeupLabel;
    juce::Label phaserRateLabel, phaserDepthLabel, phaserFeedbackLabel, phaserMixLabel;
    juce::TextButton compressorTab, phaserTab;
    juce::Slider gainSlider, panSlider, tuneSlider, driveSlider, attackSlider, decaySlider, sustainSlider, releaseSlider, filterCutoffSlider, filterResonanceSlider, eqLowSlider, eqMidSlider, eqHighSlider;
    juce::Slider compressorThresholdSlider, compressorRatioSlider, compressorAttackSlider, compressorReleaseSlider, compressorMakeupSlider;
    juce::Slider phaserRateSlider, phaserDepthSlider, phaserFeedbackSlider, phaserMixSlider;
    std::array<int, 24> activePadParameterGestureNotes{};
    bool showPhaserControls = false;
    bool updatingPadControls = false;
    juce::TextEditor velocityRange;
    juce::TextButton clearPadButton{"Clear pad"}, loadButton{"Load sample"};
    juce::TextButton playSequenceButton{"Play"}, clearSequenceButton{"Clear pattern"}, exportSequenceButton{"Export MIDI"};
    juce::ComboBox midiFallbackPolicy;
    juce::TextEditor tempoEditor, metadataBeat, metadataValue;
    juce::ComboBox metadataType;
    juce::TextButton setMetadataButton{"Set event"}, removeMetadataButton{"Remove event"};
    juce::Label metadataSummary;
    juce::Label gridResolutionLabel;
    std::vector<MidiMetaEvent> sequenceMetadata;
    MidiPattern importedMidiBaseline;
    bool hasImportedMidiBaseline = false;
    juce::TextButton importMidiButton{"Import MIDI"}, browseMidiButton{"Find drum MIDI"}, editMidiEventsButton{"Edit hits"}, reviewMidiButton{"Review changes"};
    std::vector<DrumEvent> sequenceEvents;
    bool sequenceEventsInitialised = false;
    MidiPattern midiEventEditingPattern;
    std::array<juce::Label, 16> laneLabels;
    static constexpr size_t stepButtonCount = 32;
    std::array<std::array<MagDrumStepButton, stepButtonCount>, 16> steps;
    std::array<std::array<bool, 512>, 16> sequenceStepActive{};
    std::array<std::array<int, 512>, 16> stepVelocities{};
    juce::ComboBox barSelect, meterSelect, lengthSelect, gridResolutionSelect;
    int previousGridResolution = 4;
    MidiMappingProfile mappingProfile = MidiMappingProfile::generalMidi();
    std::array<int, 16> mappedPadNotes{49,57,53,51,50,47,45,43,37,39,42,46,36,38,44,56};
    std::array<int, 16> mappedLaneNotes{36,38,42,46,43,51,49,39,53,57,50,47,45,37,44,56};
    juce::ComboBox profileSelect, mappingInstrument, mappingArticulation;
    juce::TextEditor profileName, mappingNote;
    juce::TextButton loadProfileButton{"Load profile"}, saveProfileButton{"Save profile"}, importCsvButton{"Import CSV"}, exportCsvButton{"Export CSV"}, setMappingButton{"Set mapping"}, removeMappingButton{"Remove mapping"};
    juce::Label mappingSummary;
    juce::ComboBox pluginInstrumentSelect, pluginKitSelect, pluginRenderMode, pluginProgramSelect;
    juce::TextEditor pluginVelocities, pluginNoteDuration, pluginRelease, pluginCaptureLength, pluginRepeatCount;
    double pluginInventoryProgressValue = 0.0;
    juce::ProgressBar pluginInventoryProgress{pluginInventoryProgressValue};
    juce::TextEditor pluginPresetKitName;
    juce::TextButton refreshPluginInventoryButton{"Refresh instruments"}, scanPluginInventoryButton{"Scan plugin folders"}, choosePluginFolderButton{"Choose VST3 folder"}, openPluginManagerButton{"Plugin manager"}, scanPluginNotesButton{"Scan kit notes"}, cancelPluginNotesButton{"Cancel scan"}, choosePluginPresetButton{"Choose VST3 preset"}, importPluginPresetButton{"Import preset as kit"}, scanPluginProgramsButton{"Scan programs"}, importPluginProgramButton{"Import program as kit"};
    juce::Label pluginVelocityLabel, pluginNoteDurationLabel, pluginReleaseLabel, pluginCaptureLabel, pluginRepeatLabel;
    juce::Label pluginScanProgress, pluginScanInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::DocumentWindow> comparisonWindow;
    std::unique_ptr<juce::DocumentWindow> midiEventWindow;
    std::unique_ptr<juce::FileChooser> pluginPresetChooser;
    std::unique_ptr<juce::FileChooser> pluginFolderChooser;
    juce::File selectedPluginPresetFile;
    std::shared_ptr<std::atomic<bool>> pluginPresetImportCancellation;
    bool pluginPresetImportRunning = false;
    bool pluginProgramScanRunning = false;
    bool pluginPresetImportFromProgram = false;
    juce::String pluginProgramScanIdentifier;
    juce::ThreadPool pluginPresetImportWorkers{1};
    std::shared_ptr<std::atomic<bool>> pluginProgramScanCancellation;
    juce::ThreadPool pluginProgramScanWorkers{1};

    friend class VstPluginNoteScanJob;
    friend class PluginInventoryScanJob;
    friend class PluginPresetImportJob;
    friend class PluginProgramDiscoveryJob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)
};
