#pragma once
#include "PluginProcessor.h"

class MagDrumAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                          private juce::Button::Listener
{
public:
    explicit MagDrumAudioProcessorEditor(MagDrumAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked(juce::Button*) override;
    MagDrumAudioProcessor& processor;
    juce::ComboBox noteSelect;
    juce::TextButton loadButton{"Load WAV for note"};
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)
};
