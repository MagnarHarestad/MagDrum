#pragma once
#include <JuceHeader.h>
class MagDrumAudioProcessor final : public juce::AudioProcessor {
public:
 MagDrumAudioProcessor(); void prepareToPlay(double,int) override; void releaseResources() override {}
 bool isBusesLayoutSupported(const BusesLayout&) const override;
 void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
 juce::AudioProcessorEditor* createEditor() override; bool hasEditor() const override{return true;}
 const juce::String getName()const override{return "MagDrum";} bool acceptsMidi()const override{return true;} bool producesMidi()const override{return false;} bool isMidiEffect()const override{return false;}
 double getTailLengthSeconds()const override{return 1.0;} int getNumPrograms()override{return 1;} int getCurrentProgram()override{return 0;} void setCurrentProgram(int)override{}
 const juce::String getProgramName(int)override{return{};} void changeProgramName(int,const juce::String&)override{} void getStateInformation(juce::MemoryBlock&)override{} void setStateInformation(const void*,int)override{}
private:
 struct Voice{double phase=0,frequency=0;float envelope=0;int type=0;}; double sampleRate=44100; std::array<Voice,16> voices{};
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagDrumAudioProcessor)
};
