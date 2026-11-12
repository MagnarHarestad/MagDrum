#pragma once
#include "PluginProcessor.h"
#include "../core/SampleDatabase.h"
#include "../ui/MagDrumLookAndFeel.h"

class MagDrumSampleMap;
class MagDrumWaveform;

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
    juce::File selectedSample() const;

    magdrum::ui::LookAndFeel theme;
    MagDrumAudioProcessor& processor;
    SampleDatabase database;
    juce::Array<SampleBrowseRecord> samples;
    int page = 0, selectedNote = 36, sequenceStep = 0, sequenceTempo = 120;
    bool sequencerPlaying = false, databaseReady = false;
    juce::TextButton libraryTab{"SAMPLE LIBRARY"}, machineTab{"DRUM MACHINE"}, sequencerTab{"SEQUENCER"};
    juce::TextEditor sampleSearch;
    juce::ComboBox categoryFilter;
    juce::TextButton searchButton{"Search"}, previewButton{"Preview"}, assignButton{"Assign to pad"};
    juce::ListBox indexedList;
    std::unique_ptr<MagDrumSampleMap> sampleMap;
    std::unique_ptr<MagDrumWaveform> waveform;
    std::array<juce::TextButton, 16> pads;
    juce::Label selectedPadTitle, layerSummary, sampleDetails, status;
    juce::TextEditor velocityRange;
    juce::TextButton clearPadButton{"Clear pad"}, loadButton{"Load WAV"};
    juce::TextButton playSequenceButton{"Play"}, clearSequenceButton{"Clear pattern"}, exportSequenceButton{"Export MIDI"};
    juce::TextEditor tempoEditor;
    std::array<juce::Label, 8> laneLabels;
    std::array<std::array<juce::TextButton, 16>, 8> steps;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)
};
