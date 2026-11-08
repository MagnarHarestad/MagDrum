#pragma once
#include "PluginProcessor.h"
class MagDrumAudioProcessorEditor final:public juce::AudioProcessorEditor{public:explicit MagDrumAudioProcessorEditor(MagDrumAudioProcessor&);void paint(juce::Graphics&)override;void resized()override;JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessorEditor)};
