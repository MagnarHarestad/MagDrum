#include "PluginEditor.h"
MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor&p):AudioProcessorEditor(p){setSize(420,220);}
void MagDrumAudioProcessorEditor::paint(juce::Graphics&g){g.fillAll(juce::Colour(0xff101215));g.setColour(juce::Colour(0xffff8a42));g.setFont(32);g.drawText("MagDrum",24,24,360,48,juce::Justification::centredLeft);g.setColour(juce::Colours::white);g.setFont(18);g.drawText("MIDI drum voice test",24,86,360,30,juce::Justification::centredLeft);g.setColour(juce::Colour(0xffaaa49b));g.setFont(14);g.drawText("C2: kick    D2: snare    Other notes: cymbal",24,132,380,24,juce::Justification::centredLeft);g.drawText("Velocity sensitive | VST3",24,164,360,24,juce::Justification::centredLeft);}
void MagDrumAudioProcessorEditor::resized(){}
