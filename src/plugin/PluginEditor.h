#pragma once
#include "PluginProcessor.h"
#include "../core/SampleDatabase.h"

class MagDrumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener,
                                          private juce::ListBoxModel
{
public:
    explicit MagDrumAudioProcessorEditor(MagDrumAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;

private:
    void buttonClicked(juce::Button*) override;
    MagDrumAudioProcessor& processor;
    SampleDatabase database;
    juce::Array<juce::File> indexedSamples;
    juce::TextEditor sampleSearch;
    juce::TextButton searchButton{"Search index"};
    juce::TextButton assignButton{"Assign selected"};
    juce::ListBox indexedList;
    juce::ComboBox noteSelect;
    juce::TextButton loadButton{"Load WAV for note"};
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)
};
