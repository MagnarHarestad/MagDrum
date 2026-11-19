#include <JuceHeader.h>
#include "ui/MagDrumLookAndFeel.h"
#include "ui/MagDrumLocalization.h"
#include "ui/PortableMetadataConflictDialog.h"
#include "core/SampleDatabase.h"
#include "core/AudioClassifier.h"
#include "core/WavScanner.h"
#include "core/MidiPatternIO.h"
#include "core/PluginNoteScanner.h"
#include "core/PluginScanProcess.h"
#include "core/PluginPrograms.h"
#include "core/PluginPresetFiles.h"
#include "core/PluginSourceKitRequest.h"
#include "core/PatternAudioRenderer.h"
#include "core/LoopingAudioSource.h"
#include "core/PluginVelocityLayerAnalyzer.h"
#include "core/PluginRoundRobinAnalyzer.h"
#include "core/SampleFeatureFilter.h"
#include "core/SampleClassificationReview.h"
#include "core/StructuredLogger.h"
#include "core/MagDrumPaths.h"
#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <unordered_map>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if JUCE_DEBUG
#include <crtdbg.h>
#endif
#endif

namespace
{
#if JUCE_WINDOWS && JUCE_DEBUG
int terminateDebugWorkerOnAssertion(int reportType, char* message, int* returnValue)
{
 if(reportType!=_CRT_ASSERT)return 0;
 if(message!=nullptr)OutputDebugStringA(message);
 OutputDebugStringA("\nMagDrum isolated VST3 worker stopped after a Debug assertion.\n");
 if(returnValue!=nullptr)*returnValue=0;
 ExitProcess(0xC0000420u); // STATUS_ASSERTION_FAILURE; the parent skips this module and resumes.
}

void makeDebugWorkerAssertionsNonInteractive()
{
 _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL,terminateDebugWorkerOnAssertion);
}
#endif

juce::String describeMidiMapping(const MidiNoteMapping& mapping)
{
 static constexpr const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
 static constexpr const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
 const auto instrument=static_cast<size_t>(mapping.instrument);
 const auto articulation=static_cast<size_t>(mapping.articulation);
 if(instrument>=std::size(instruments)||articulation>=std::size(articulations))return magdrum::ui::localisedText("Unknown mapping");
 return magdrum::ui::localisedText(instruments[instrument])+" / "+magdrum::ui::localisedText(articulations[articulation]);
}

juce::String describeMidiEvent(const DrumEvent& event)
{
 return describeMidiMapping({event.instrument,event.articulation,0})+" — "+magdrum::ui::localisedText("Velocity")+" "+juce::String(event.velocity)
     +", "+magdrum::ui::localisedText("Beat")+" "+juce::String(event.positionBeats,3)+", "+magdrum::ui::localisedText("Length in beats")+" "+juce::String(event.durationBeats,3);
}

juce::String describeMidiMetadata(const MidiMetaEvent& meta)
{
 if(meta.message.isTempoMetaEvent())
  return magdrum::ui::localisedText("Tempo {tempo} BPM at beat {beat}")
      .replace("{tempo}",juce::String(static_cast<int>(std::round(60.0/meta.message.getTempoSecondsPerQuarterNote()))))
      .replace("{beat}",juce::String(meta.positionBeats,3));
 if(meta.message.isTimeSignatureMetaEvent())
 {
  int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);
  return magdrum::ui::localisedText("Time signature {meter} at beat {beat}")
      .replace("{meter}",juce::String(numerator)+"/"+juce::String(denominator))
      .replace("{beat}",juce::String(meta.positionBeats,3));
 }
 return magdrum::ui::localisedText("MIDI metadata at beat {beat}").replace("{beat}",juce::String(meta.positionBeats,3));
}

juce::String describeMidiPatternDiff(const MidiPattern& imported,const MidiPattern& current)
{
 const auto diff=compareMidiPatterns(imported,current);
 const int removed=static_cast<int>(diff.removedEvents.size()+diff.removedMetadata.size());
 const int added=static_cast<int>(diff.addedEvents.size()+diff.addedMetadata.size());
 if(removed==0&&added==0&&!diff.lengthChanged)return magdrum::ui::localisedText("No changes from the imported MIDI pattern.");

 juce::String report=magdrum::ui::localisedText("Imported MIDI — removed from current pattern ({count}):").replace("{count}",juce::String(removed))+"\n";
 for(const auto& event:diff.removedEvents)report+="  "+describeMidiEvent(event)+"\n";
 for(const auto& meta:diff.removedMetadata)report+="  "+describeMidiMetadata(meta)+"\n";
 if(removed==0)report+="  "+magdrum::ui::localisedText("None")+"\n";
 report+="\n"+magdrum::ui::localisedText("Current pattern — added since import ({count}):").replace("{count}",juce::String(added))+"\n";
 for(const auto& event:diff.addedEvents)report+="  "+describeMidiEvent(event)+"\n";
 for(const auto& meta:diff.addedMetadata)report+="  "+describeMidiMetadata(meta)+"\n";
 if(added==0)report+="  "+magdrum::ui::localisedText("None")+"\n";
 if(diff.lengthChanged)report+="\n"+magdrum::ui::localisedText("Pattern length: {old} → {new} beats").replace("{old}",juce::String(imported.lengthBeats,3)).replace("{new}",juce::String(current.lengthBeats,3))+"\n";
 return report;
}
}

class WaveformView final : public juce::Component, private juce::ChangeListener
{
public:
 WaveformView():thumbnail(512,formats,cache){formats.registerBasicFormats();thumbnail.addChangeListener(this);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);setOpaque(true);}
 ~WaveformView()override{thumbnail.removeChangeListener(this);magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);}
 void setSample(const juce::File& file){if(currentFile==file)return;currentFile=file;thumbnail.setSource(file.existsAsFile()?new juce::FileInputSource(file):nullptr);repaint();}
 void paint(juce::Graphics& g)override{g.fillAll(magdrum::ui::Palette::Panel);auto area=getLocalBounds().reduced(8);g.setColour(magdrum::ui::Palette::Selection);g.drawRoundedRectangle(area.toFloat(),4.0f,1.0f);if(thumbnail.getTotalLength()>0.0){g.setColour(magdrum::ui::Palette::Accent);thumbnail.drawChannels(g,area.reduced(5),0.0,thumbnail.getTotalLength(),0.9f);}else{g.setColour(magdrum::ui::Palette::Muted);g.setFont(12.0f);g.drawText(magdrum::ui::localisedText("Select a sample to load its waveform"),area,juce::Justification::centred);}}
private:
 void changeListenerCallback(juce::ChangeBroadcaster*)override{repaint();}
 juce::AudioFormatManager formats;juce::AudioThumbnailCache cache{8};juce::AudioThumbnail thumbnail;juce::File currentFile;
};

class MidiPatternEditor final : public juce::Component, private juce::ListBoxModel, private juce::Button::Listener, private juce::ChangeListener
{
public:
 explicit MidiPatternEditor(MidiPattern& value,const MidiPattern& importedValue,bool hasImportedValue):pattern(value),importedPattern(importedValue),hasImportedPattern(hasImportedValue),events("Drum events",this)
 {
  events.setRowHeight(26);addAndMakeVisible(events);
  const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
  for(int i=0;i<9;++i)instrument.addItem(instruments[i],i+1);
  const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
  for(int i=0;i<13;++i)articulation.addItem(articulations[i],i+1);
  addAndMakeVisible(instrument);addAndMakeVisible(articulation);
  velocity.setInputRestrictions(3,"0123456789");beat.setInputRestrictions(12,"0123456789.");duration.setInputRestrictions(12,"0123456789.");
  velocity.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity"),juce::Colours::grey);beat.setTextToShowWhenEmpty(magdrum::ui::localisedText("Beat position"),juce::Colours::grey);duration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Duration beats"),juce::Colours::grey);
  addAndMakeVisible(velocity);addAndMakeVisible(beat);addAndMakeVisible(duration);
  apply.setButtonText(magdrum::ui::localisedText("Apply edit"));remove.setButtonText(magdrum::ui::localisedText("Delete event"));apply.addListener(this);remove.addListener(this);addAndMakeVisible(apply);addAndMakeVisible(remove);
  metadataSummary.setMultiLine(true);metadataSummary.setReadOnly(true);metadataSummary.setScrollbarsShown(true);addAndMakeVisible(metadataSummary);
  metadataBeatLabel.setText(magdrum::ui::localisedText("At beat"),juce::dontSendNotification);tempoLabel.setText(magdrum::ui::localisedText("BPM"),juce::dontSendNotification);timeSignatureLabel.setText(magdrum::ui::localisedText("Meter"),juce::dontSendNotification);
  for(auto* label:{&metadataBeatLabel,&tempoLabel,&timeSignatureLabel})label->setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);
  metadataBeat.setInputRestrictions(12,"0123456789.");metadataBeat.setText(magdrum::ui::localisedText("0"),juce::dontSendNotification);
  metadataType.addItem(magdrum::ui::localisedText("Tempo"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(1,juce::dontSendNotification);
  tempo.setInputRestrictions(3,"0123456789");timeSignature.setInputRestrictions(5,"0123456789/");tempo.setText(magdrum::ui::localisedText("120"),juce::dontSendNotification);timeSignature.setText(magdrum::ui::localisedText("4/4"),juce::dontSendNotification);
  addAndMakeVisible(metadataBeatLabel);addAndMakeVisible(metadataBeat);addAndMakeVisible(metadataType);addAndMakeVisible(tempoLabel);addAndMakeVisible(tempo);addAndMakeVisible(timeSignatureLabel);addAndMakeVisible(timeSignature);
  saveMeta.setButtonText(magdrum::ui::localisedText("Set at beat"));removeMeta.setButtonText(magdrum::ui::localisedText("Remove at beat"));saveMeta.addListener(this);removeMeta.addListener(this);addAndMakeVisible(saveMeta);addAndMakeVisible(removeMeta);
  compare.setButtonText(magdrum::ui::localisedText("Compare with imported"));compare.addListener(this);compare.setEnabled(hasImportedPattern);addAndMakeVisible(compare);
  bool tempoAtStartFound=false,signatureAtStartFound=false;for(const auto& meta:pattern.tempoAndSignature)if(meta.positionBeats==0.0){if(meta.message.isTempoMetaEvent()&&!tempoAtStartFound){tempo.setText(juce::String(static_cast<int>(std::round(60.0/meta.message.getTempoSecondsPerQuarterNote()))),juce::dontSendNotification);tempoAtStartFound=true;}if(meta.message.isTimeSignatureMetaEvent()&&!signatureAtStartFound){int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);timeSignature.setText(juce::String(numerator)+"/"+juce::String(denominator),juce::dontSendNotification);signatureAtStartFound=true;}}
  addAndMakeVisible(message);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);applyLanguage();refreshMetadataSummary();
 }
 ~MidiPatternEditor()override{magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);}
 void changeListenerCallback(juce::ChangeBroadcaster*)override{applyLanguage();events.updateContent();refreshMetadataSummary();}
 void resized()override
 {
  auto area=getLocalBounds().reduced(12);events.setBounds(area.removeFromTop(155));area.removeFromTop(5);
  auto row=area.removeFromTop(32);instrument.setBounds(row.removeFromLeft(125));row.removeFromLeft(5);articulation.setBounds(row.removeFromLeft(125));row.removeFromLeft(5);velocity.setBounds(row.removeFromLeft(80));row.removeFromLeft(5);beat.setBounds(row.removeFromLeft(130));row.removeFromLeft(5);duration.setBounds(row.removeFromLeft(130));row.removeFromLeft(5);apply.setBounds(row.removeFromLeft(100));row.removeFromLeft(5);remove.setBounds(row.removeFromLeft(105));
  area.removeFromTop(5);metadataSummary.setBounds(area.removeFromTop(62));area.removeFromTop(5);
  row=area.removeFromTop(32);metadataBeatLabel.setBounds(row.removeFromLeft(50));metadataBeat.setBounds(row.removeFromLeft(72));row.removeFromLeft(5);metadataType.setBounds(row.removeFromLeft(120));row.removeFromLeft(8);tempoLabel.setBounds(row.removeFromLeft(38));tempo.setBounds(row.removeFromLeft(52));row.removeFromLeft(8);timeSignatureLabel.setBounds(row.removeFromLeft(48));timeSignature.setBounds(row.removeFromLeft(56));row.removeFromLeft(8);saveMeta.setBounds(row.removeFromLeft(100));row.removeFromLeft(5);removeMeta.setBounds(row.removeFromLeft(112));
  row=area.removeFromTop(30);message.setBounds(row.removeFromLeft(610));row.removeFromLeft(6);compare.setBounds(row);
 }
private:
 void applyLanguage(){
  const int instrumentId=instrument.getSelectedId(),articulationId=articulation.getSelectedId(),metadataTypeId=metadataType.getSelectedId();
  const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
  const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
  instrument.clear(juce::dontSendNotification);for(int i=0;i<9;++i)instrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);instrument.setSelectedId(instrumentId,juce::dontSendNotification);
  articulation.clear(juce::dontSendNotification);for(int i=0;i<13;++i)articulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);articulation.setSelectedId(articulationId,juce::dontSendNotification);
  metadataType.clear(juce::dontSendNotification);metadataType.addItem(magdrum::ui::localisedText("Tempo"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(metadataTypeId>0?metadataTypeId:1,juce::dontSendNotification);
  events.setName(magdrum::ui::localisedText("Drum events"));velocity.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity"),juce::Colours::grey);beat.setTextToShowWhenEmpty(magdrum::ui::localisedText("Beat position"),juce::Colours::grey);duration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Duration beats"),juce::Colours::grey);
  apply.setButtonText(magdrum::ui::localisedText("Apply edit"));remove.setButtonText(magdrum::ui::localisedText("Delete event"));metadataBeatLabel.setText(magdrum::ui::localisedText("At beat"),juce::dontSendNotification);tempoLabel.setText(magdrum::ui::localisedText("BPM"),juce::dontSendNotification);timeSignatureLabel.setText(magdrum::ui::localisedText("Meter"),juce::dontSendNotification);saveMeta.setButtonText(magdrum::ui::localisedText("Set at beat"));removeMeta.setButtonText(magdrum::ui::localisedText("Remove at beat"));compare.setButtonText(magdrum::ui::localisedText("Compare with imported"));message.setText(magdrum::ui::localisedText("Edit drum events or change tempo and meter at any beat."),juce::dontSendNotification);
 }
 int getNumRows()override{return static_cast<int>(pattern.events.size());}
 void paintListBoxItem(int row,juce::Graphics& g,int width,int height,bool selected)override
 {
  if(!juce::isPositiveAndBelow(row,pattern.events.size()))return;
  const auto& event=pattern.events[static_cast<size_t>(row)];
  if(selected)g.fillAll(magdrum::ui::Palette::Selection);
  const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
  const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
  const auto instrumentIndex=static_cast<size_t>(event.instrument);const auto articulationIndex=static_cast<size_t>(event.articulation);
  const auto instrumentName=instrumentIndex<9?instruments[instrumentIndex]:"Unknown";const auto articulationName=articulationIndex<13?articulations[articulationIndex]:"Unknown";
  g.setColour(magdrum::ui::Palette::Text);g.drawText(juce::String(row+1)+".  "+magdrum::ui::localisedText(instrumentName)+" / "+magdrum::ui::localisedText(articulationName)+"     "+magdrum::ui::localisedText("Velocity")+" "+juce::String(event.velocity)+"     "+magdrum::ui::localisedText("Beat")+" "+juce::String(event.positionBeats,3)+"     "+magdrum::ui::localisedText("Length in beats")+" "+juce::String(event.durationBeats,3),8,0,width-16,height,juce::Justification::centredLeft);
 }
 void selectedRowsChanged(int row)override
 {
  if(!juce::isPositiveAndBelow(row,pattern.events.size()))return;
  const auto& event=pattern.events[static_cast<size_t>(row)];instrument.setSelectedId(static_cast<int>(event.instrument)+1);articulation.setSelectedId(static_cast<int>(event.articulation)+1);velocity.setText(juce::String(event.velocity),juce::dontSendNotification);beat.setText(juce::String(event.positionBeats,6),juce::dontSendNotification);duration.setText(juce::String(event.durationBeats,6),juce::dontSendNotification);
 }
 void buttonClicked(juce::Button* button)override
 {
   if(button==&compare){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("MIDI pattern diff"),describeMidiPatternDiff(importedPattern,pattern),magdrum::ui::localisedText("Close"));return;}
  if(button==&saveMeta||button==&removeMeta){const double position=metadataBeat.getText().getDoubleValue();if(!std::isfinite(position)||position<0.0){message.setText(magdrum::ui::localisedText("Enter a non-negative beat position."),juce::dontSendNotification);return;}const bool editingTempo=metadataType.getSelectedId()==1;if(button==&removeMeta){const bool removed=editingTempo?removePatternTempo(pattern,position):removePatternTimeSignature(pattern,position);message.setText(removed?magdrum::ui::localisedText("Metadata removed at beat {beat}.").replace("{beat}",juce::String(position,3)):magdrum::ui::localisedText("No matching metadata exists at that beat."),juce::dontSendNotification);}else if(editingTempo){const int bpm=tempo.getText().getIntValue();if(!setPatternTempo(pattern,bpm,position)){message.setText(magdrum::ui::localisedText("Use a tempo from 20 to 300 BPM and a non-negative beat."),juce::dontSendNotification);return;}message.setText(magdrum::ui::localisedText("Tempo set to {tempo} BPM at beat {beat}.").replace("{tempo}",juce::String(bpm)).replace("{beat}",juce::String(position,3)),juce::dontSendNotification);}else{const auto text=timeSignature.getText().trim();const int slash=text.indexOfChar('/');if(slash<=0){message.setText(magdrum::ui::localisedText("Use a time signature such as 4/4."),juce::dontSendNotification);return;}const int numerator=text.substring(0,slash).getIntValue(),denominator=text.substring(slash+1).getIntValue();if(!setPatternTimeSignature(pattern,numerator,denominator,position)){message.setText(magdrum::ui::localisedText("Use a numerator 1–32 and denominator 1, 2, 4, 8 or 16."),juce::dontSendNotification);return;}message.setText(magdrum::ui::localisedText("Time signature set to {meter} at beat {beat}.").replace("{meter}",juce::String(numerator)+"/"+juce::String(denominator)).replace("{beat}",juce::String(position,3)),juce::dontSendNotification);}refreshMetadataSummary();return;}
  const int row=events.getSelectedRow();if(!juce::isPositiveAndBelow(row,pattern.events.size())){message.setText(magdrum::ui::localisedText("Select a drum event first."),juce::dontSendNotification);return;}
  if(button==&remove){pattern.events.erase(pattern.events.begin()+row);events.updateContent();events.selectRow(juce::jmin(row,static_cast<int>(pattern.events.size())-1));message.setText(magdrum::ui::localisedText("Event deleted."),juce::dontSendNotification);return;}
  const int newVelocity=velocity.getText().getIntValue();const double newBeat=beat.getText().getDoubleValue(),newDuration=duration.getText().getDoubleValue();
  if(instrument.getSelectedId()<1||instrument.getSelectedId()>9||articulation.getSelectedId()<1||articulation.getSelectedId()>13||newVelocity<1||newVelocity>127||newBeat<0.0||newDuration<=0.0||!std::isfinite(newBeat)||!std::isfinite(newDuration)){message.setText(magdrum::ui::localisedText("Use a valid instrument, articulation, velocity 1–127, beat position and positive duration."),juce::dontSendNotification);return;}
  auto& event=pattern.events[static_cast<size_t>(row)];event.instrument=static_cast<DrumInstrument>(instrument.getSelectedId()-1);event.articulation=static_cast<DrumArticulation>(articulation.getSelectedId()-1);event.velocity=static_cast<std::uint8_t>(newVelocity);event.positionBeats=newBeat;event.durationBeats=newDuration;
  std::stable_sort(pattern.events.begin(),pattern.events.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});events.updateContent();events.selectRow(row);message.setText(magdrum::ui::localisedText("Event updated and pattern resorted by beat position."),juce::dontSendNotification);
 }
 void refreshMetadataSummary(){juce::String summary;for(const auto& meta:pattern.tempoAndSignature)if(meta.message.isTempoMetaEvent()||meta.message.isTimeSignatureMetaEvent())summary+=describeMidiMetadata(meta)+"\n";if(summary.isEmpty())summary=magdrum::ui::localisedText("No tempo or time-signature events.");metadataSummary.setText(summary,juce::dontSendNotification);}
 MidiPattern& pattern;const MidiPattern& importedPattern;bool hasImportedPattern=false;juce::ListBox events;juce::ComboBox instrument,articulation,metadataType;juce::TextEditor velocity,beat,duration,metadataBeat,tempo,timeSignature,metadataSummary;juce::TextButton apply,remove,saveMeta,removeMeta,compare;juce::Label message,metadataBeatLabel,tempoLabel,timeSignatureLabel;
};

class StepCellButton final : public juce::TextButton
{
public:
 void mouseDown(const juce::MouseEvent& event)override
 {
  if(!event.mods.isRightButtonDown()){juce::TextButton::mouseDown(event);return;}
  juce::PopupMenu menu;for(int first=1;first<=127;first+=32){juce::PopupMenu range;const int last=juce::jmin(first+31,127);for(int value=first;value<=last;++value)range.addItem(value,juce::String(value));menu.addSubMenu(juce::String(first)+"–"+juce::String(last),range);}
  juce::Component::SafePointer<StepCellButton> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),[safe](int value){if(safe!=nullptr&&value>=1&&value<=127){safe->stepVelocity=value;safe->customVelocity=true;safe->updateCellText();}});
 }
 void updateCellText(){setButtonText(getToggleState()?juce::String(stepVelocity):juce::String());}
 int stepVelocity=100;bool customVelocity=false;
};

class DrumStepSequencer final : public juce::Component, private juce::ChangeListener
{
public:
 explicit DrumStepSequencer(MidiPattern& value,std::function<void()> previewCallback,std::function<void(bool)> loopCallback,bool loopEnabled):pattern(value),onPreview(std::move(previewCallback)),onLoopChange(std::move(loopCallback))
 {
  setSize(1160,390);
  const char* names[]{"Kick","Snare","Closed hat","Open hat","Clap","Tom","Ride","Crash"};
  for(int row=0;row<8;++row){labels[static_cast<size_t>(row)].setText(names[row],juce::dontSendNotification);labels[static_cast<size_t>(row)].setColour(juce::Label::textColourId,magdrum::ui::Palette::forDrumCategory(juce::String(names[row])));addAndMakeVisible(labels[static_cast<size_t>(row)]);for(int step=0;step<16;++step){auto& button=steps[static_cast<size_t>(row)][static_cast<size_t>(step)];button.setClickingTogglesState(true);button.setColour(juce::TextButton::buttonOnColourId,magdrum::ui::Palette::forDrumCategory(juce::String(names[row])));button.onClick=[this,row,step]{auto& cell=steps[static_cast<size_t>(row)][static_cast<size_t>(step)];if(!cell.customVelocity){const int defaultVelocity=velocity.getText().getIntValue();if(defaultVelocity>=1&&defaultVelocity<=127)cell.stepVelocity=defaultVelocity;}cell.updateCellText();};addAndMakeVisible(button);}}
  velocity.setText(magdrum::ui::localisedText("100"),juce::dontSendNotification);velocity.setInputRestrictions(3,"0123456789");addAndMakeVisible(velocity);velocityLabel.setText(magdrum::ui::localisedText("Velocity"),juce::dontSendNotification);velocityLabel.setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);addAndMakeVisible(velocityLabel);
  append.setButtonText(magdrum::ui::localisedText("Append one bar to pattern"));append.onClick=[this]{appendBar();};addAndMakeVisible(append);
  preview.setButtonText(magdrum::ui::localisedText("Preview pattern"));preview.onClick=[this]{if(onPreview)onPreview();};addAndMakeVisible(preview);
  loopPreview.setButtonText(magdrum::ui::localisedText("Loop preview"));loopPreview.setToggleState(loopEnabled,juce::dontSendNotification);loopPreview.setTooltip(magdrum::ui::localisedText("Repeat the rendered pattern preview until stopped or another sound is played."));loopPreview.onClick=[this]{if(onLoopChange)onLoopChange(loopPreview.getToggleState());};addAndMakeVisible(loopPreview);
   message.setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);addAndMakeVisible(message);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);applyLanguage();updateGridForPattern();
 }
 ~DrumStepSequencer()override{magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);}
 void changeListenerCallback(juce::ChangeBroadcaster*)override{applyLanguage();updateGridForPattern();}
 void resized()override
 {
  auto area=getLocalBounds().reduced(14);auto head=area.removeFromTop(30);head.removeFromLeft(112);for(int step=0;step<16;++step){auto& owned=stepLabels[static_cast<size_t>(step)];if(owned==nullptr){owned=std::make_unique<juce::Label>();owned->setText(juce::String(step+1),juce::dontSendNotification);owned->setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);owned->setJustificationType(juce::Justification::centred);addAndMakeVisible(*owned);}owned->setBounds(head.removeFromLeft(54));}
  area.removeFromTop(4);for(int row=0;row<8;++row){auto line=area.removeFromTop(34);labels[static_cast<size_t>(row)].setBounds(line.removeFromLeft(108));line.removeFromLeft(4);for(int step=0;step<16;++step){steps[static_cast<size_t>(row)][static_cast<size_t>(step)].setBounds(line.removeFromLeft(52));line.removeFromLeft(2);}area.removeFromTop(3);}
  area.removeFromTop(6);auto controls=area.removeFromTop(32);velocityLabel.setBounds(controls.removeFromLeft(62));velocity.setBounds(controls.removeFromLeft(58));controls.removeFromLeft(8);append.setBounds(controls.removeFromLeft(190));controls.removeFromLeft(6);preview.setBounds(controls.removeFromLeft(138));controls.removeFromLeft(6);loopPreview.setBounds(controls.removeFromLeft(110));controls.removeFromLeft(8);message.setBounds(controls);
 }
private:
  void applyLanguage(){
   static constexpr const char* names[]{"Kick","Snare","Closed hi-hat","Open hi-hat","Clap","Tom","Ride","Crash"};
   for(size_t i=0;i<labels.size();++i)labels[i].setText(magdrum::ui::localisedText(names[i]),juce::dontSendNotification);
   velocityLabel.setText(magdrum::ui::localisedText("Velocity"),juce::dontSendNotification);
   append.setButtonText(magdrum::ui::localisedText("Append one bar to pattern"));preview.setButtonText(magdrum::ui::localisedText("Preview pattern"));loopPreview.setButtonText(magdrum::ui::localisedText("Loop preview"));
   loopPreview.setTooltip(magdrum::ui::localisedText("Repeat the rendered pattern preview until stopped or another sound is played."));
  }
 void updateGridForPattern()
 {
  double latestEventBeat=-1.0,latestMetaBeat=-1.0;for(const auto& event:pattern.events)latestEventBeat=juce::jmax(latestEventBeat,event.positionBeats);for(const auto& meta:pattern.tempoAndSignature)latestMetaBeat=juce::jmax(latestMetaBeat,meta.positionBeats);
  const double timelineEnd=juce::jmax(0.0,juce::jmax(pattern.lengthBeats,juce::jmax(latestEventBeat,latestMetaBeat)));int numerator=4,denominator=4;double signatureBeat=0.0;for(const auto& meta:pattern.tempoAndSignature)if(meta.positionBeats<=timelineEnd&&meta.message.isTimeSignatureMetaEvent()){meta.message.getTimeSignatureInfo(numerator,denominator);signatureBeat=meta.positionBeats;}
   if(denominator<1||16%denominator!=0){activeSteps=0;message.setText(magdrum::ui::localisedText("This meter cannot be represented by the current 16-step grid."),juce::dontSendNotification);return;}const int requestedSteps=numerator*(16/denominator);if(requestedSteps<1||requestedSteps>16){activeSteps=0;message.setText(magdrum::ui::localisedText("This meter needs more than 16 steps per bar; the sequencer leaves the pattern unchanged."),juce::dontSendNotification);return;}
  activeSteps=requestedSteps;stepBeats=4.0/static_cast<double>(denominator)/4.0;barLength=static_cast<double>(numerator)*4.0/static_cast<double>(denominator);
  if(pattern.lengthBeats>0.0){const double patternEnd=juce::jmax(pattern.lengthBeats,latestEventBeat);startBeat=signatureBeat+juce::jmax(0.0,std::ceil((patternEnd-signatureBeat)/barLength-1.0e-9))*barLength;}else if(latestEventBeat<0.0)startBeat=latestMetaBeat>=0.0?latestMetaBeat:0.0;else if(signatureBeat>latestEventBeat)startBeat=signatureBeat;else startBeat=signatureBeat+(std::floor((timelineEnd-signatureBeat)/barLength)+1.0)*barLength;
  for(int row=0;row<8;++row)for(int step=0;step<16;++step){auto& button=steps[static_cast<size_t>(row)][static_cast<size_t>(step)];button.setEnabled(step<activeSteps);if(step>=activeSteps)button.setToggleState(false,juce::dontSendNotification);}
   message.setText(magdrum::ui::localisedText("Meter {meter}: {steps} steps. Left-click toggles; right-click sets velocity 1–127.").replace("{meter}",juce::String(numerator)+"/"+juce::String(denominator)).replace("{steps}",juce::String(activeSteps)),juce::dontSendNotification);
 }
 void appendBar()
 {
   updateGridForPattern();if(activeSteps==0)return;const int value=velocity.getText().getIntValue();if(value<1||value>127){message.setText(magdrum::ui::localisedText("Velocity must be between 1 and 127."),juce::dontSendNotification);return;}
  static const DrumInstrument instruments[]{DrumInstrument::Kick,DrumInstrument::Snare,DrumInstrument::HiHat,DrumInstrument::HiHat,DrumInstrument::Clap,DrumInstrument::Tom,DrumInstrument::Ride,DrumInstrument::Crash};
  for(int row=0;row<8;++row)for(int step=0;step<activeSteps;++step){auto& cell=steps[static_cast<size_t>(row)][static_cast<size_t>(step)];if(cell.getToggleState()){DrumEvent event;event.instrument=instruments[row];event.articulation=row==2?DrumArticulation::Closed:row==3?DrumArticulation::Open:DrumArticulation::Default;event.velocity=static_cast<std::uint8_t>(cell.customVelocity?cell.stepVelocity:value);event.positionBeats=startBeat+static_cast<double>(step)*stepBeats;event.durationBeats=row==3?stepBeats*1.2:stepBeats*0.5;pattern.events.push_back(event);}}
   const double appendedAt=startBeat;const int appendedSteps=activeSteps;pattern.lengthBeats=juce::jmax(pattern.lengthBeats,startBeat+static_cast<double>(activeSteps)*stepBeats);std::stable_sort(pattern.events.begin(),pattern.events.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});updateGridForPattern();message.setText(magdrum::ui::localisedText("Added a {steps}-step bar at beat {at}. Next bar begins at beat {next}.").replace("{steps}",juce::String(appendedSteps)).replace("{at}",juce::String(appendedAt,2)).replace("{next}",juce::String(startBeat,2)),juce::dontSendNotification);
 }
 MidiPattern& pattern;std::array<juce::Label,8>labels;std::array<std::array<StepCellButton,16>,8>steps;std::array<std::unique_ptr<juce::Label>,16>stepLabels;juce::TextEditor velocity;juce::Label velocityLabel,message;juce::TextButton append,preview;juce::ToggleButton loopPreview;std::function<void()>onPreview;std::function<void(bool)>onLoopChange;double startBeat=0.0,stepBeats=0.25,barLength=4.0;int activeSteps=16;
};

class MainComponent final : public juce::AudioAppComponent, private juce::Button::Listener, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer {
public:
 explicit MainComponent(bool initialiseAudio=true):rows("Samples",this),audioEnabled(initialiseAudio){
  languageButton.setTooltip(magdrum::ui::localisedText("Menu"));languageButton.onClick=[this]{magdrum::ui::showApplicationMenu(languageButton);};addAndMakeVisible(languageButton);
  setSize(1380,750); title.setText(magdrum::ui::localisedText("MagDrum"),juce::dontSendNotification); title.setFont(juce::Font(30.0f,juce::Font::bold)); title.setColour(juce::Label::textColourId,magdrum::ui::Palette::Text);
  subtitle.setText(magdrum::ui::localisedText("Your drum libraries, in one place."),juce::dontSendNotification); subtitle.setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted); addAndMakeVisible(title);addAndMakeVisible(subtitle);
  scan.setButtonText(magdrum::ui::localisedText("Add sample folder"));scan.addListener(this);addAndMakeVisible(scan);scanHistory.setButtonText(magdrum::ui::localisedText("Scan history"));scanHistory.addListener(this);addAndMakeVisible(scanHistory);pluginScan.setButtonText(magdrum::ui::localisedText("Plugin instruments"));pluginScan.addListener(this);addAndMakeVisible(pluginScan);play.setButtonText(magdrum::ui::localisedText("Play selected"));play.addListener(this);addAndMakeVisible(play);openSourceKit.setButtonText(magdrum::ui::localisedText("Open source kit"));openSourceKit.onClick=[this]{openSelectedSourceKit();};openSourceKit.setEnabled(false);addAndMakeVisible(openSourceKit);
  filter.setTextToShowWhenEmpty(magdrum::ui::localisedText("Filter by filename, folder or tag"),magdrum::ui::Palette::Muted);filter.onTextChange=[this]{startTimer(250);};addAndMakeVisible(filter);
  const char* categories[]{"All categories","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion","Unknown"};for(int i=0;i<13;++i)categoryFilter.addItem(magdrum::ui::localisedText(categories[i]),i+1);categoryFilter.setSelectedId(1);categoryFilter.onChange=[this]{refresh(false);};addAndMakeVisible(categoryFilter);
  sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.addItem(magdrum::ui::localisedText("Sample folders"),2);sourceFilter.setSelectedId(1);sourceFilter.onChange=[this]{refresh(false);};addAndMakeVisible(sourceFilter);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);kitFilter.setSelectedId(1);kitFilter.onChange=[this]{refresh(false);};addAndMakeVisible(kitFilter);stepSequencer.setButtonText(magdrum::ui::localisedText("Step sequencer"));stepSequencer.onClick=[this]{if(stepSequencerWindow==nullptr)stepSequencerWindow=std::make_unique<StepSequencerWindow>(*this);stepSequencerWindow->toFront(true);};addAndMakeVisible(stepSequencer);sortFilter.addItem(magdrum::ui::localisedText("Name A-Z"),1);sortFilter.addItem(magdrum::ui::localisedText("Duration shortest"),2);sortFilter.addItem(magdrum::ui::localisedText("Duration longest"),3);sortFilter.addItem(magdrum::ui::localisedText("Brightest first"),4);sortFilter.addItem(magdrum::ui::localisedText("Darkest first"),5);sortFilter.setSelectedId(1);sortFilter.onChange=[this]{refresh(false);};addAndMakeVisible(sortFilter);characteristicFilter.addItem(magdrum::ui::localisedText("All sound features"),1);characteristicFilter.addItem(magdrum::ui::localisedText("Very short (<100 ms)"),2);characteristicFilter.addItem(magdrum::ui::localisedText("Short (100–500 ms)"),3);characteristicFilter.addItem(magdrum::ui::localisedText("Medium (0.5–2 s)"),4);characteristicFilter.addItem(magdrum::ui::localisedText("Long (≥2 s)"),5);characteristicFilter.addItem(magdrum::ui::localisedText("Dark (<1 kHz)"),6);characteristicFilter.addItem(magdrum::ui::localisedText("Mid (1–3 kHz)"),7);characteristicFilter.addItem(magdrum::ui::localisedText("Bright (≥3 kHz)"),8);characteristicFilter.setSelectedId(1);characteristicFilter.onChange=[this]{refresh(false);};addAndMakeVisible(characteristicFilter);confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review (<75% or unknown)"),2);confidenceFilter.setSelectedId(1);confidenceFilter.onChange=[this]{refresh(false);};addAndMakeVisible(confidenceFilter);
  categoryEdit.addItem(magdrum::ui::localisedText("Unknown"),1);categoryEdit.addItem(magdrum::ui::localisedText("Kick"),2);categoryEdit.addItem(magdrum::ui::localisedText("Snare"),3);categoryEdit.addItem(magdrum::ui::localisedText("Closed hi-hat"),4);categoryEdit.addItem(magdrum::ui::localisedText("Open hi-hat"),5);categoryEdit.addItem(magdrum::ui::localisedText("Pedal hi-hat"),6);categoryEdit.addItem(magdrum::ui::localisedText("Tom"),7);categoryEdit.addItem(magdrum::ui::localisedText("Ride"),8);categoryEdit.addItem(magdrum::ui::localisedText("Crash"),9);categoryEdit.addItem(magdrum::ui::localisedText("Clap"),10);categoryEdit.addItem(magdrum::ui::localisedText("Rimshot"),11);categoryEdit.addItem(magdrum::ui::localisedText("Percussion"),12);categoryEdit.setSelectedId(1);addAndMakeVisible(categoryEdit);
  categorySave.setButtonText(magdrum::ui::localisedText("Set category"));categorySave.addListener(this);addAndMakeVisible(categorySave);tags.setTextToShowWhenEmpty(magdrum::ui::localisedText("Tags, comma separated"),magdrum::ui::Palette::Muted);addAndMakeVisible(tags);tagsSave.setButtonText(magdrum::ui::localisedText("Save tags"));tagsSave.addListener(this);addAndMakeVisible(tagsSave);tagsSave.removeListener(this);tagsSave.onClick=[this]{saveSelectedTags();};similar.setButtonText(magdrum::ui::localisedText("Find similar"));similar.addListener(this);addAndMakeVisible(similar);duplicates.setButtonText(magdrum::ui::localisedText("Find exact duplicates"));duplicates.addListener(this);addAndMakeVisible(duplicates);perceptualDuplicates.setButtonText(magdrum::ui::localisedText("Find near duplicates"));perceptualDuplicates.addListener(this);addAndMakeVisible(perceptualDuplicates);
  midiImport.setButtonText(magdrum::ui::localisedText("Import MIDI"));midiImport.addListener(this);addAndMakeVisible(midiImport);midiExport.setButtonText(magdrum::ui::localisedText("Export MIDI"));midiExport.addListener(this);addAndMakeVisible(midiExport);midiEdit.setButtonText(magdrum::ui::localisedText("Edit pattern"));midiEdit.addListener(this);addAndMakeVisible(midiEdit);midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);midiFallbackPolicy.setSelectedId(1,juce::dontSendNotification);midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));addAndMakeVisible(midiFallbackPolicy);
  profileName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Mapping profile name"),magdrum::ui::Palette::Muted);profileName.setText(magdrum::ui::localisedText("General MIDI"));addAndMakeVisible(profileName);profileSave.setButtonText(magdrum::ui::localisedText("Save profile"));profileSave.addListener(this);addAndMakeVisible(profileSave);profileLoad.setButtonText(magdrum::ui::localisedText("Load profile"));profileLoad.addListener(this);addAndMakeVisible(profileLoad);profileImport.setButtonText(magdrum::ui::localisedText("Import CSV"));profileImport.addListener(this);addAndMakeVisible(profileImport);profileExport.setButtonText(magdrum::ui::localisedText("Export CSV"));profileExport.addListener(this);addAndMakeVisible(profileExport);
  const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<9;++i)mappingInstrument.addItem(instruments[i],i+1);mappingInstrument.setSelectedId(1);addAndMakeVisible(mappingInstrument);
  const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};for(int i=0;i<13;++i)mappingArticulation.addItem(articulations[i],i+1);mappingArticulation.setSelectedId(1);addAndMakeVisible(mappingArticulation);
  mappingNote.setTextToShowWhenEmpty(magdrum::ui::localisedText("MIDI note 0-127"),magdrum::ui::Palette::Muted);mappingNote.setInputRestrictions(3,"0123456789");addAndMakeVisible(mappingNote);mappingSet.setButtonText(magdrum::ui::localisedText("Set mapping"));mappingSet.addListener(this);addAndMakeVisible(mappingSet);mappingRemove.setButtonText(magdrum::ui::localisedText("Remove mapping"));mappingRemove.addListener(this);addAndMakeVisible(mappingRemove);
  status.setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);addAndMakeVisible(status);rows.setRowHeight(48);rows.setMultipleSelectionEnabled(true);addAndMakeVisible(rows);addAndMakeVisible(waveform);compareSamples.setButtonText(magdrum::ui::localisedText("Compare selected"));compareSamples.addListener(this);compareSamples.onClick=[this]{showSampleComparison();};addAndMakeVisible(compareSamples);
  metadataExport.setButtonText(magdrum::ui::localisedText("Export metadata"));metadataExport.onClick=[this]{chooseMetadataExport();};addAndMakeVisible(metadataExport);metadataImport.setButtonText(magdrum::ui::localisedText("Import metadata"));metadataImport.onClick=[this]{chooseMetadataImport();};addAndMakeVisible(metadataImport);
   metadataSync.setButtonText(magdrum::ui::localisedText("Sync metadata"));metadataSync.onClick=[this]{chooseMetadataSyncFolder();};addAndMakeVisible(metadataSync);
  refreshLanguageFilters();formats.registerBasicFormats();juce::addDefaultFormatsToManager(pluginFormats);if(audioEnabled)setAudioChannels(0,2);initDb();loadPluginList();knownPlugins.addChangeListener(this);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);syncPluginInventory();refresh();
 }
  ~MainComponent()override{stopTimer();knownPlugins.removeChangeListener(this);magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);pluginScanCancellation.store(true);sampleScanCancellation.store(true);if(patternPreviewCancellation)patternPreviewCancellation->store(true);comparisonWindow.reset();pluginWindow.reset();midiPatternWindow.reset();stepSequencerWindow.reset();pluginEditorWindow.reset();metadataWorkers.removeAllJobs(true,-1);browseWorkers.removeAllJobs(true,-1);workers.removeAllJobs(true,-1);selectedPlugin.reset();if(audioEnabled)shutdownAudio();transport.setSource(nullptr);source.reset();if(patternPreviewFile.existsAsFile())patternPreviewFile.deleteFile();}
 void openSourceKitRequest(const PluginSourceKitRequest& request){openSourceKitByIdentifier(request);}
 void prepareToPlay(int n,double r)override{transport.prepareToPlay(n,r);} void getNextAudioBlock(const juce::AudioSourceChannelInfo&i)override{transport.getNextAudioBlock(i);} void releaseResources()override{transport.releaseResources();}
 void timerCallback()override{stopTimer();refresh(false);}
 void paint(juce::Graphics&g)override{magdrum::ui::paintTexturedBackground(g,getLocalBounds(),0.42f);g.setColour(magdrum::ui::Palette::Border);g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(20.0f),12.0f,1.0f);g.setColour(magdrum::ui::Palette::Accent);g.fillRoundedRectangle(22,22,5,44,2);}
 void openPluginBrowser();
 void resized()override
 {
  auto a=getLocalBounds().reduced(30,24);auto h=a.removeFromTop(78);title.setBounds(h.removeFromTop(40));subtitle.setBounds(h);languageButton.setBounds(getWidth()-100,24,70,34);a.removeFromTop(8);
  auto c=a.removeFromTop(36);scan.setBounds(c.removeFromLeft(140));c.removeFromLeft(5);scanHistory.setBounds(c.removeFromLeft(90));c.removeFromLeft(5);pluginScan.setBounds(c.removeFromLeft(110));c.removeFromLeft(5);play.setBounds(c.removeFromLeft(105));c.removeFromLeft(5);openSourceKit.setBounds(c.removeFromLeft(140));c.removeFromLeft(5);midiImport.setBounds(c.removeFromLeft(90));c.removeFromLeft(5);midiExport.setBounds(c.removeFromLeft(90));c.removeFromLeft(5);midiEdit.setBounds(c.removeFromLeft(100));c.removeFromLeft(8);filter.setBounds(c.removeFromLeft(190));c.removeFromLeft(8);categoryFilter.setBounds(c.removeFromLeft(140));
  a.removeFromTop(5);c=a.removeFromTop(34);sourceFilter.setBounds(c.removeFromLeft(245));c.removeFromLeft(6);kitFilter.setBounds(c.removeFromLeft(300));c.removeFromLeft(6);stepSequencer.setBounds(c.removeFromLeft(135));c.removeFromLeft(6);sortFilter.setBounds(c.removeFromLeft(155));c.removeFromLeft(6);characteristicFilter.setBounds(c.removeFromLeft(185));c.removeFromLeft(6);confidenceFilter.setBounds(c.removeFromLeft(180));
  a.removeFromTop(5);c=a.removeFromTop(34);categoryEdit.setBounds(c.removeFromLeft(135));c.removeFromLeft(5);categorySave.setBounds(c.removeFromLeft(108));c.removeFromLeft(8);tags.setBounds(c.removeFromLeft(190));c.removeFromLeft(5);tagsSave.setBounds(c.removeFromLeft(82));c.removeFromLeft(5);similar.setBounds(c.removeFromLeft(102));c.removeFromLeft(10);profileName.setBounds(c.removeFromLeft(145));c.removeFromLeft(5);profileSave.setBounds(c.removeFromLeft(95));c.removeFromLeft(5);profileLoad.setBounds(c.removeFromLeft(90));c.removeFromLeft(5);profileImport.setBounds(c.removeFromLeft(90));c.removeFromLeft(5);profileExport.setBounds(c.removeFromLeft(90));
   a.removeFromTop(5);c=a.removeFromTop(34);mappingInstrument.setBounds(c.removeFromLeft(125));c.removeFromLeft(6);mappingArticulation.setBounds(c.removeFromLeft(125));c.removeFromLeft(6);mappingNote.setBounds(c.removeFromLeft(115));c.removeFromLeft(6);mappingSet.setBounds(c.removeFromLeft(110));c.removeFromLeft(6);mappingRemove.setBounds(c.removeFromLeft(125));c.removeFromLeft(8);duplicates.setBounds(c.removeFromLeft(130));c.removeFromLeft(6);perceptualDuplicates.setBounds(c.removeFromLeft(150));c.removeFromLeft(6);compareSamples.setBounds(c.removeFromLeft(130));c.removeFromLeft(6);metadataExport.setBounds(c.removeFromLeft(120));c.removeFromLeft(6);metadataImport.setBounds(c.removeFromLeft(120));c.removeFromLeft(6);metadataSync.setBounds(c.removeFromLeft(120));
  a.removeFromTop(4);auto feedbackRow=a.removeFromTop(24);midiFallbackPolicy.setBounds(feedbackRow.removeFromRight(280));status.setBounds(feedbackRow);a.removeFromTop(4);waveform.setBounds(a.removeFromTop(88));a.removeFromTop(4);rows.setBounds(a);
 }
 int getNumRows()override{return visible.size()+(hasMoreSamples?1:0);}
 void listBoxItemClicked(int row,const juce::MouseEvent&)override{if(hasMoreSamples&&row==visible.size())loadNextSamplePage();}
 void selectedRowsChanged(int lastRowSelected)override{openSourceKit.setEnabled(false);if(juce::isPositiveAndBelow(lastRowSelected,visible.size())){const auto& selected=visible.getReference(lastRowSelected);waveform.setSample(selected);const auto found=browseRecordsByPath.find(selected.getFullPathName().toStdString());openSourceKit.setEnabled(found!=browseRecordsByPath.end()&&found->second.pluginIdentifier.isNotEmpty());}}
void paintListBoxItem(int n,juce::Graphics&g,int w,int h,bool selected)override{if(hasMoreSamples&&n==visible.size()){g.fillAll(magdrum::ui::Palette::Surface);g.setColour(magdrum::ui::Palette::Accent);g.setFont(14);g.drawText(magdrum::ui::localisedText("Load the next %1 matching samples…").replace("%1","250"),12,0,w-24,h,juce::Justification::centred,true);return;}if(!juce::isPositiveAndBelow(n,visible.size()))return;g.fillAll(selected?magdrum::ui::Palette::Selection:magdrum::ui::Palette::Panel);const auto f=visible.getReference(n);const auto found=browseRecordsByPath.find(f.getFullPathName().toStdString());const SampleBrowseRecord* record=found==browseRecordsByPath.end()?nullptr:&found->second;g.setColour(magdrum::ui::Palette::Text);g.setFont(15);g.drawText(f.getFileName(),12,3,w-220,20,juce::Justification::centredLeft,true);const juce::String classificationCategory=record!=nullptr?record->category:"Unknown";juce::String classification=magdrum::ui::localisedText(classificationCategory);if(record!=nullptr){if(record->manuallyClassified)classification+=" - "+magdrum::ui::localisedText("Manually classified");else classification+=" - "+juce::String(static_cast<int>(std::lround(record->classificationConfidence*100.0)))+"%";}g.setColour(magdrum::ui::Palette::forDrumCategory(classificationCategory));g.drawText(classification,w-196,3,184,20,juce::Justification::centredRight,true);juce::String location=f.getParentDirectory().getFullPathName();if(record!=nullptr&&record->pluginIdentifier.isNotEmpty()){location=record->pluginName+" / "+record->kitName+" | MIDI "+juce::String(record->midiNote)+" "+magdrum::ui::localisedText("Velocity")+" "+juce::String(record->velocity);if(record->takeIndex>1)location+=" | "+magdrum::ui::localisedText("Take")+" "+juce::String(record->takeIndex);if(record->articulation.isNotEmpty())location+=" | "+record->articulation;}if(record!=nullptr&&!record->classificationTags.isEmpty())location+=" | "+[&]{juce::StringArray translatedTagList;for(const auto& tagValue:record->classificationTags)translatedTagList.add(magdrum::ui::localisedText(tagValue));return translatedTagList.joinIntoString(", ");}();g.setColour(magdrum::ui::Palette::Muted);g.setFont(12);g.drawText(location,12,23,w-24,h-23,juce::Justification::centredLeft,true);}
private:
 class ComparisonView final:public juce::Component{
 public:ComparisonView(const juce::File& first,const juce::File& second){leftTitle.setText(first.getFileName(),juce::dontSendNotification);rightTitle.setText(second.getFileName(),juce::dontSendNotification);for(auto* label:{&leftTitle,&rightTitle}){label->setColour(juce::Label::textColourId,magdrum::ui::Palette::Text);label->setFont(juce::Font(15.0f,juce::Font::bold));addAndMakeVisible(*label);}left.setSample(first);right.setSample(second);addAndMakeVisible(left);addAndMakeVisible(right);}
  void resized()override{auto area=getLocalBounds().reduced(12);const int half=area.getWidth()/2;auto a=area.removeFromLeft(half).reduced(6);auto b=area.reduced(6);leftTitle.setBounds(a.removeFromTop(28));a.removeFromTop(6);left.setBounds(a);rightTitle.setBounds(b.removeFromTop(28));b.removeFromTop(6);right.setBounds(b);}
 private:juce::Label leftTitle,rightTitle;WaveformView left,right;
 };
 class ComparisonWindow final:public juce::DocumentWindow{
 public:ComparisonWindow(MainComponent& owner,const juce::File& first,const juce::File& second):DocumentWindow(magdrum::ui::localisedText("Compare samples"),magdrum::ui::Palette::Panel,juce::DocumentWindow::allButtons),main(owner){setUsingNativeTitleBar(true);setContentOwned(new ComparisonView(first,second),true);setResizable(true,true);centreWithSize(900,420);setVisible(true);}
  void closeButtonPressed()override{main.comparisonWindow.reset();}
 private:MainComponent&main;
 };
  class PluginBrowser final:public juce::Component,private juce::Button::Listener,private juce::Timer,private juce::ChangeListener{
 public:
   explicit PluginBrowser(MainComponent& owner):main(owner),list(main.pluginFormats,main.knownPlugins,main.pluginCrashList,nullptr){load.addListener(this);programs.onChange=[this]{const int selected=programs.getSelectedId();if(main.selectedPlugin&&selected>0){main.selectedPlugin->setCurrentProgram(selected-1);lastAutomaticKitName=programs.getText();kitName.setText(lastAutomaticKitName,juce::dontSendNotification);}};addAndMakeVisible(load);addAndMakeVisible(programs);addAndMakeVisible(kitName);saveKit.addListener(this);addAndMakeVisible(saveKit);recallKit.addListener(this);addAndMakeVisible(recallKit);findPresets.addListener(this);findPresets.setEnabled(false);addAndMakeVisible(findPresets);choosePresets.addListener(this);choosePresets.setEnabled(false);addAndMakeVisible(choosePresets);velocities.setText("64, 100, 127");velocities.setInputRestrictions(31,"0123456789,; ");addAndMakeVisible(velocities);noteDuration.setText("0.25");noteDuration.setInputRestrictions(8,"0123456789.");addAndMakeVisible(noteDuration);releaseTime.setText("0.5");releaseTime.setInputRestrictions(8,"0123456789.");addAndMakeVisible(releaseTime);captureLength.setText("0.75");captureLength.setInputRestrictions(8,"0123456789.");addAndMakeVisible(captureLength);repeatCount.setText("1");repeatCount.setInputRestrictions(1,"12345678");addAndMakeVisible(repeatCount);addAndMakeVisible(renderMode);scanNotes.addListener(this);addAndMakeVisible(scanNotes);cancelScan.addListener(this);cancelScan.setEnabled(false);addAndMakeVisible(cancelScan);showLayers.addListener(this);showLayers.setEnabled(false);addAndMakeVisible(showLayers);showRoundRobin.addListener(this);showRoundRobin.setEnabled(false);addAndMakeVisible(showRoundRobin);message.setColour(juce::Label::textColourId,magdrum::ui::Palette::Muted);addAndMakeVisible(message);addAndMakeVisible(list);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);applyLanguage();startTimerHz(2);}
   ~PluginBrowser()override{magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);if(presetSearchCancellation)presetSearchCancellation->store(true);}
  void resized()override{auto area=getLocalBounds().reduced(8);auto controls=area.removeFromTop(34);load.setBounds(controls.removeFromLeft(135));controls.removeFromLeft(5);programs.setBounds(controls.removeFromLeft(220));controls.removeFromLeft(5);kitName.setBounds(controls.removeFromLeft(165));controls.removeFromLeft(5);saveKit.setBounds(controls.removeFromLeft(90));controls.removeFromLeft(5);recallKit.setBounds(controls.removeFromLeft(130));controls.removeFromLeft(5);findPresets.setBounds(controls.removeFromLeft(150));controls.removeFromLeft(5);choosePresets.setBounds(controls.removeFromLeft(150));area.removeFromTop(4);controls=area.removeFromTop(34);velocities.setBounds(controls.removeFromLeft(150));controls.removeFromLeft(5);noteDuration.setBounds(controls.removeFromLeft(85));controls.removeFromLeft(5);releaseTime.setBounds(controls.removeFromLeft(85));controls.removeFromLeft(5);captureLength.setBounds(controls.removeFromLeft(90));controls.removeFromLeft(5);repeatCount.setBounds(controls.removeFromLeft(45));controls.removeFromLeft(5);renderMode.setBounds(controls.removeFromLeft(150));area.removeFromTop(4);controls=area.removeFromTop(34);scanNotes.setBounds(controls.removeFromLeft(100));controls.removeFromLeft(5);cancelScan.setBounds(controls.removeFromLeft(70));controls.removeFromLeft(5);showLayers.setBounds(controls.removeFromLeft(150));controls.removeFromLeft(5);showRoundRobin.setBounds(controls.removeFromLeft(190));area.removeFromTop(4);message.setBounds(area.removeFromTop(22));area.removeFromTop(4);list.setBounds(area);}
  void setScanBusy(bool busy){load.setEnabled(!busy);programs.setEnabled(!busy&&programs.getNumItems()>0);saveKit.setEnabled(!busy);recallKit.setEnabled(!busy);findPresets.setEnabled(!busy&&main.selectedPlugin!=nullptr&&main.selectedPluginFormat=="VST3");choosePresets.setEnabled(!busy&&main.selectedPlugin!=nullptr&&main.selectedPluginFormat=="VST3");velocities.setEnabled(!busy);noteDuration.setEnabled(!busy);releaseTime.setEnabled(!busy);captureLength.setEnabled(!busy);repeatCount.setEnabled(!busy);renderMode.setEnabled(!busy);scanNotes.setEnabled(!busy);cancelScan.setEnabled(busy);showLayers.setEnabled(!busy&&main.selectedPlugin!=nullptr);showRoundRobin.setEnabled(!busy&&main.selectedPlugin!=nullptr);}
  void setMessage(const juce::String& text){message.setText(text,juce::dontSendNotification);}
 private:
   void changeListenerCallback(juce::ChangeBroadcaster* source)override{if(source==&magdrum::ui::languageChangeBroadcaster())applyLanguage();}
   void applyLanguage(){const int renderModeId=renderMode.getSelectedId();load.setButtonText(magdrum::ui::localisedText("Load selected"));programs.setTextWhenNothingSelected(magdrum::ui::localisedText("Instrument programs"));programs.setTooltip(magdrum::ui::localisedText("Programs exposed by the loaded instrument; many instruments do not expose a list."));kitName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Kit or preset name"),magdrum::ui::Palette::Muted);saveKit.setButtonText(magdrum::ui::localisedText("Save state"));recallKit.setButtonText(magdrum::ui::localisedText("Recall saved kit"));findPresets.setButtonText(magdrum::ui::localisedText("Find standard presets"));findPresets.setTooltip(magdrum::ui::localisedText("Search standard VST3 preset locations for this instrument."));choosePresets.setButtonText(magdrum::ui::localisedText("Choose preset folder"));choosePresets.setTooltip(magdrum::ui::localisedText("Search a selected folder recursively for .vstpreset files."));velocities.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity levels"),magdrum::ui::Palette::Muted);noteDuration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Note sec"),magdrum::ui::Palette::Muted);releaseTime.setTextToShowWhenEmpty(magdrum::ui::localisedText("Release sec"),magdrum::ui::Palette::Muted);captureLength.setTextToShowWhenEmpty(magdrum::ui::localisedText("Capture sec"),magdrum::ui::Palette::Muted);repeatCount.setTooltip(magdrum::ui::localisedText("Repeated captures per note and velocity (1–8); helps detect round-robin variation."));renderMode.clear(juce::dontSendNotification);renderMode.addItem(magdrum::ui::localisedText("Offline (fast)"),1);renderMode.addItem(magdrum::ui::localisedText("Real-time paced"),2);renderMode.addItem(magdrum::ui::localisedText("Audio device fallback"),3);renderMode.setSelectedId(renderModeId>0?renderModeId:1,juce::dontSendNotification);renderMode.setTooltip(magdrum::ui::localisedText("Use a real audio-device callback when offline rendering fails. The default output device is opened and scan audio is sent to it."));scanNotes.setButtonText(magdrum::ui::localisedText("Scan notes"));cancelScan.setButtonText(magdrum::ui::localisedText("Cancel"));showLayers.setButtonText(magdrum::ui::localisedText("View layer candidates"));showRoundRobin.setButtonText(magdrum::ui::localisedText("View round-robin candidates"));message.setText(juce::String(),juce::dontSendNotification);}
  class PresetDiscoveryJob final:public juce::ThreadPoolJob{
  public:PresetDiscoveryJob(PluginBrowser& browser,juce::Array<juce::File> searchRoots,juce::String pluginIdentifier,std::shared_ptr<std::atomic<bool>> cancel):ThreadPoolJob("MagDrum VST3 preset discovery"),owner(&browser),roots(std::move(searchRoots)),identifier(std::move(pluginIdentifier)),cancellation(std::move(cancel)){}
   JobStatus runJob()override{auto result=findVst3PresetFiles(roots,[this]{return shouldExit()||cancellation->load();});if(shouldExit()||cancellation->load())result.cancelled=true;auto safe=owner;juce::MessageManager::callAsync([safe,identifier=identifier,result=std::move(result)]()mutable{if(safe!=nullptr)safe->finishPresetDiscovery(identifier,std::move(result));});return jobHasFinished;}
  private:juce::Component::SafePointer<PluginBrowser>owner;juce::Array<juce::File>roots;juce::String identifier;std::shared_ptr<std::atomic<bool>>cancellation;
  };
  void findStandardPresets(){if(!main.selectedPlugin||main.selectedPluginFormat!="VST3"){message.setText(magdrum::ui::localisedText("Load a VST3 instrument before searching for VST3 preset files."),juce::dontSendNotification);return;}const auto roots=getStandardVst3PresetSearchRoots(main.selectedPluginDescription.manufacturerName,main.selectedPluginDescription.name,juce::File::getSpecialLocation(juce::File::currentExecutableFile));startPresetDiscovery(roots,"Searching standard VST3 preset locations…",true);}
  void choosePresetFolder(){if(!main.selectedPlugin||main.selectedPluginFormat!="VST3"){message.setText(magdrum::ui::localisedText("Load a VST3 instrument before searching for VST3 preset files."),juce::dontSendNotification);return;}presetFolderChooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Select a folder containing VST3 presets"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));juce::Component::SafePointer<PluginBrowser>safe(this);presetFolderChooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser& chooser){if(safe==nullptr)return;const auto folder=chooser.getResult();if(folder.isDirectory())safe->startPresetDiscovery(folder);});}
  void startPresetDiscovery(const juce::File& folder){juce::Array<juce::File> roots;roots.add(folder);startPresetDiscovery(roots,"Searching the selected folder for .vstpreset files…",false);}
  void startPresetDiscovery(const juce::Array<juce::File>& roots,const juce::String& progress,bool standardSearch){if(presetSearchCancellation)presetSearchCancellation->store(true);presetSearchCancellation=std::make_shared<std::atomic<bool>>(false);presetSearchWasStandard=standardSearch;main.workers.addJob(new PresetDiscoveryJob(*this,roots,main.selectedPluginIdentifier,presetSearchCancellation),true);message.setText(progress,juce::dontSendNotification);}
  void finishPresetDiscovery(const juce::String& identifier,PluginPresetSearchResult result){if(identifier!=main.selectedPluginIdentifier||main.selectedPluginFormat!="VST3")return;if(result.cancelled){message.setText(magdrum::ui::localisedText("VST3 preset search cancelled."),juce::dontSendNotification);return;}if(!result.rootIsDirectory){message.setText(presetSearchWasStandard?"No standard preset folder was found. Choose a folder to search another location.":"The selected preset location is not a folder.",juce::dontSendNotification);return;}if(result.files.isEmpty()){message.setText(presetSearchWasStandard?"No standard .vstpreset files were found. Choose a folder to search other locations.":"No .vstpreset files were found in that folder.",juce::dontSendNotification);return;}juce::PopupMenu menu;for(int index=0;index<result.files.size();++index){const auto& file=result.files.getReference(index);menu.addItem(index+1,file.getFileNameWithoutExtension()+"  ["+file.getParentDirectory().getFileName()+"]");}juce::Component::SafePointer<PluginBrowser>safe(this);auto* target=presetSearchWasStandard?static_cast<juce::Component*>(&findPresets):static_cast<juce::Component*>(&choosePresets);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withMinimumWidth(360),[safe,files=std::move(result.files),identifier](int selected){if(safe==nullptr||selected<1||selected>files.size())return;if(identifier!=safe->main.selectedPluginIdentifier)return;const auto file=files.getReference(selected-1);if(safe->main.loadVst3Preset(file)){safe->lastAutomaticKitName.clear();safe->kitName.setText(file.getFileNameWithoutExtension(),juce::dontSendNotification);safe->message.setText(magdrum::ui::localisedText("Loaded VST3 preset: ")+file.getFileName(),juce::dontSendNotification);}else safe->message.setText(magdrum::ui::localisedText("This preset could not be loaded by the selected instrument. Confirm it belongs to that VST3 plugin."),juce::dontSendNotification);});}
  void buttonClicked(juce::Button* button)override{if(button==&load){const int row=list.getTableListBox().getSelectedRow();const auto types=main.knownPlugins.getTypes();if(!juce::isPositiveAndBelow(row,types.size())){message.setText(magdrum::ui::localisedText("Select an instrument row first."),juce::dontSendNotification);return;}const auto description=types.getReference(row);if(!description.isInstrument){message.setText(magdrum::ui::localisedText("The selected plugin does not identify itself as an instrument."),juce::dontSendNotification);return;}main.loadPluginInstrument(description);}else if(button==&findPresets){findStandardPresets();}else if(button==&choosePresets){choosePresetFolder();}else if(button==&saveKit){if(main.saveSelectedPluginKit(kitName.getText()))message.setText(magdrum::ui::localisedText("Plugin state saved under this kit name."),juce::dontSendNotification);else message.setText(magdrum::ui::localisedText("Load an instrument and enter a kit name first."),juce::dontSendNotification);}else if(button==&recallKit){if(main.recallSelectedPluginKit(kitName.getText()))message.setText(magdrum::ui::localisedText("Saved plugin kit state recalled."),juce::dontSendNotification);else message.setText(magdrum::ui::localisedText("Load an instrument and enter a saved kit name first."),juce::dontSendNotification);}else if(button==&scanNotes){juce::StringArray tokens;tokens.addTokens(velocities.getText(),",; ","");std::vector<int> levels;for(const auto& token:tokens)if(token.trim().isNotEmpty())levels.push_back(token.getIntValue());if(main.startPluginNoteScan(kitName.getText(),levels,noteDuration.getText().getDoubleValue(),releaseTime.getText().getDoubleValue(),captureLength.getText().getDoubleValue(),repeatCount.getText().getIntValue(),renderMode.getSelectedId())){setScanBusy(true);const auto progressKey=renderMode.getSelectedId()==3?"Scanning through the default audio device… audio will be sent to its outputs; close the plugin editor before starting.":(renderMode.getSelectedId()==2?"Scanning MIDI notes with real-time pacing… close the plugin editor before starting.":"Scanning MIDI notes offline… close the plugin editor before starting.");message.setText(magdrum::ui::localisedText(progressKey),juce::dontSendNotification);}}else if(button==&cancelScan){main.cancelPluginNoteScan();message.setText(magdrum::ui::localisedText("Cancellation requested; current note will finish first."),juce::dontSendNotification);}else if(button==&showLayers){main.showPluginVelocityLayerCandidates(kitName.getText());}else if(button==&showRoundRobin){main.showPluginRoundRobinCandidates(kitName.getText());}}
  void timerCallback()override{auto* plugin=main.selectedPlugin.get();if(plugin==observedPlugin){if(plugin!=nullptr&&programs.getNumItems()>0){const int current=plugin->getCurrentProgram();if(juce::isPositiveAndBelow(current,programs.getNumItems())&&programs.getSelectedId()!=current+1){programs.setSelectedId(current+1,juce::dontSendNotification);const auto currentName=programs.getText();if(kitName.getText()==lastAutomaticKitName)kitName.setText(currentName,juce::dontSendNotification);lastAutomaticKitName=currentName;}}return;}if(presetSearchCancellation)presetSearchCancellation->store(true);observedPlugin=plugin;programs.clear(juce::dontSendNotification);lastAutomaticKitName.clear();if(plugin==nullptr){programs.setEnabled(false);findPresets.setEnabled(false);choosePresets.setEnabled(false);showLayers.setEnabled(false);showRoundRobin.setEnabled(false);return;}findPresets.setEnabled(main.selectedPluginFormat=="VST3");choosePresets.setEnabled(main.selectedPluginFormat=="VST3");showLayers.setEnabled(true);showRoundRobin.setEnabled(true);const auto discovered=discoverPluginPrograms(*plugin);if(discovered.exceedsLimit){message.setText(magdrum::ui::localisedText("This instrument exposes an unusually large program list; select its kit in the editor instead."),juce::dontSendNotification);programs.setEnabled(false);return;}if(discovered.programs.empty()){message.setText(main.selectedPluginFormat=="VST3"?"This instrument exposes no program list. Select its kit in the editor or load a .vstpreset file.":"This instrument exposes no program list. Choose a kit in its editor, then enter a name and save its state.",juce::dontSendNotification);programs.setEnabled(false);return;}for(const auto& program:discovered.programs)programs.addItem(program.name,program.index+1);const int current=plugin->getCurrentProgram();if(juce::isPositiveAndBelow(current,static_cast<int>(discovered.programs.size()))){programs.setSelectedId(current+1,juce::dontSendNotification);lastAutomaticKitName=programs.getText();kitName.setText(lastAutomaticKitName,juce::dontSendNotification);}programs.setEnabled(true);message.setText(magdrum::ui::localisedText("Discovered ")+juce::String(static_cast<int>(discovered.programs.size()))+" host-exposed programs. You can still select kits manually in the instrument editor.",juce::dontSendNotification);}
  MainComponent& main;juce::PluginListComponent list;juce::TextButton load,saveKit,recallKit,findPresets,choosePresets,scanNotes,cancelScan,showLayers,showRoundRobin;juce::ComboBox programs,renderMode;juce::TextEditor kitName,velocities,noteDuration,releaseTime,captureLength,repeatCount;juce::Label message;juce::AudioProcessor* observedPlugin=nullptr;juce::String lastAutomaticKitName;std::unique_ptr<juce::FileChooser>presetFolderChooser;std::shared_ptr<std::atomic<bool>>presetSearchCancellation;bool presetSearchWasStandard=false;
 };
 class PluginWindow final:public juce::DocumentWindow,private juce::ChangeListener{
 public:explicit PluginWindow(MainComponent& owner):DocumentWindow(magdrum::ui::localisedText("Plugin instruments"),magdrum::ui::Palette::Panel,juce::DocumentWindow::allButtons),main(owner){setUsingNativeTitleBar(true);setContentOwned(new PluginBrowser(main),true);setResizable(true,true);centreWithSize(1100,700);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);setVisible(true);}~PluginWindow()override{magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);}void changeListenerCallback(juce::ChangeBroadcaster* source)override{if(source==&magdrum::ui::languageChangeBroadcaster())setName(magdrum::ui::localisedText("Plugin instruments"));}void closeButtonPressed()override{main.pluginWindow.reset();}PluginBrowser* getBrowser(){return dynamic_cast<PluginBrowser*>(getContentComponent());}
 private:MainComponent&main;
 };
 class MidiPatternWindow final:public juce::DocumentWindow{
 public:explicit MidiPatternWindow(MainComponent& owner):DocumentWindow(magdrum::ui::localisedText("Edit MIDI pattern"),magdrum::ui::Palette::Panel,juce::DocumentWindow::allButtons),main(owner){setUsingNativeTitleBar(true);setContentOwned(new MidiPatternEditor(main.midiPattern,main.midiBaseline,main.hasMidiImportBaseline),true);setResizable(true,true);centreWithSize(900,420);setVisible(true);}void closeButtonPressed()override{main.midiPatternWindow.reset();}
 private:MainComponent& main;
 };
 class StepSequencerWindow final:public juce::DocumentWindow{
 public:explicit StepSequencerWindow(MainComponent& owner):DocumentWindow(magdrum::ui::localisedText("Drum step sequencer"),magdrum::ui::Palette::Panel,juce::DocumentWindow::allButtons),main(owner){setUsingNativeTitleBar(true);setContentOwned(new DrumStepSequencer(main.midiPattern,[ownerPtr=&main]{ownerPtr->playPatternPreview();},[ownerPtr=&main](bool enabled){ownerPtr->setPatternPreviewLooping(enabled);},main.patternPreviewLooping),true);setResizable(true,true);centreWithSize(1180,410);setVisible(true);}void closeButtonPressed()override{main.stepSequencerWindow.reset();}
 private:MainComponent& main;
 };
 class PluginEditorWindow final:public juce::DocumentWindow{
 public:PluginEditorWindow(MainComponent& owner,juce::AudioProcessorEditor* editor,const juce::String& name):DocumentWindow(name,magdrum::ui::Palette::Panel,juce::DocumentWindow::allButtons),main(owner){setUsingNativeTitleBar(true);setContentOwned(editor,true);setResizable(true,true);centreWithSize(760,560);setVisible(true);}void closeButtonPressed()override{main.pluginEditorWindow.reset();}
 private:MainComponent&main;
 };
 class PluginNoteScanJob final:public juce::ThreadPoolJob{
 public:
  PluginNoteScanJob(MainComponent& owner,juce::PluginDescription pluginDescription,juce::MemoryBlock savedState,PluginNoteScanSettings scanSettings,juce::File dbFile,juce::File previewRoot,juce::String identifier,std::int64_t kit,std::atomic<bool>& cancel):ThreadPoolJob("MagDrum isolated plugin MIDI note scan"),main(&owner),description(std::move(pluginDescription)),state(std::move(savedState)),settings(std::move(scanSettings)),databaseFile(std::move(dbFile)),previewBase(std::move(previewRoot)),pluginIdentifier(std::move(identifier)),kitId(kit),cancellation(cancel){}
  JobStatus runJob()override{
   StructuredLogger::log(StructuredLogger::Level::info,"plugin_scan.started",{{"plugin",pluginIdentifier},{"kit_id",juce::String(static_cast<juce::int64>(kitId))},{"repeat_count",juce::String(settings.repeatCount)},{"isolated_process","true"}});
   SampleDatabase db;if(!db.open(databaseFile)){finish(0,0,0,1,false,"Could not open the sound database.");return jobHasFinished;}
   juce::String pluginName=pluginIdentifier;for(const auto& instrument:db.getPluginInstruments())if(instrument.identifier==pluginIdentifier){pluginName=instrument.name;break;}const auto sourceId=db.ensureSampleSource("plugin",pluginIdentifier,pluginName);if(!sourceId){finish(0,0,0,1,false,"Could not save the plugin source record.");return jobHasFinished;}
   const auto session=previewBase.getChildFile(juce::String(pluginIdentifier.hashCode64())).getChildFile(juce::String(static_cast<juce::int64>(kitId)));
   if(!session.isDirectory()&&!session.createDirectory().wasOk()){finish(0,0,0,1,false,"Could not create the plugin preview folder.");return jobHasFinished;}
   const auto requestDirectory=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrum").getChildFile("plugin-scan-"+juce::Uuid().toString());
   const auto outputDirectory=requestDirectory.getChildFile("captures");
   if(!requestDirectory.createDirectory().wasOk()||!outputDirectory.createDirectory().wasOk()){finish(0,0,0,1,false,"Could not create isolated scan working files.");return jobHasFinished;}
   const auto descriptionFile=requestDirectory.getChildFile("plugin.xml"),stateFile=requestDirectory.getChildFile("kit-state.bin");
   auto descriptionXml=description.createXml();
   if(descriptionXml==nullptr||!descriptionXml->writeTo(descriptionFile)||!stateFile.replaceWithData(state.getData(),state.getSize())){requestDirectory.deleteRecursively();finish(0,0,0,1,false,"Could not prepare the saved kit state for isolated scanning.");return jobHasFinished;}
   const auto cancellationFile=requestDirectory.getChildFile("cancel"),progressFile=requestDirectory.getChildFile("progress.json"),captureLogFile=requestDirectory.getChildFile("captures.jsonl"),resultFile=requestDirectory.getChildFile("result.json"),requestFile=requestDirectory.getChildFile("request.json");
   auto* request=new juce::DynamicObject();request->setProperty("descriptionFile",descriptionFile.getFullPathName());request->setProperty("stateFile",stateFile.getFullPathName());request->setProperty("outputDirectory",outputDirectory.getFullPathName());request->setProperty("cancellationFile",cancellationFile.getFullPathName());request->setProperty("progressFile",progressFile.getFullPathName());request->setProperty("captureLogFile",captureLogFile.getFullPathName());request->setProperty("resultFile",resultFile.getFullPathName());request->setProperty("sampleRate",settings.sampleRate);request->setProperty("blockSize",settings.blockSize);request->setProperty("repeatCount",settings.repeatCount);request->setProperty("noteDurationSeconds",settings.noteDurationSeconds);request->setProperty("releaseSeconds",settings.releaseSeconds);request->setProperty("captureLengthSeconds",settings.captureLengthSeconds);request->setProperty("realtimePaced",settings.renderMode==PluginNoteRenderMode::realtimePaced);request->setProperty("audioDeviceRealtime",settings.renderMode==PluginNoteRenderMode::audioDevice);juce::Array<juce::var> velocities;for(const auto velocity:settings.velocityLevels)velocities.add(velocity);request->setProperty("velocityLevels",velocities);
   if(!requestFile.replaceWithText(juce::JSON::toString(juce::var(request),true))){requestDirectory.deleteRecursively();finish(0,0,0,1,false,"Could not write the isolated scan request.");return jobHasFinished;}
    juce::StringArray command;command.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());command.add("--magdrum-plugin-scan-worker");command.add(requestFile.getFullPathName());
    auto safe=main;
    const auto execution=PluginScanProcess::monitorWorker(command,progressFile,resultFile,cancellationFile,
     [this]{return shouldExit()||cancellation.load();},
     [safe](int done,int total,int note,int velocity){juce::MessageManager::callAsync([safe,done,total,note,velocity]{if(safe!=nullptr)safe->setPluginScanProgress(done,total,note,velocity);});},10000,captureLogFile);
    if(!execution.started){requestDirectory.deleteRecursively();finish(0,0,0,1,false,"Could not start the isolated plugin scanner process.");return jobHasFinished;}
    const auto& result=execution.manifest;
    if(!execution.manifestRead){
     const auto message=execution.terminatedForCancellation?"Plugin scan cancelled after the isolated instrument stopped responding.":(execution.exitCode!=0?"The instrument crashed in the isolated scanner process. MagDrum is still running.":"The isolated scanner exited without a valid result.");
     StructuredLogger::log(execution.terminatedForCancellation?StructuredLogger::Level::warning:StructuredLogger::Level::error,"plugin_scan.worker_failed",{{"plugin",pluginIdentifier},{"exit_code",juce::String(execution.exitCode)}});requestDirectory.deleteRecursively();finish(0,0,0,execution.terminatedForCancellation?0:1,execution.terminatedForCancellation,message);return jobHasFinished;
   }
   if((!result.succeeded||result.error.isNotEmpty())&&result.captures.empty()){
    const auto message=result.error.isNotEmpty()?result.error:"The isolated scanner reported an unsuccessful scan.";StructuredLogger::log(StructuredLogger::Level::error,"plugin_scan.failed",{{"plugin",pluginIdentifier},{"error",message}});requestDirectory.deleteRecursively();finish(0,0,0,1,false,message);return jobHasFinished;
   }
   std::unordered_map<std::string,juce::File> permanentFiles;
   int failures=0;
   for(const auto& capture:result.captures)if(!capture.silent){const auto temporary=outputDirectory.getChildFile(capture.fileName);const auto permanent=session.getChildFile(capture.fileName);if(permanent.existsAsFile())permanent.deleteFile();if(!temporary.existsAsFile()||!temporary.copyFileTo(permanent))++failures;else permanentFiles.emplace(temporary.getFullPathName().toStdString(),permanent);}
   const auto analyzed=WavScanner{}.scan(outputDirectory);std::unordered_map<std::string,const ScannedSample*> samplesByPath;samplesByPath.reserve(static_cast<size_t>(analyzed.size()));for(const auto&sample:analyzed)samplesByPath.emplace(sample.file.getFullPathName().toStdString(),&sample);int sounding=0,silent=0,saved=0;std::vector<PluginSoundRecord> analyzedRecords;std::vector<PluginRoundRobinTake> roundRobinTakes;
   for(const auto&capture:result.captures){PluginSoundRecord record;record.midiNote=capture.midiNote;record.velocity=capture.velocity;record.silent=capture.silent;record.duration=static_cast<double>(capture.sampleCount)/(capture.sampleRate>0.0?capture.sampleRate:settings.sampleRate);record.sampleRate=capture.sampleRate>0.0?capture.sampleRate:settings.sampleRate;record.channels=capture.channels;record.peak=capture.peak;record.rms=capture.rms;PluginRoundRobinTake take;take.midiNote=capture.midiNote;take.velocity=capture.velocity;take.takeIndex=capture.takeIndex;take.silent=capture.silent;if(capture.silent)++silent;else{++sounding;const auto temporary=outputDirectory.getChildFile(capture.fileName);const auto permanentIt=permanentFiles.find(temporary.getFullPathName().toStdString());if(permanentIt!=permanentFiles.end()){const auto&permanent=permanentIt->second;record.previewPath=permanent.getFullPathName();take.previewPath=record.previewPath;const auto sampleIt=samplesByPath.find(temporary.getFullPathName().toStdString());if(sampleIt!=samplesByPath.end()){auto sample=*sampleIt->second;sample.file=permanent;sample.fileSizeBytes=permanent.getSize();sample.modifiedTimeMillis=permanent.getLastModificationTime().toMilliseconds();record.duration=sample.durationSeconds;record.sampleRate=sample.sampleRate;record.channels=sample.channels;record.peak=sample.peak;record.rms=sample.rms;record.spectralCentroid=sample.spectralCentroidHz;record.spectralFlatness=sample.spectralFlatness;record.transientStrength=sample.transientStrength;record.transientCount=sample.transientCount;record.fundamentalHz=sample.fundamentalHz;record.pitchConfidence=sample.pitchConfidence;record.fingerprint=sample.fingerprint;record.acousticFingerprint=sample.acousticFingerprint;record.category=sample.category;record.confidence=sample.classificationConfidence;take.acousticFingerprint=sample.acousticFingerprint;if(!db.insertSample(sample,*sourceId))++failures;}}}roundRobinTakes.push_back(take);if(capture.takeIndex==1){if(db.upsertPluginSound(kitId,record)){++saved;analyzedRecords.push_back(record);}else ++failures;}}
   requestDirectory.deleteRecursively();
   const auto layerCandidates=PluginVelocityLayerAnalyzer{}.analyze(analyzedRecords);if(!db.replacePluginVelocityLayerCandidates(kitId,layerCandidates))++failures;
   const auto roundRobinCandidates=PluginRoundRobinAnalyzer{}.analyze(roundRobinTakes);if(!db.replacePluginRoundRobinData(kitId,roundRobinTakes,roundRobinCandidates))++failures;
   juce::String layerSummary;if(layerCandidates.empty())layerSummary="No strong timbral changes were detected between the sampled velocity levels.";else{layerSummary=juce::String(layerCandidates.size())+" possible velocity layer boundary candidate(s): ";const int shown=juce::jmin(12,static_cast<int>(layerCandidates.size()));for(int index=0;index<shown;++index){if(index>0)layerSummary+="; ";const auto&candidate=layerCandidates[static_cast<size_t>(index)];layerSummary+="note "+juce::String(candidate.midiNote)+" v"+juce::String(candidate.lowerVelocity)+"→v"+juce::String(candidate.upperVelocity);}if(layerCandidates.size()>static_cast<size_t>(shown))layerSummary+="; …";layerSummary+=" (heuristic; review by ear)";}
   juce::String roundRobinSummary;if(roundRobinCandidates.empty())roundRobinSummary="No repeated-trigger variation candidates were detected.";else roundRobinSummary=juce::String(static_cast<int>(roundRobinCandidates.size()))+" possible repeated-trigger variation candidate(s); review by ear.";
   const bool wasCancelled=result.cancelled||execution.terminatedForCancellation;
   const auto scanError=result.error;
   StructuredLogger::log(wasCancelled?StructuredLogger::Level::warning:(scanError.isNotEmpty()?StructuredLogger::Level::error:StructuredLogger::Level::info),"plugin_scan.finished",{{"plugin",pluginIdentifier},{"saved_note_records",juce::String(saved)},{"audible_takes",juce::String(sounding)},{"silent_takes",juce::String(silent)},{"errors",juce::String(failures)},{"cancelled",wasCancelled?"true":"false"},{"error",scanError}});
   finish(saved,sounding,silent,failures,wasCancelled,scanError,layerSummary+" "+roundRobinSummary);return jobHasFinished;
  }
 private:
  void finish(int saved,int sounding,int silent,int failures,bool cancelled,const juce::String& error,const juce::String& layerSummary={}){auto safe=main;juce::MessageManager::callAsync([safe,saved,sounding,silent,failures,cancelled,error,layerSummary]{if(safe!=nullptr)safe->finishPluginNoteScan(saved,sounding,silent,failures,cancelled,error,layerSummary);});}
  juce::Component::SafePointer<MainComponent>main;juce::PluginDescription description;juce::MemoryBlock state;PluginNoteScanSettings settings;juce::File databaseFile,previewBase;juce::String pluginIdentifier;std::int64_t kitId;std::atomic<bool>&cancellation;
 };
 class BrowsePageJob final : public juce::ThreadPoolJob
 {
 public:
  BrowsePageJob(MainComponent& component,SampleDatabase& readOnlyDatabase,SampleBrowseFilter query,int start,std::uint64_t generation)
      : ThreadPoolJob("MagDrum sample browse page"),owner(&component),database(readOnlyDatabase),filter(std::move(query)),offset(start),requestGeneration(generation) {}
  JobStatus runJob() override
  {
   auto page=database.searchSampleBrowseRecords(filter,pageSize,offset);
   auto safe=owner;const auto generation=requestGeneration;const auto start=offset;
   juce::MessageManager::callAsync([safe,generation,start,page=std::move(page)]()mutable
   {if(safe!=nullptr)safe->finishBrowsePage(generation,start,std::move(page));});
   return jobHasFinished;
  }
 private:
  static constexpr int pageSize=250;
  juce::Component::SafePointer<MainComponent> owner;
  SampleDatabase& database;
  SampleBrowseFilter filter;
  int offset;
  std::uint64_t requestGeneration;
 };
 class ScanJob final : public juce::ThreadPoolJob {
 public:
  ScanJob(MainComponent& component,juce::File directory,juce::File dbFile,std::atomic<bool>& cancelFlag):ThreadPoolJob("MagDrum sample scan"),owner(&component),root(std::move(directory)),databaseFile(std::move(dbFile)),cancellation(cancelFlag){}
  JobStatus runJob()override
  {
   StructuredLogger::log(StructuredLogger::Level::info,"sample_scan.started",{{"root",root.getFullPathName()}});
   WavScanner::Stats stats;std::int64_t indexed=0,scanId=0;juce::String error;SampleDatabase scanDatabase;
   juce::String resumeAfter;
   if(!scanDatabase.open(databaseFile))error="Could not open the sample database for scanning.";
   const auto rootPath=root.getFullPathName();const auto sourceId=error.isEmpty()?scanDatabase.ensureSampleSource("folder",rootPath,root.getFileName().isNotEmpty()?root.getFileName():rootPath,rootPath):std::optional<std::int64_t>();if(error.isEmpty()&&!sourceId)error="Could not save the sample folder source record.";
   if(error.isEmpty())
   {
    resumeAfter=scanDatabase.getResumableSampleScanPath(root);
    const auto started=scanDatabase.beginSampleScan(root,resumeAfter);
    if(!started)error="Could not create a scan-history record.";
    else scanId=*started;
   }
   if(error.isEmpty())
   {
    auto examples=scanDatabase.getManualClassificationExamples();
    std::shared_ptr<const DrumClassificationModel> learnedModel;
    if(!examples.empty())learnedModel=std::make_shared<ManualExampleDrumClassificationModel>(std::move(examples));
    WavScanner scanner;
    (void) scanner.scan(root,[this]{return shouldExit()||cancellation.load();},[&scanDatabase](const juce::File& file,std::int64_t size,std::int64_t modified){return scanDatabase.isSampleUnchanged(file,size,modified);},&stats,[&](const ScannedSample& sample){if(!scanDatabase.insertSample(sample,*sourceId))return false;++indexed;return true;},resumeAfter,[&scanDatabase,scanId](const juce::File& file){return scanDatabase.updateSampleScanCheckpoint(scanId,file);},std::move(learnedModel));
    if(!scanDatabase.assignSamplesToSource(root,*sourceId))++stats.errors;
    const auto state=stats.cancelled?"cancelled":"completed";
    if(!scanDatabase.finishSampleScan(scanId,state,stats.filesConsidered,stats.filesSkippedUnchanged,indexed,stats.errors))
     error="The sample scan finished, but its history record could not be updated.";
   }
   StructuredLogger::log(error.isNotEmpty()?StructuredLogger::Level::error:(stats.cancelled?StructuredLogger::Level::warning:StructuredLogger::Level::info),"sample_scan.finished",{{"root",root.getFullPathName()},{"indexed",juce::String(static_cast<juce::int64>(indexed))},{"skipped_unchanged",juce::String(stats.filesSkippedUnchanged)},{"errors",juce::String(stats.errors)},{"cancelled",stats.cancelled?"true":"false"},{"error",error}});
   auto safe=owner;
   juce::MessageManager::callAsync([safe,stats,indexed,error]{if(safe!=nullptr)safe->finishScan(stats,indexed,error);});
   return jobHasFinished;
  }
 private:juce::Component::SafePointer<MainComponent>owner;juce::File root,databaseFile;std::atomic<bool>&cancellation;
 };
  class PortableMetadataJob final : public juce::ThreadPoolJob {
  public:
   PortableMetadataJob(MainComponent& component,juce::File database,juce::File file,bool importing)
       :ThreadPoolJob(importing?"MagDrum metadata import":"MagDrum metadata export"),owner(&component),databaseFile(std::move(database)),archiveFile(std::move(file)),isImport(importing){}
   JobStatus runJob()override
   {
    SampleDatabase metadataDatabase;juce::String error;PortableMetadataMergeResult mergeResult;bool fileWritten=false;
    if(!metadataDatabase.open(databaseFile))error="Could not open the sample database.";
    else if(isImport)
    {
     if(archiveFile.getSize()>128LL*1024*1024)error="Metadata archive is larger than the 128 MB safety limit.";
     else mergeResult=metadataDatabase.mergePortableMetadata(juce::JSON::parse(archiveFile.loadFileAsString()));
     if(error.isEmpty()&&!mergeResult.ok)error=mergeResult.error;
    }
    else
    {
     const auto json=juce::JSON::toString(metadataDatabase.exportPortableMetadata(),false);
     fileWritten=archiveFile.replaceWithText(json);
     if(!fileWritten)error="Could not write the metadata archive.";
    }
    auto safe=owner;
    juce::MessageManager::callAsync([safe,file=archiveFile,isImport=isImport,mergeResult,error,fileWritten]{
     if(safe==nullptr)return;
     if(!error.isEmpty()){safe->status.setText(magdrum::ui::localisedText(isImport?"Metadata import failed: {error}":"Metadata export failed: {error}").replace("{error}",magdrum::ui::localisedText(error)),juce::dontSendNotification);return;}
     if(!isImport&&fileWritten){safe->status.setText(magdrum::ui::localisedText("Metadata archive exported. It contains tags, manual categories and MIDI profiles, not audio files or local paths."),juce::dontSendNotification);return;}
     if(isImport&&mergeResult.ok)safe->finishPortableMetadataImport(mergeResult);
     juce::ignoreUnused(file);
    });
    return jobHasFinished;
   }
  private:
   juce::Component::SafePointer<MainComponent> owner;juce::File databaseFile,archiveFile;bool isImport=false;
  };
  class MetadataSyncJob final : public juce::ThreadPoolJob {
  public:
   MetadataSyncJob(MainComponent& component,juce::File database,juce::File folder)
       :ThreadPoolJob("MagDrum metadata folder sync"),owner(&component),databaseFile(std::move(database)),syncFolder(std::move(folder)){}
   JobStatus runJob()override
   {
    SampleDatabase metadataDatabase;PortableMetadataSyncResult result;
    if(metadataDatabase.open(databaseFile))result=metadataDatabase.syncPortableMetadataFolder(syncFolder);
    else result.error="Could not open the sample database.";
    if(!result.ok)StructuredLogger::log(StructuredLogger::Level::error,"metadata_sync.failed",{{"folder",syncFolder.getFullPathName()},{"error",result.error}});
    else StructuredLogger::log(StructuredLogger::Level::info,"metadata_sync.finished",{{"folder",syncFolder.getFullPathName()},{"device_id",result.deviceId},{"archives_merged",juce::String(result.archivesMerged)},{"samples_matched",juce::String(result.samplesMatched)},{"samples_unavailable",juce::String(result.samplesUnavailable)},{"tags_added",juce::String(result.tagsAdded)},{"profiles_added",juce::String(result.profilesAdded)},{"category_conflicts",juce::String(result.categoryConflicts)},{"profile_conflicts",juce::String(result.profileConflicts)}});
    auto safe=owner;
    juce::MessageManager::callAsync([safe,result]{if(safe!=nullptr)safe->finishPortableMetadataSync(result);});
    return jobHasFinished;
   }
  private:
   juce::Component::SafePointer<MainComponent> owner;juce::File databaseFile,syncFolder;
  };
 class PatternPreviewJob final:public juce::ThreadPoolJob{
 public:
  PatternPreviewJob(MainComponent& owner,MidiPattern value,std::array<juce::File,9> sampleFiles,juce::File destination,std::shared_ptr<std::atomic<bool>> cancel,std::uint64_t request):ThreadPoolJob("MagDrum step sequencer preview"),main(&owner),pattern(std::move(value)),files(std::move(sampleFiles)),output(std::move(destination)),cancellation(std::move(cancel)),generation(request){}
  JobStatus runJob()override{
   juce::AudioFormatManager audioFormats;audioFormats.registerBasicFormats();PatternAudioRenderer::Samples samples;int loaded=0;juce::String error;
   for(size_t index=0;index<files.size();++index){if(shouldExit()||cancellation->load()){error="Preview cancelled.";break;}if(files[index].existsAsFile()){juce::String sampleError;if(PatternAudioRenderer::loadSample(audioFormats,files[index],samples[index],sampleError))++loaded;}}
   PatternAudioRenderResult result;if(error.isNotEmpty())result.error=error;else if(loaded==0)result.error="No matching drum samples are indexed. Add and scan a sample folder first.";else result=PatternAudioRenderer{}.render(pattern,samples,output,[this]{return shouldExit()||cancellation->load();});
   juce::String detail;if(result.ok&&loaded<static_cast<int>(files.size()))detail=" Some drum lanes have no indexed sample, so those hits are silent.";
   auto safe=main;juce::MessageManager::callAsync([safe,output=output,result,generation=generation,detail]{if(safe!=nullptr)safe->finishPatternPreview(generation,output,result,detail);else output.deleteFile();});return jobHasFinished;
  }
 private:juce::Component::SafePointer<MainComponent>main;MidiPattern pattern;std::array<juce::File,9>files;juce::File output;std::shared_ptr<std::atomic<bool>>cancellation;std::uint64_t generation;
 };
 void initDb(){auto base=getMagDrumDataDirectory();if(!base.isDirectory()&&!base.createDirectory().wasOk()){status.setText(magdrum::ui::localisedText("Could not create the MagDrum data directory."),juce::dontSendNotification);return;}pluginListFile=base.getChildFile("vst3-plugins.xml");pluginCrashList=base.getChildFile("RecentlyCrashedPluginsList");databaseFile=base.getChildFile("samples.sqlite3");if(database.open(databaseFile)){database.markRunningSampleScansInterrupted();browseDatabaseReady=browseDatabase.open(databaseFile);}if(!browseDatabaseReady)status.setText(magdrum::ui::localisedText("Could not open the sample index for browsing."),juce::dontSendNotification);}
 void loadPluginList(){if(auto xml=juce::XmlDocument::parse(pluginListFile))knownPlugins.recreateFromXml(*xml);}
 void syncPluginInventory(){for(const auto& plugin:knownPlugins.getTypes()){if(!plugin.isInstrument)continue;database.upsertPluginInstrument({plugin.createIdentifierString(),plugin.pluginFormatName,plugin.name,plugin.manufacturerName,plugin.category,plugin.fileOrIdentifier,plugin.version});}}
 void changeListenerCallback(juce::ChangeBroadcaster* changeSource)override{if(changeSource==&magdrum::ui::languageChangeBroadcaster()){applyLanguage();refreshLanguageFilters();refresh(true);return;}if(changeSource==&knownPlugins){if(auto xml=knownPlugins.createXml())xml->writeTo(pluginListFile);syncPluginInventory();refresh(true);}}
 void openSelectedSourceKit(){const int row=rows.getSelectedRow();if(!juce::isPositiveAndBelow(row,visible.size())){status.setText(magdrum::ui::localisedText("Select a captured plugin sound first."),juce::dontSendNotification);return;}const auto file=visible.getReference(row);const auto found=browseRecordsByPath.find(file.getFullPathName().toStdString());if(found==browseRecordsByPath.end()||found->second.pluginIdentifier.isEmpty()){status.setText(magdrum::ui::localisedText("The selected sample has no saved plugin source kit."),juce::dontSendNotification);return;}const auto identifier=found->second.pluginIdentifier;const auto kitName=found->second.kitName;for(const auto& description:knownPlugins.getTypes())if(description.createIdentifierString()==identifier){loadPluginInstrument(description,kitName);return;}status.setText(magdrum::ui::localisedText("The source instrument is not installed or discovered. Scan its plugin folder first."),juce::dontSendNotification);}
 void openSourceKitByIdentifier(const PluginSourceKitRequest& request){if(!request.isValid()){status.setText(magdrum::ui::localisedText("The saved source kit request is invalid."),juce::dontSendNotification);return;}for(const auto& description:knownPlugins.getTypes())if(description.isInstrument&&description.createIdentifierString()==request.pluginIdentifier){loadPluginInstrument(description,request.kitName);return;}status.setText(magdrum::ui::localisedText("The source instrument is not installed or discovered. Scan its plugin folder first."),juce::dontSendNotification);}
 void loadPluginInstrument(const juce::PluginDescription& description,const juce::String& kitToRecall={}){if(pluginScanRunning.load()){status.setText(magdrum::ui::localisedText("Wait for the current instrument scan to finish."),juce::dontSendNotification);return;}if(selectedPlugin&&selectedPluginIdentifier==description.createIdentifierString()){if(!kitToRecall.isEmpty()){if(recallSelectedPluginKit(kitToRecall))status.setText(magdrum::ui::localisedText("Recalled source kit: ")+kitToRecall,juce::dontSendNotification);else status.setText(magdrum::ui::localisedText("The source kit state could not be recalled."),juce::dontSendNotification);}showSelectedPluginEditor();return;}pluginEditorWindow.reset();selectedPlugin.reset();selectedPluginIdentifier=description.createIdentifierString();selectedPluginFormat=description.pluginFormatName;selectedPluginDescription=description;status.setText(magdrum::ui::localisedText("Loading ")+description.name+"…",juce::dontSendNotification);const auto generation=++pluginLoadGeneration;juce::Component::SafePointer<MainComponent>safe(this);pluginFormats.createPluginInstanceAsync(description,44100.0,512,[safe,description,generation,kitToRecall](std::unique_ptr<juce::AudioPluginInstance> instance,const juce::String& error){if(safe==nullptr||generation!=safe->pluginLoadGeneration)return;if(!instance){safe->selectedPluginIdentifier.clear();safe->selectedPluginFormat.clear();safe->selectedPluginDescription={};safe->status.setText(magdrum::ui::localisedText("Could not load plugin: ")+error,juce::dontSendNotification);return;}safe->selectedPluginName=description.name;safe->selectedPlugin=std::move(instance);if(kitToRecall.isNotEmpty()){if(safe->recallSelectedPluginKit(kitToRecall))safe->status.setText(magdrum::ui::localisedText("Loaded ")+description.name+" and recalled source kit: "+kitToRecall,juce::dontSendNotification);else safe->status.setText(magdrum::ui::localisedText("Loaded ")+description.name+", but its saved source kit state could not be recalled.",juce::dontSendNotification);}else safe->status.setText(magdrum::ui::localisedText("Loaded ")+description.name+". Select a kit in its editor, close it, then save or scan the kit state.",juce::dontSendNotification);safe->showSelectedPluginEditor();});}
 bool loadVst3Preset(const juce::File& file){if(pluginScanRunning.load()||!selectedPlugin||selectedPluginFormat!="VST3"||!file.existsAsFile()||file.getSize()<=0||file.getSize()>512LL*1024LL*1024LL)return false;juce::MemoryBlock preset;if(!file.loadFileAsData(preset)||preset.isEmpty())return false;struct PresetLoader final:juce::ExtensionsVisitor{explicit PresetLoader(const juce::MemoryBlock& dataToLoad):data(dataToLoad){}void visitVST3Client(const VST3Client& client)override{supported=true;loaded=client.setPreset(data);}const juce::MemoryBlock&data;bool supported=false,loaded=false;};PresetLoader loader(preset);selectedPlugin->getExtensions(loader);if(!loader.supported||!loader.loaded)return false;selectedPlugin->updateHostDisplay();return true;}
 void showSelectedPluginEditor(){if(!selectedPlugin)return;if(!selectedPlugin->hasEditor()){status.setText(magdrum::ui::localisedText("This plugin has no editor. Its current state can still be saved as a kit."),juce::dontSendNotification);return;}auto* editor=selectedPlugin->createEditorIfNeeded();if(editor==nullptr){status.setText(magdrum::ui::localisedText("The plugin editor could not be created."),juce::dontSendNotification);return;}if(pluginEditorWindow==nullptr)pluginEditorWindow=std::make_unique<PluginEditorWindow>(*this,editor,selectedPluginName);pluginEditorWindow->toFront(true);}
 bool saveSelectedPluginKit(const juce::String& name){if(!selectedPlugin||name.trim().isEmpty())return false;juce::MemoryBlock state;selectedPlugin->getStateInformation(state);if(state.getSize()==0)return false;return database.savePluginKit(selectedPluginIdentifier,name,state).has_value();}
 bool recallSelectedPluginKit(const juce::String& name){if(pluginScanRunning.load()||!selectedPlugin||name.trim().isEmpty())return false;auto state=database.loadPluginKitState(selectedPluginIdentifier,name);if(!state||state->isEmpty()||state->getSize()>static_cast<size_t>(std::numeric_limits<int>::max()))return false;const bool reopenEditor=pluginEditorWindow!=nullptr;pluginEditorWindow.reset();selectedPlugin->setStateInformation(state->getData(),static_cast<int>(state->getSize()));if(reopenEditor)showSelectedPluginEditor();return true;}
 bool startPluginNoteScan(const juce::String& kitName,const std::vector<int>& velocityLevels,double noteDuration,double releaseSeconds,double captureSeconds,int repeatCount,int renderMode){if(pluginScanRunning.load()||!selectedPlugin){status.setText(magdrum::ui::localisedText("Load one plugin instrument before scanning."),juce::dontSendNotification);return false;}if(pluginEditorWindow!=nullptr){status.setText(magdrum::ui::localisedText("Close the plugin editor after choosing its kit, then start the scan."),juce::dontSendNotification);return false;}if(kitName.trim().isEmpty()||velocityLevels.empty()||velocityLevels.size()>16||repeatCount<1||repeatCount>8){status.setText(magdrum::ui::localisedText("Enter a kit name, 1–16 velocity levels, and 1–8 repeats."),juce::dontSendNotification);return false;}PluginNoteScanSettings settings;settings.velocityLevels=velocityLevels;settings.repeatCount=repeatCount;settings.noteDurationSeconds=noteDuration;settings.releaseSeconds=releaseSeconds;settings.captureLengthSeconds=captureSeconds;settings.renderMode=renderMode==3?PluginNoteRenderMode::audioDevice:(renderMode==2?PluginNoteRenderMode::realtimePaced:PluginNoteRenderMode::offline);juce::MemoryBlock state;selectedPlugin->getStateInformation(state);if(state.getSize()==0||state.getSize()>static_cast<size_t>(std::numeric_limits<int>::max())){status.setText(magdrum::ui::localisedText("The plugin did not provide a saveable state for this kit."),juce::dontSendNotification);return false;}auto kit=database.savePluginKit(selectedPluginIdentifier,kitName,state);if(!kit){status.setText(magdrum::ui::localisedText("Could not save the kit state to the sound database."),juce::dontSendNotification);return false;}pluginScanCancellation.store(false);pluginScanRunning.store(true);status.setText(renderMode==3?"Preparing device-backed real-time MIDI scan…":(renderMode==2?"Preparing real-time paced MIDI scan…":"Preparing offline MIDI scan…"),juce::dontSendNotification);auto previewRoot=databaseFile.getSiblingFile("plugin-previews");workers.addJob(new PluginNoteScanJob(*this,selectedPlugin->getPluginDescription(),state,settings,databaseFile,previewRoot,selectedPluginIdentifier,*kit,pluginScanCancellation),true);return true;}
 void cancelPluginNoteScan(){if(pluginScanRunning.load())pluginScanCancellation.store(true);}
 void showPluginVelocityLayerCandidates(const juce::String& kitName)
 {
  if(selectedPluginIdentifier.isEmpty()||kitName.trim().isEmpty()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Velocity layer candidates"),magdrum::ui::localisedText("Load an instrument and enter the saved kit name first."));return;}
  const auto pluginIdentifier=selectedPluginIdentifier;
  const auto candidates=database.getPluginVelocityLayerCandidates(pluginIdentifier,kitName.trim());
  const auto confirmed=database.getConfirmedPluginVelocityLayers(pluginIdentifier,kitName.trim());
  if(candidates.empty()&&confirmed.empty()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Velocity layer candidates"),magdrum::ui::localisedText("No candidates or confirmed ranges are saved for this kit. Scan the same MIDI notes at two or more velocity levels first."));return;}
  juce::String report=magdrum::ui::localisedText("%1 heuristic boundary candidate(s). Listen to the captures before recording confirmed ranges. These ranges describe your review, not a claim about the plugin's internal sample zones.\n\nCandidates:\n").replace("%1",juce::String(candidates.size()));
  const int shown=juce::jmin(48,static_cast<int>(candidates.size()));
  for(int index=0;index<shown;++index){const auto&candidate=candidates[static_cast<size_t>(index)];report+=magdrum::ui::localisedText("MIDI note %1: velocity %2 to %3 (distance %4)\n").replace("%1",juce::String(candidate.midiNote)).replace("%2",juce::String(candidate.lowerVelocity)).replace("%3",juce::String(candidate.upperVelocity)).replace("%4",juce::String(candidate.spectralDistance,2));}
  if(candidates.size()>static_cast<size_t>(shown))report+=magdrum::ui::localisedText("Showing the first %1 candidates.\n").replace("%1",juce::String(shown));
  report+=magdrum::ui::localisedText("\nUser-confirmed ranges:\n");
  juce::String serialized;
  for(const auto&range:confirmed){report+=magdrum::ui::localisedText("MIDI note %1: velocity %2 to %3\n").replace("%1",juce::String(range.midiNote)).replace("%2",juce::String(range.minimumVelocity)).replace("%3",juce::String(range.maximumVelocity));if(serialized.isNotEmpty())serialized+="; ";serialized+=juce::String(range.midiNote)+":"+juce::String(range.minimumVelocity)+"-"+juce::String(range.maximumVelocity);}
  if(confirmed.empty())report+=magdrum::ui::localisedText("None\n");
  auto* dialog=new juce::AlertWindow(magdrum::ui::localisedText("Review velocity layers — %1").replace("%1",kitName.trim()),report,juce::AlertWindow::InfoIcon);
  dialog->addTextEditor("ranges",serialized,magdrum::ui::localisedText("Confirmed ranges (note:min-max; ...)"));
  dialog->addButton(magdrum::ui::localisedText("Save confirmed ranges"),1,juce::KeyPress(juce::KeyPress::returnKey));
  dialog->addButton(magdrum::ui::localisedText("Close"),0,juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<MainComponent> owner(this);
  dialog->enterModalState(true,juce::ModalCallbackFunction::create([owner,dialog,kitName,pluginIdentifier](int result)
  {
   const auto input=result==1?dialog->getTextEditorContents("ranges"):juce::String();
   delete dialog;
   if(result!=1||owner==nullptr)return;
   juce::StringArray entries;entries.addTokens(input,"; ,","");
   std::vector<PluginConfirmedVelocityLayerRange> ranges;
   for(const auto&entry:entries)
   {
    const auto item=entry.trim();if(item.isEmpty())continue;
    const auto noteText=item.upToFirstOccurrenceOf(":",false,false).trim();
    const auto rangeText=item.fromFirstOccurrenceOf(":",false,false).trim();
    const auto minimumText=rangeText.upToFirstOccurrenceOf("-",false,false).trim();
    const auto maximumText=rangeText.fromFirstOccurrenceOf("-",false,false).trim();
    if(noteText.isEmpty()||rangeText==item||minimumText.isEmpty()||maximumText.isEmpty()||!noteText.containsOnly("0123456789")||!minimumText.containsOnly("0123456789")||!maximumText.containsOnly("0123456789")){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,magdrum::ui::localisedText("Invalid velocity ranges"),magdrum::ui::localisedText("Use entries such as 36:1-48; 36:49-127."));return;}
    ranges.push_back({noteText.getIntValue(),minimumText.getIntValue(),maximumText.getIntValue()});
   }
   const auto kitId=owner->database.getPluginKitId(pluginIdentifier,kitName.trim());
   if(!kitId||!owner->database.replaceConfirmedPluginVelocityLayers(*kitId,ranges))juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,magdrum::ui::localisedText("Could not save velocity ranges"),magdrum::ui::localisedText("Check that note numbers are 0–127, velocities are 1–127, and ranges for each note do not overlap."));
   else juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,magdrum::ui::localisedText("Velocity ranges saved"),magdrum::ui::localisedText("The user-confirmed ranges are stored with this kit."));
  }),false);
 }
 void showPluginRoundRobinCandidates(const juce::String& kitName)
 {
  if(selectedPluginIdentifier.isEmpty()||kitName.trim().isEmpty()){juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,magdrum::ui::localisedText("Round-robin candidates"),magdrum::ui::localisedText("Load an instrument and enter the saved kit name first."));return;}
  const auto pluginIdentifier=selectedPluginIdentifier;
  const auto candidates=database.getPluginRoundRobinCandidates(pluginIdentifier,kitName.trim());
  const auto confirmed=database.getConfirmedPluginRoundRobinGroups(pluginIdentifier,kitName.trim());
  if(candidates.empty()){juce::String report=magdrum::ui::localisedText("No repeated-trigger variation candidates are saved. Scan with 2–8 repeats, then listen to the extra takes in the sample browser.");for(const auto&group:confirmed){juce::String takes;for(const int take:group.takeIndices){if(takes.isNotEmpty())takes+=" ";takes+=juce::String(take);}report+="\n"+magdrum::ui::localisedText("Confirmed MIDI note %1, velocity %2, takes %3\n").replace("%1",juce::String(group.midiNote)).replace("%2",juce::String(group.velocity)).replace("%3",takes);}juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,magdrum::ui::localisedText("Round-robin groups — %1").replace("%1",kitName.trim()),report);return;}
  juce::String report=magdrum::ui::localisedText("%1 heuristic variation candidate(s). Listen to the indexed takes; only confirm a group after reviewing its audio.\n\n").replace("%1",juce::String(candidates.size()));
  juce::StringArray choices;
  for(const auto&candidate:candidates)
  {
   const auto isConfirmed=std::any_of(confirmed.begin(),confirmed.end(),[&](const auto&group){return group.midiNote==candidate.midiNote&&group.velocity==candidate.velocity;});
   const auto confirmation=isConfirmed?magdrum::ui::localisedText(" (confirmed)"):juce::String();
   const auto label=magdrum::ui::localisedText("MIDI note %1, velocity %2 — %3 variants / %4 takes%5").replace("%1",juce::String(candidate.midiNote)).replace("%2",juce::String(candidate.velocity)).replace("%3",juce::String(candidate.distinctVariants)).replace("%4",juce::String(candidate.takeCount)).replace("%5",confirmation);
   choices.add(label);report+=label+"\n";
  }
  auto* dialog=new juce::AlertWindow(magdrum::ui::localisedText("Review round-robin groups — %1").replace("%1",kitName.trim()),report,juce::AlertWindow::InfoIcon);
  dialog->addComboBox("group",choices,magdrum::ui::localisedText("Candidate"));
  dialog->addButton(magdrum::ui::localisedText("Toggle confirmation"),1,juce::KeyPress(juce::KeyPress::returnKey));
  dialog->addButton(magdrum::ui::localisedText("Close"),0,juce::KeyPress(juce::KeyPress::escapeKey));
  juce::Component::SafePointer<MainComponent> owner(this);
  dialog->enterModalState(true,juce::ModalCallbackFunction::create([owner,dialog,candidates,confirmed,kitName,pluginIdentifier](int result)
  {
   const auto* selection=dialog->getComboBoxComponent("group");
   const int index=selection!=nullptr?selection->getSelectedItemIndex():-1;
   delete dialog;
   if(result!=1||owner==nullptr||!juce::isPositiveAndBelow(index,static_cast<int>(candidates.size())))return;
   const auto&candidate=candidates[static_cast<size_t>(index)];
   const auto isConfirmed=std::any_of(confirmed.begin(),confirmed.end(),[&](const auto&group){return group.midiNote==candidate.midiNote&&group.velocity==candidate.velocity;});
   const auto kitId=owner->database.getPluginKitId(pluginIdentifier,kitName.trim());
   const bool saved=kitId&&(isConfirmed?owner->database.removeConfirmedPluginRoundRobinGroup(*kitId,candidate.midiNote,candidate.velocity):owner->database.confirmPluginRoundRobinGroup(*kitId,candidate.midiNote,candidate.velocity));
   const auto title=saved?(isConfirmed?magdrum::ui::localisedText("Confirmation removed"):magdrum::ui::localisedText("Round-robin group confirmed")):magdrum::ui::localisedText("Could not update group");
   const auto message=saved?(isConfirmed?magdrum::ui::localisedText("The group is no longer marked as confirmed."):magdrum::ui::localisedText("The exact audible take indices are saved as a user-confirmed group.")):magdrum::ui::localisedText("This candidate no longer has at least two indexed, audible takes.");
   juce::AlertWindow::showMessageBoxAsync(saved?juce::AlertWindow::InfoIcon:juce::AlertWindow::WarningIcon,title,message);
  }),false);
 } void setPluginScanProgress(int done,int total,int note,int velocity){status.setText(magdrum::ui::localisedText("Scanning MIDI note ")+juce::String(note)+" at velocity "+juce::String(velocity)+" ("+juce::String(done)+"/"+juce::String(total)+").",juce::dontSendNotification);if(pluginWindow!=nullptr)if(auto* browser=pluginWindow->getBrowser())browser->setMessage(status.getText());}
 void finishPluginNoteScan(int saved,int sounding,int silent,int failures,bool cancelled,const juce::String& error,const juce::String& layerSummary){pluginScanRunning.store(false);juce::String message;if(error.isNotEmpty())message=magdrum::ui::localisedText("Plugin note scan failed: {error}").replace("{error}",error);else message=magdrum::ui::localisedText("Plugin note scan {state}: {saved} saved, {sounding} sounding, {silent} silent, {failures} errors. {summary}").replace("{state}",magdrum::ui::localisedText(cancelled?"cancelled":"complete")).replace("{saved}",juce::String(saved)).replace("{sounding}",juce::String(sounding)).replace("{silent}",juce::String(silent)).replace("{failures}",juce::String(failures)).replace("{summary}",layerSummary);status.setText(message,juce::dontSendNotification);if(pluginWindow!=nullptr)if(auto* browser=pluginWindow->getBrowser()){browser->setScanBusy(false);browser->setMessage(message);}refresh(true);}
 void buttonClicked(juce::Button*b)override{if(b==&scanHistory)showScanHistory();else if(b==&pluginScan){if(pluginWindow==nullptr)pluginWindow=std::make_unique<PluginWindow>(*this);pluginWindow->toFront(true);status.setText(juce::String(database.getPluginInstruments().size())+" plugin instruments indexed.",juce::dontSendNotification);}else if(b==&scan){if(scanning){sampleScanCancellation.store(true);scan.setEnabled(false);scan.setButtonText(magdrum::ui::localisedText("Cancelling…"));status.setText(magdrum::ui::localisedText("Cancelling the current sample scan…"),juce::dontSendNotification);return;}chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose an audio sample folder"),juce::File(),juce::String());juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser&c){if(safe!=nullptr){auto d=c.getResult();if(d.isDirectory())safe->startScan(d);}});}else if(b==&play)playSelected();else if(b==&midiImport)chooseMidiImport();else if(b==&midiExport)chooseMidiExport();else if(b==&midiEdit){if(midiPattern.events.empty()){status.setText(magdrum::ui::localisedText("Import a MIDI file before editing its drum events."),juce::dontSendNotification);return;}if(midiPatternWindow==nullptr)midiPatternWindow=std::make_unique<MidiPatternWindow>(*this);midiPatternWindow->toFront(true);}else if(b==&profileImport)chooseProfileImport();else if(b==&profileExport)chooseProfileExport();else if(b==&profileSave){if(database.saveMappingProfile(profileName.getText(),mappingProfile))status.setText(magdrum::ui::localisedText("Mapping profile saved."),juce::dontSendNotification);else status.setText(magdrum::ui::localisedText("Could not save profile."),juce::dontSendNotification);}else if(b==&profileLoad){auto loaded=database.loadMappingProfile(profileName.getText());if(loaded){mappingProfile=*loaded;status.setText(magdrum::ui::localisedText("Mapping profile loaded."),juce::dontSendNotification);}else status.setText(magdrum::ui::localisedText("Profile not found or empty."),juce::dontSendNotification);}else if(b==&mappingSet||b==&mappingRemove){const auto instrument=static_cast<DrumInstrument>(mappingInstrument.getSelectedId()-1);const auto articulation=static_cast<DrumArticulation>(mappingArticulation.getSelectedId()-1);bool changed=false;if(b==&mappingSet){const int note=mappingNote.getText().getIntValue();if(note<0||note>127){status.setText(magdrum::ui::localisedText("Enter a MIDI note from 0 to 127."),juce::dontSendNotification);return;}changed=mappingProfile.setMapping({instrument,articulation,static_cast<std::uint8_t>(note)});}else changed=mappingProfile.removeMapping(instrument,articulation);if(changed&&database.saveMappingProfile(profileName.getText(),mappingProfile))status.setText(b==&mappingSet?"Mapping updated and saved.":"Mapping removed and profile saved.",juce::dontSendNotification);else status.setText(magdrum::ui::localisedText("No mapping change was saved."),juce::dontSendNotification);}else if(b==&categorySave||b==&tagsSave||b==&similar||b==&duplicates||b==&perceptualDuplicates){auto n=rows.getSelectedRow();if(!juce::isPositiveAndBelow(n,visible.size())){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}auto f=visible.getReference(n);if(b==&categorySave){static constexpr const char* categories[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};const int categoryId=categoryEdit.getSelectedId();if(juce::isPositiveAndBelow(categoryId-1,12))database.setUserCategory(f,categories[static_cast<size_t>(categoryId-1)]);status.setText(magdrum::ui::localisedText("Category saved."),juce::dontSendNotification);refresh();}else if(b==&tagsSave){juce::StringArray values;values.addTokens(tags.getText(),",","");database.setUserTags(f,values);status.setText(magdrum::ui::localisedText("Tags saved."),juce::dontSendNotification);}else if(b==&similar){auto matches=database.findSimilar(f,20);status.setText(magdrum::ui::localisedText("%1 similar samples found in the index.").replace("%1",juce::String(matches.size())),juce::dontSendNotification);}else if(b==&perceptualDuplicates){const auto matches=database.findPerceptualDuplicates(f);juce::String details;for(const auto&match:matches)details<<juce::String(match.similarity*100.0,1)<<"%  "<<match.file.getFullPathName()<<"\n";status.setText(magdrum::ui::localisedText("%1 near-duplicate candidates found.").replace("%1",juce::String(matches.size())),juce::dontSendNotification);juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Near-duplicate candidates"),matches.empty()?magdrum::ui::localisedText("No likely near duplicates found. These are feature-based candidates; listen before deleting anything."):details+"\n"+magdrum::ui::localisedText("These are feature-based candidates; listen before deleting anything."),magdrum::ui::localisedText("Close"));}else{auto matches=database.findExactDuplicates(f,100);juce::String details;for(const auto&match:matches)details<<match.getFullPathName()<<"\n";status.setText(magdrum::ui::localisedText("%1 exact duplicate files found.").replace("%1",juce::String(matches.size())),juce::dontSendNotification);juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Exact file duplicates"),matches.isEmpty()?magdrum::ui::localisedText("No exact duplicates are indexed."):details,magdrum::ui::localisedText("Close"));}}}
  void chooseMidiImport()
  {
   chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Import a Standard MIDI File"),juce::File(),"*.mid;*.midi");
   juce::Component::SafePointer<MainComponent>safe(this);
   chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c)
   {
    if(safe==nullptr)return;
    const auto file=c.getResult();if(!file.existsAsFile())return;
    MidiPattern pattern;auto report=MidiPatternIO{}.read(file,safe->mappingProfile,pattern);
    if(!report.ok){safe->status.setText(report.error,juce::dontSendNotification);return;}
    safe->midiPattern=std::move(pattern);safe->midiBaseline=safe->midiPattern;safe->hasMidiImportBaseline=true;
    std::map<int,int> unmappedCounts;for(const auto note:report.unmappedNotes)++unmappedCounts[note];
    juce::String details;
    for(const auto&[note,count]:unmappedCounts)details<<magdrum::ui::localisedText("Unmapped MIDI note %1: %2 event(s).\n").replace("%1",juce::String(note)).replace("%2",juce::String(count));
    for(const auto&ambiguous:report.ambiguousNotes)
    {
     details<<magdrum::ui::localisedText("Ambiguous MIDI note %1: %2 event(s) imported as ").replace("%1",juce::String(ambiguous.note)).replace("%2",juce::String(ambiguous.eventCount));
     if(!ambiguous.candidates.empty())details<<describeMidiMapping(ambiguous.candidates.front());
     details<<magdrum::ui::localisedText(" (first matching profile entry). Candidates: ");
     for(size_t index=0;index<ambiguous.candidates.size();++index)
     {
      if(index>0)details<<", ";
      details<<describeMidiMapping(ambiguous.candidates[index]);
     }
     details<<".\n";
    }
    safe->status.setText(magdrum::ui::localisedText("%1 drum events imported; %2 unmapped and %3 ambiguous MIDI notes.").replace("%1",juce::String(safe->midiPattern.events.size())).replace("%2",juce::String(report.unmappedNotes.size())).replace("%3",juce::String(report.ambiguousNotes.size())),juce::dontSendNotification);
    if(!details.isEmpty())juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,magdrum::ui::localisedText("MIDI import mapping report"),details,magdrum::ui::localisedText("Close"));
   });
  }
 void chooseMidiExport(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Export a Standard MIDI File"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum.mid"),"*.mid");juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe==nullptr)return;auto f=c.getResult();if(f==juce::File())return;if(f.getFileExtension().isEmpty())f=f.withFileExtension(".mid");std::vector<DrumEvent>unsupported;std::vector<MidiFallbackUse>fallbacks;const bool ok=MidiPatternIO{}.write(f,safe->midiPattern,safe->mappingProfile,static_cast<MidiFallbackPolicy>(juce::jlimit(0,2,safe->midiFallbackPolicy.getSelectedId()-1)),&unsupported,&fallbacks);safe->status.setText(ok?magdrum::ui::localisedText("MIDI exported; %1 fallback mapping(s), %2 event(s) omitted.").replace("%1",juce::String(fallbacks.size())).replace("%2",juce::String(unsupported.size())):magdrum::ui::localisedText("MIDI export failed."),juce::dontSendNotification);if(ok&&(!fallbacks.empty()||!unsupported.empty())){juce::String details;for(const auto&fallback:fallbacks)details<<magdrum::ui::localisedText("Fallback: ")<<describeMidiMapping({fallback.requestedEvent.instrument,fallback.requestedEvent.articulation,fallback.midiNote})<<" mapped to "<<describeMidiMapping({fallback.requestedEvent.instrument,fallback.mappedArticulation,fallback.midiNote})<<" (MIDI note "<<static_cast<int>(fallback.midiNote)<<")\n";for(const auto&event:unsupported)details<<magdrum::ui::localisedText("Omitted: ")<<describeMidiMapping({event.instrument,event.articulation,0})<<", velocity "<<static_cast<int>(event.velocity)<<"\n";juce::AlertWindow::showMessageBoxAsync(unsupported.empty()?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,magdrum::ui::localisedText("MIDI export mapping report"),details,magdrum::ui::localisedText("Close"));}});}
 void chooseProfileImport(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Import a MagDrum mapping profile CSV"),juce::File(),"*.csv");juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe==nullptr)return;const auto file=c.getResult();if(!file.existsAsFile())return;const auto parsed=MidiMappingProfile::fromCsv(file.loadFileAsString().toStdString());if(!parsed){safe->status.setText(magdrum::ui::localisedText("CSV profile is invalid; expected instrument,articulation,note rows."),juce::dontSendNotification);return;}safe->mappingProfile=*parsed;safe->profileName.setText(file.getFileNameWithoutExtension(),juce::dontSendNotification);if(safe->database.saveMappingProfile(safe->profileName.getText(),safe->mappingProfile))safe->status.setText(magdrum::ui::localisedText("Mapping profile imported and saved."),juce::dontSendNotification);else safe->status.setText(magdrum::ui::localisedText("Profile imported but could not be saved to the database."),juce::dontSendNotification);});}
 void chooseProfileExport(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Export mapping profile CSV"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(profileName.getText()+".csv"),"*.csv");juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe==nullptr)return;auto file=c.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=file.withFileExtension(".csv");if(file.replaceWithText(safe->mappingProfile.toCsv()))safe->status.setText(magdrum::ui::localisedText("Mapping profile exported as CSV."),juce::dontSendNotification);else safe->status.setText(magdrum::ui::localisedText("Could not write mapping profile CSV."),juce::dontSendNotification);});}
  void chooseMetadataExport(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Export MagDrum metadata archive"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Metadata.json"),"*.json");juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe==nullptr)return;auto file=c.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=file.withFileExtension(".json");safe->status.setText(magdrum::ui::localisedText("Exporting metadata archive…"),juce::dontSendNotification);safe->metadataWorkers.addJob(new PortableMetadataJob(*safe,safe->databaseFile,file,false),true);});}
  void chooseMetadataImport(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Import MagDrum metadata archive"),juce::File(),"*.json");juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser&c){if(safe==nullptr)return;const auto file=c.getResult();if(!file.existsAsFile())return;safe->status.setText(magdrum::ui::localisedText("Merging metadata archive…"),juce::dontSendNotification);safe->metadataWorkers.addJob(new PortableMetadataJob(*safe,safe->databaseFile,file,true),true);});}
  void chooseMetadataSyncFolder(){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose a folder for metadata sync"),juce::File(),"",true,false,this);juce::Component::SafePointer<MainComponent>safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser&c){if(safe==nullptr)return;const auto folder=c.getResult();if(!folder.isDirectory())return;safe->status.setText(magdrum::ui::localisedText("Merging metadata archive…"),juce::dontSendNotification);safe->metadataWorkers.addJob(new MetadataSyncJob(*safe,safe->databaseFile,folder),true);});}
  void finishPortableMetadataSync(const PortableMetadataSyncResult& result){if(!result.ok){const auto error=result.metadataMerged?magdrum::ui::localisedText("Metadata merged, but the device snapshot could not be saved. Check folder permissions and sync again."):magdrum::ui::localisedText("Metadata folder sync failed. Check the folder and try again.");status.setText(error,juce::dontSendNotification);return;}finishPortableMetadataImport(result);const auto summary=magdrum::ui::localisedText("Metadata sync saved. Merged {devices} other device(s); {matched} sample matches, {unavailable} unavailable, {tags} new tags, {profiles} new profiles.").replace("{devices}",juce::String(result.archivesMerged)).replace("{matched}",juce::String(result.samplesMatched)).replace("{unavailable}",juce::String(result.samplesUnavailable)).replace("{tags}",juce::String(result.tagsAdded)).replace("{profiles}",juce::String(result.profilesAdded));status.setText(summary,juce::dontSendNotification);}
  void finishPortableMetadataImport(const PortableMetadataMergeResult& result){if(!result.ok)return;const auto summary=magdrum::ui::localisedText("Metadata merged: {matched} sample identities matched, {unavailable} unavailable, {tags} tags added, {profiles} profiles added.").replace("{matched}",juce::String(result.samplesMatched)).replace("{unavailable}",juce::String(result.samplesUnavailable)).replace("{tags}",juce::String(result.tagsAdded)).replace("{profiles}",juce::String(result.profilesAdded));status.setText(summary,juce::dontSendNotification);refresh();if(!result.conflicts.empty())
  {
   juce::Component::SafePointer<MainComponent> safe(this);
   magdrum::ui::showPortableMetadataConflictReview(this,result.conflicts,
    [safe](const PortableMetadataConflict& conflict,const PortableMetadataConflictCandidate& choice)
    {return safe!=nullptr&&safe->database.resolvePortableMetadataConflict(conflict,choice);},
    [safe](int applied,int failed,int skipped)
    {
     if(safe==nullptr)return;
     safe->refresh();
     safe->status.setText(magdrum::ui::localisedText("Metadata conflict review: {applied} applied, {failed} failed, {skipped} left unchanged.")
      .replace("{applied}",juce::String(applied)).replace("{failed}",juce::String(failed)).replace("{skipped}",juce::String(skipped)),
      juce::dontSendNotification);
    });
  }
  else if(result.samplesUnavailable>0)
  {
   const auto report=magdrum::ui::localisedText("Sample identities not found on this device: {count}")
    .replace("{count}",juce::String(result.samplesUnavailable))+"\n\n"
    +magdrum::ui::localisedText("Conflicting local values were not overwritten. No audio files or plugin states were imported.");
   juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
    magdrum::ui::localisedText("Metadata import report"),report,magdrum::ui::localisedText("Close"));
  }}
 void startScan(const juce::File&directory){scanning=true;sampleScanCancellation.store(false);scan.setButtonText(magdrum::ui::localisedText("Cancel scan"));status.setText(magdrum::ui::localisedText("Scanning sample folder…"),juce::dontSendNotification);workers.addJob(new ScanJob(*this,directory,databaseFile,sampleScanCancellation),true);}
 void finishScan(const WavScanner::Stats&stats,std::int64_t indexed,const juce::String&error){scanning=false;scan.setEnabled(true);scan.setButtonText(magdrum::ui::localisedText("Add sample folder"));if(error.isNotEmpty())status.setText(magdrum::ui::localisedText(error),juce::dontSendNotification);else{const auto summary=magdrum::ui::localisedText(stats.cancelled?"Scan cancelled: %1 indexed, %2 unchanged, %3 errors.":"Scan complete: %1 indexed, %2 unchanged, %3 errors.").replace("%1",juce::String(indexed)).replace("%2",juce::String(stats.filesSkippedUnchanged)).replace("%3",juce::String(stats.errors));status.setText(summary,juce::dontSendNotification);}refresh();}
 void playPatternPreview(){if(midiPattern.events.empty()){status.setText(magdrum::ui::localisedText("Add one or more hits to the sequencer before previewing."),juce::dontSendNotification);return;}if(patternPreviewCancellation)patternPreviewCancellation->store(true);patternPreviewCancellation=std::make_shared<std::atomic<bool>>(false);const auto generation=++patternPreviewGeneration;std::array<juce::File,9> sampleFiles;const juce::String categories[]{"Kick","Snare","Closed hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(size_t index=0;index<sampleFiles.size();++index)if(const auto sample=database.getFirstSampleForCategory(categories[index]))sampleFiles[index]=*sample;const auto output=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrum").getChildFile("pattern-preview-"+juce::String(juce::Uuid().toString())+".wav");transport.stop();status.setText(magdrum::ui::localisedText("Rendering a tempo-aware drum preview from indexed samples…"),juce::dontSendNotification);workers.addJob(new PatternPreviewJob(*this,midiPattern,sampleFiles,output,patternPreviewCancellation,generation),true);}
 void finishPatternPreview(std::uint64_t generation,const juce::File&output,const PatternAudioRenderResult&result,const juce::String&detail){if(generation!=patternPreviewGeneration){output.deleteFile();return;}if(!result.ok){output.deleteFile();auto error=result.error;if(error=="Preview cancelled.")error=magdrum::ui::localisedText("Preview cancelled.");else if(error=="No matching drum samples are indexed. Add and scan a sample folder first.")error=magdrum::ui::localisedText(error);status.setText(magdrum::ui::localisedText("Sequencer preview failed: ")+error,juce::dontSendNotification);return;}auto* reader=formats.createReaderFor(output);if(reader==nullptr){output.deleteFile();status.setText(magdrum::ui::localisedText("The rendered preview could not be opened."),juce::dontSendNotification);return;}transport.stop();transport.setSource(nullptr);source.reset();patternLoopSource=nullptr;if(patternPreviewFile.existsAsFile())patternPreviewFile.deleteFile();patternPreviewFile=output;auto loopSource=std::make_unique<LoopingAudioSource>(reader);loopSource->setLooping(patternPreviewLooping);patternLoopSource=loopSource.get();source=std::move(loopSource);transport.setSource(source.get(),0,nullptr,reader->sampleRate);transport.start();const auto localizedDetail=detail==" Some drum lanes have no indexed sample, so those hits are silent."?" "+magdrum::ui::localisedText("Some drum lanes have no indexed sample, so those hits are silent."):detail;status.setText(magdrum::ui::localisedText("Playing sequencer preview")+" ("+juce::String(result.durationSeconds,1)+" s"+(patternPreviewLooping?", "+magdrum::ui::localisedText("looping"):"")+")."+localizedDetail,juce::dontSendNotification);}
 void setPatternPreviewLooping(bool enabled){patternPreviewLooping=enabled;if(patternLoopSource!=nullptr)patternLoopSource->setLooping(enabled);}
 void showScanHistory(){const auto records=database.getRecentSampleScans();if(records.isEmpty()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Sample scan history"),magdrum::ui::localisedText("No sample scans have been recorded yet."));return;}juce::String message;for(const auto&record:records){message<<record.startedAt<<"  |  "<<record.status<<"\n"<<record.rootPath<<"\n"<<record.filesIndexed<<" indexed, "<<record.filesSkipped<<" unchanged, "<<record.errors<<" errors\n\n";}juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("Recent sample scans"),message,magdrum::ui::localisedText("Close"));}
 void showSampleComparison(){const auto selected=rows.getSelectedRows();if(selected.size()<2){status.setText(magdrum::ui::localisedText("Select two samples with Ctrl/Command-click to compare their waveforms."),juce::dontSendNotification);return;}const int first=selected[0],second=selected[1];if(!juce::isPositiveAndBelow(first,visible.size())||!juce::isPositiveAndBelow(second,visible.size()))return;comparisonWindow.reset();comparisonWindow=std::make_unique<ComparisonWindow>(*this,visible.getReference(first),visible.getReference(second));}
 void refresh(bool updateFilterChoices=true)
 {
  stopTimer();
  if(updateFilterChoices){const auto priorSource=sourceFilter.getText(),priorKit=kitFilter.getText();const auto plugins=database.getPluginInstruments();const auto kitNames=database.getPluginKitNames();juce::StringArray pluginNames;
  for(const auto& plugin:plugins)pluginNames.addIfNotAlreadyThere(plugin.name);
  sourceFilter.clear(juce::dontSendNotification);sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.addItem(magdrum::ui::localisedText("Sample folders"),2);int sourceId=3;for(const auto& name:pluginNames){sourceFilter.addItem(name,sourceId);if(name==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}for(const auto& folder:database.getSampleSources("folder")){const auto label=magdrum::ui::localisedText("Folder: ")+folder.identifier;sourceFilter.addItem(label,sourceId);if(label==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
  if(priorSource==magdrum::ui::localisedText("Sample folders"))sourceFilter.setSelectedId(2,juce::dontSendNotification);else if(priorSource==magdrum::ui::localisedText("All sources")||priorSource.isEmpty()||sourceFilter.getSelectedId()==0)sourceFilter.setSelectedId(1,juce::dontSendNotification);
  kitFilter.clear(juce::dontSendNotification);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);int kitId=2;for(const auto& name:kitNames){kitFilter.addItem(name,kitId);if(name==priorKit)kitFilter.setSelectedId(kitId,juce::dontSendNotification);++kitId;}if(priorKit==magdrum::ui::localisedText("All kits")||priorKit.isEmpty()||kitFilter.getSelectedId()==0)kitFilter.setSelectedId(1,juce::dontSendNotification);}
  ++browseGeneration;visible.clear();browseRecordsByPath.clear();loadedSampleCount=0;hasMoreSamples=false;browsePagePending=false;rows.updateContent();loadNextSamplePage();
 }
 void loadNextSamplePage()
 {
  if(browsePagePending)return;
  if(!browseDatabaseReady){status.setText(magdrum::ui::localisedText("The sample index is unavailable for browsing."),juce::dontSendNotification);return;}
  SampleBrowseFilter filterOptions;filterOptions.query=filter.getText();const int categoryId=categoryFilter.getSelectedId();if(categoryId>1){static constexpr const char* categories[]{"All categories","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion","Unknown"};if(juce::isPositiveAndBelow(categoryId-1,13))filterOptions.category=categories[categoryId-1];}filterOptions.feature=characteristicFilter.getSelectedId();filterOptions.sort=sortFilter.getSelectedId();filterOptions.needsReview=confidenceFilter.getSelectedId()==2;
  const auto selectedSource=sourceFilter.getText();if(sourceFilter.getSelectedId()==2)filterOptions.sourceType="folder";else if(selectedSource.startsWith(magdrum::ui::localisedText("Folder: "))){filterOptions.sourceType="folder";filterOptions.sourceIdentifier=selectedSource.fromFirstOccurrenceOf(magdrum::ui::localisedText("Folder: "),false,false);}else if(sourceFilter.getSelectedId()>2){filterOptions.sourceType="plugin";filterOptions.pluginName=selectedSource;}
  const auto selectedKit=kitFilter.getText();if(kitFilter.getSelectedId()>1){filterOptions.sourceType="plugin";if(filterOptions.pluginName.isEmpty())filterOptions.pluginName=selectedKit.upToFirstOccurrenceOf(" / ",false,false);filterOptions.kitName=selectedKit.fromFirstOccurrenceOf(" / ",false,false);}
  browsePagePending=true;browseWorkers.addJob(new BrowsePageJob(*this,browseDatabase,std::move(filterOptions),loadedSampleCount,browseGeneration),true);
 }
 void finishBrowsePage(std::uint64_t generation,int offset,SampleBrowsePage page)
 {
  if(generation!=browseGeneration)return;browsePagePending=false;if(offset!=loadedSampleCount)return;
  for(const auto& record:page.records){visible.add(record.file);browseRecordsByPath.emplace(record.file.getFullPathName().toStdString(),record);}loadedSampleCount+=page.records.size();hasMoreSamples=page.hasMore;rows.updateContent();rows.repaint();
 }
 void saveSelectedTags(){const int row=rows.getSelectedRow();if(!juce::isPositiveAndBelow(row,visible.size())){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}juce::StringArray values;values.addTokens(tags.getText(),",","");if(!database.setUserTags(visible.getReference(row),values)){status.setText(magdrum::ui::localisedText("Could not save the sample tags."),juce::dontSendNotification);return;}status.setText(magdrum::ui::localisedText("Tags saved."),juce::dontSendNotification);refresh(false);}
 void playSelected(){auto n=rows.getSelectedRow();if(!juce::isPositiveAndBelow(n,visible.size())){status.setText(magdrum::ui::localisedText("Select a sample before playback."),juce::dontSendNotification);return;}const auto file=visible.getReference(n);auto*r=formats.createReaderFor(file);if(!r){status.setText(magdrum::ui::localisedText("Could not open this sample. The file may have moved, been deleted or use an unsupported format."),juce::dontSendNotification);StructuredLogger::log(StructuredLogger::Level::warning,"sample_preview.unavailable",{{"path",file.getFullPathName()}});return;}if(patternPreviewCancellation)patternPreviewCancellation->store(true);++patternPreviewGeneration;transport.stop();transport.setSource(nullptr);source.reset();patternLoopSource=nullptr;if(patternPreviewFile.existsAsFile())patternPreviewFile.deleteFile();patternPreviewFile=juce::File();source.reset(new juce::AudioFormatReaderSource(r,true));transport.setSource(source.get(),0,nullptr,r->sampleRate);transport.start();}
 juce::Label title,subtitle,status;juce::TextButton scan,scanHistory,pluginScan,play,openSourceKit,stepSequencer,categorySave,tagsSave,similar,duplicates,perceptualDuplicates,compareSamples,metadataExport,metadataImport,metadataSync,midiImport,midiExport,midiEdit,profileSave,profileLoad,profileImport,profileExport,mappingSet,mappingRemove;juce::ComboBox midiFallbackPolicy;juce::TextEditor filter,tags,profileName,mappingNote;juce::ComboBox categoryFilter,sourceFilter,kitFilter,sortFilter,characteristicFilter,confidenceFilter,categoryEdit,mappingInstrument,mappingArticulation;juce::ListBox rows;WaveformView waveform;juce::AudioFormatManager formats;juce::AudioPluginFormatManager pluginFormats;juce::KnownPluginList knownPlugins;juce::AudioTransportSource transport;SampleDatabase database,browseDatabase;MidiMappingProfile mappingProfile=MidiMappingProfile::generalMidi();MidiPattern midiPattern,midiBaseline;bool hasMidiImportBaseline=false,audioEnabled=true;juce::Array<juce::File>visible;std::unordered_map<std::string,SampleBrowseRecord>browseRecordsByPath;juce::File pluginListFile,pluginCrashList,databaseFile,patternPreviewFile;juce::String selectedPluginIdentifier,selectedPluginName,selectedPluginFormat;juce::PluginDescription selectedPluginDescription;std::uint64_t pluginLoadGeneration=0,patternPreviewGeneration=0,browseGeneration=0;std::shared_ptr<std::atomic<bool>>patternPreviewCancellation;std::unique_ptr<PluginWindow>pluginWindow;std::unique_ptr<MidiPatternWindow>midiPatternWindow;std::unique_ptr<StepSequencerWindow>stepSequencerWindow;std::unique_ptr<PluginEditorWindow>pluginEditorWindow;std::unique_ptr<ComparisonWindow>comparisonWindow;std::unique_ptr<juce::AudioPluginInstance>selectedPlugin;std::unique_ptr<juce::PositionableAudioSource>source;LoopingAudioSource*patternLoopSource=nullptr;std::unique_ptr<juce::FileChooser>chooser;juce::ThreadPool workers{1},browseWorkers{1},metadataWorkers{1};std::atomic<bool>pluginScanCancellation{false},pluginScanRunning{false},sampleScanCancellation{false};int loadedSampleCount=0;bool browseDatabaseReady=false,browsePagePending=false,hasMoreSamples=false,scanning=false,patternPreviewLooping=false;
 void refreshLanguageFilters(){const int categoryId=categoryFilter.getSelectedId(),categoryEditId=categoryEdit.getSelectedId(),sortId=sortFilter.getSelectedId(),featureId=characteristicFilter.getSelectedId(),confidenceId=confidenceFilter.getSelectedId(),instrumentId=mappingInstrument.getSelectedId(),articulationId=mappingArticulation.getSelectedId();categoryFilter.clear(juce::dontSendNotification);const char* cats[]{"All categories","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion","Unknown"};for(int i=0;i<13;++i)categoryFilter.addItem(magdrum::ui::localisedText(cats[i]),i+1);categoryFilter.setSelectedId(categoryId,juce::dontSendNotification);categoryEdit.clear(juce::dontSendNotification);const char* editCategories[]={"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<12;++i)categoryEdit.addItem(magdrum::ui::localisedText(editCategories[i]),i+1);categoryEdit.setSelectedId(categoryEditId,juce::dontSendNotification);sortFilter.clear(juce::dontSendNotification);const char* sorts[]{"Name A-Z","Duration shortest","Duration longest","Brightest first","Darkest first"};for(int i=0;i<5;++i)sortFilter.addItem(magdrum::ui::localisedText(sorts[i]),i+1);sortFilter.setSelectedId(sortId,juce::dontSendNotification);characteristicFilter.clear(juce::dontSendNotification);const char* features[]{"All sound features","Very short (<100 ms)","Short (100–500 ms)","Medium (0.5–2 s)","Long (≥2 s)","Dark (<1 kHz)","Mid (1–3 kHz)","Bright (≥3 kHz)"};for(int i=0;i<8;++i)characteristicFilter.addItem(magdrum::ui::localisedText(features[i]),i+1);characteristicFilter.setSelectedId(featureId,juce::dontSendNotification);confidenceFilter.clear(juce::dontSendNotification);confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review (<75% or unknown)"),2);confidenceFilter.setSelectedId(confidenceId,juce::dontSendNotification);mappingInstrument.clear(juce::dontSendNotification);const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<9;++i)mappingInstrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);mappingInstrument.setSelectedId(instrumentId,juce::dontSendNotification);mappingArticulation.clear(juce::dontSendNotification);const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};for(int i=0;i<13;++i)mappingArticulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);mappingArticulation.setSelectedId(articulationId,juce::dontSendNotification);}
 magdrum::ui::MenuIconButton languageButton;
#if 0 // replaced by the consolidated live-language refresh below
 void applyLanguage(){status.setText(juce::String(),juce::dontSendNotification);languageButton.setTooltip(magdrum::ui::localisedText("Menu"));subtitle.setText(magdrum::ui::localisedText("Your drum libraries, in one place."),juce::dontSendNotification);scan.setButtonText(magdrum::ui::localisedText(scanning?"Cancel scan":"Add sample folder"));scanHistory.setButtonText(magdrum::ui::localisedText("Scan history"));pluginScan.setButtonText(magdrum::ui::localisedText("Plugin instruments"));play.setButtonText(magdrum::ui::localisedText("Play selected"));openSourceKit.setButtonText(magdrum::ui::localisedText("Open source kit"));stepSequencer.setButtonText(magdrum::ui::localisedText("Step sequencer"));filter.setTextToShowWhenEmpty(magdrum::ui::localisedText("Filter by filename, folder or tag"),magdrum::ui::Palette::Muted);tags.setTextToShowWhenEmpty(magdrum::ui::localisedText("Tags, comma separated"),magdrum::ui::Palette::Muted);categorySave.setButtonText(magdrum::ui::localisedText("Set category"));tagsSave.setButtonText(magdrum::ui::localisedText("Save tags"));similar.setButtonText(magdrum::ui::localisedText("Find similar"));duplicates.setButtonText(magdrum::ui::localisedText("Find exact duplicates"));perceptualDuplicates.setButtonText(magdrum::ui::localisedText("Find near duplicates"));midiImport.setButtonText(magdrum::ui::localisedText("Import MIDI"));midiExport.setButtonText(magdrum::ui::localisedText("Export MIDI"));midiEdit.setButtonText(magdrum::ui::localisedText("Edit pattern"));const int fallbackPolicyId=juce::jlimit(1,3,midiFallbackPolicy.getSelectedId());midiFallbackPolicy.clear(juce::dontSendNotification);midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);midiFallbackPolicy.setSelectedId(fallbackPolicyId,juce::dontSendNotification);midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));profileSave.setButtonText(magdrum::ui::localisedText("Save profile"));profileLoad.setButtonText(magdrum::ui::localisedText("Load profile"));profileImport.setButtonText(magdrum::ui::localisedText("Import CSV"));profileExport.setButtonText(magdrum::ui::localisedText("Export CSV"));mappingSet.setButtonText(magdrum::ui::localisedText("Set mapping"));mappingRemove.setButtonText(magdrum::ui::localisedText("Remove mapping"));compareSamples.setButtonText(magdrum::ui::localisedText("Compare selected"));metadataExport.setButtonText(magdrum::ui::localisedText("Export metadata"));metadataImport.setButtonText(magdrum::ui::localisedText("Import metadata"));metadataSync.setButtonText(magdrum::ui::localisedText("Sync metadata"));rows.updateContent();waveform.repaint();resized();}
 void applyLanguage(){status.setText(juce::String(),juce::dontSendNotification);languageButton.setTooltip(magdrum::ui::localisedText("Menu"));subtitle.setText(magdrum::ui::localisedText("Your drum libraries, in one place."),juce::dontSendNotification);scan.setButtonText(magdrum::ui::localisedText(scanning?"Cancel scan":"Add sample folder"));scanHistory.setButtonText(magdrum::ui::localisedText("Scan history"));pluginScan.setButtonText(magdrum::ui::localisedText("Plugin instruments"));play.setButtonText(magdrum::ui::localisedText("Play selected"));openSourceKit.setButtonText(magdrum::ui::localisedText("Open source kit"));stepSequencer.setButtonText(magdrum::ui::localisedText("Step sequencer"));filter.setTextToShowWhenEmpty(magdrum::ui::localisedText("Filter by filename, folder or tag"),magdrum::ui::Palette::Muted);tags.setTextToShowWhenEmpty(magdrum::ui::localisedText("Tags, comma separated"),magdrum::ui::Palette::Muted);categorySave.setButtonText(magdrum::ui::localisedText("Set category"));tagsSave.setButtonText(magdrum::ui::localisedText("Save tags"));similar.setButtonText(magdrum::ui::localisedText("Find similar"));duplicates.setButtonText(magdrum::ui::localisedText("Find exact duplicates"));perceptualDuplicates.setButtonText(magdrum::ui::localisedText("Find near duplicates"));midiImport.setButtonText(magdrum::ui::localisedText("Import MIDI"));midiExport.setButtonText(magdrum::ui::localisedText("Export MIDI"));midiEdit.setButtonText(magdrum::ui::localisedText("Edit pattern"));const int fallbackPolicyId=juce::jlimit(1,3,midiFallbackPolicy.getSelectedId());midiFallbackPolicy.clear(juce::dontSendNotification);midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);midiFallbackPolicy.setSelectedId(fallbackPolicyId,juce::dontSendNotification);midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));profileSave.setButtonText(magdrum::ui::localisedText("Save profile"));profileLoad.setButtonText(magdrum::ui::localisedText("Load profile"));profileImport.setButtonText(magdrum::ui::localisedText("Import CSV"));profileExport.setButtonText(magdrum::ui::localisedText("Export CSV"));mappingSet.setButtonText(magdrum::ui::localisedText("Set mapping"));mappingRemove.setButtonText(magdrum::ui::localisedText("Remove mapping"));compareSamples.setButtonText(magdrum::ui::localisedText("Compare selected"));metadataExport.setButtonText(magdrum::ui::localisedText("Export metadata"));metadataImport.setButtonText(magdrum::ui::localisedText("Import metadata"));rows.updateContent();waveform.repaint();resized();}
#endif
 void applyLanguage(){status.setText(juce::String(),juce::dontSendNotification);languageButton.setTooltip(magdrum::ui::localisedText("Language"));subtitle.setText(magdrum::ui::localisedText("Your drum libraries, in one place."),juce::dontSendNotification);scan.setButtonText(magdrum::ui::localisedText(scanning?"Cancel scan":"Add sample folder"));scanHistory.setButtonText(magdrum::ui::localisedText("Scan history"));pluginScan.setButtonText(magdrum::ui::localisedText("Plugin instruments"));play.setButtonText(magdrum::ui::localisedText("Play selected"));openSourceKit.setButtonText(magdrum::ui::localisedText("Open source kit"));stepSequencer.setButtonText(magdrum::ui::localisedText("Step sequencer"));filter.setTextToShowWhenEmpty(magdrum::ui::localisedText("Filter by filename, folder or tag"),magdrum::ui::Palette::Muted);tags.setTextToShowWhenEmpty(magdrum::ui::localisedText("Tags, comma separated"),magdrum::ui::Palette::Muted);categorySave.setButtonText(magdrum::ui::localisedText("Set category"));tagsSave.setButtonText(magdrum::ui::localisedText("Save tags"));similar.setButtonText(magdrum::ui::localisedText("Find similar"));duplicates.setButtonText(magdrum::ui::localisedText("Find exact duplicates"));perceptualDuplicates.setButtonText(magdrum::ui::localisedText("Find near duplicates"));midiImport.setButtonText(magdrum::ui::localisedText("Import MIDI"));midiExport.setButtonText(magdrum::ui::localisedText("Export MIDI"));midiEdit.setButtonText(magdrum::ui::localisedText("Edit pattern"));const int fallbackPolicyId=juce::jlimit(1,3,midiFallbackPolicy.getSelectedId());midiFallbackPolicy.clear(juce::dontSendNotification);midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);midiFallbackPolicy.setSelectedId(fallbackPolicyId,juce::dontSendNotification);midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));profileSave.setButtonText(magdrum::ui::localisedText("Save profile"));profileLoad.setButtonText(magdrum::ui::localisedText("Load profile"));profileImport.setButtonText(magdrum::ui::localisedText("Import CSV"));profileExport.setButtonText(magdrum::ui::localisedText("Export CSV"));mappingSet.setButtonText(magdrum::ui::localisedText("Set mapping"));mappingRemove.setButtonText(magdrum::ui::localisedText("Remove mapping"));compareSamples.setButtonText(magdrum::ui::localisedText("Compare selected"));metadataExport.setButtonText(magdrum::ui::localisedText("Export metadata"));metadataImport.setButtonText(magdrum::ui::localisedText("Import metadata"));if(comparisonWindow)comparisonWindow->setName(magdrum::ui::localisedText("Compare samples"));if(midiPatternWindow)midiPatternWindow->setName(magdrum::ui::localisedText("Edit MIDI pattern"));if(stepSequencerWindow)stepSequencerWindow->setName(magdrum::ui::localisedText("Drum step sequencer"));if(pluginWindow)pluginWindow->setName(magdrum::ui::localisedText("Plugin instruments"));resized();}
 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
void MainComponent::openPluginBrowser(){if(pluginWindow==nullptr)pluginWindow=std::make_unique<PluginWindow>(*this);pluginWindow->toFront(true);status.setText(juce::String(database.getPluginInstruments().size())+" plugin instruments indexed.",juce::dontSendNotification);}
int runIsolatedVst3Discovery(const juce::File& progressFile, const juce::File& additionalFolder = {})
{
    if (additionalFolder != juce::File() && ! additionalFolder.isDirectory())
        return 7;

    const auto dataDirectory = getMagDrumDataDirectory();
    if ((! dataDirectory.isDirectory()) && ! dataDirectory.createDirectory().wasOk())
        return 2;

    magdrum::plugins::Vst3DiscoveryProgress progressState;
    magdrum::plugins::readVst3DiscoveryProgress(progressFile, progressState);
    const int progressBase = progressState.completed;
    const int progressTotal = progressState.total;
    const auto publishProgress = [&progressFile, &progressState](int completed, int total,
        const juce::String& current, const juce::String& currentPath)
    {
        progressState.completed = completed;
        progressState.total = total;
        progressState.current = current;
        progressState.currentPath = currentPath;
        magdrum::plugins::writeVst3DiscoveryProgress(progressFile, progressState);
    };
    publishProgress(progressBase, progressTotal, "discovering", {});

    const auto inventoryFile = dataDirectory.getChildFile("vst3-plugins.xml");
    juce::KnownPluginList knownPlugins;
    if (inventoryFile.existsAsFile())
    {
        const auto xml = juce::XmlDocument::parse(inventoryFile);
        if (xml == nullptr)
            return 3;
        knownPlugins.recreateFromXml(*xml);
    }

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager(formats);
    juce::AudioPluginFormat* vst3 = nullptr;
    for (int index = 0; index < formats.getNumFormats(); ++index)
        if (auto* format = formats.getFormat(index); format != nullptr && format->getName() == "VST3")
        { vst3 = format; break; }
    if (vst3 == nullptr)
        return 4;

    const auto searchPath = magdrum::plugins::createVst3DiscoverySearchPath(
        vst3->getDefaultLocationsToSearch(), additionalFolder);
    const auto previouslyDiscoveredTypes = knownPlugins.getTypes();
    const auto cachedModuleIndex = magdrum::plugins::indexCachedVst3Modules(
        previouslyDiscoveredTypes);
    const auto moduleFilterCacheFile = dataDirectory.getChildFile("vst3-module-filter-cache.xml");
    auto moduleFilterCache = magdrum::plugins::loadVst3ModuleInstrumentCache(moduleFilterCacheFile);
    juce::StringArray instrumentCandidates;
    const auto candidates = vst3->searchPathsForPlugins(searchPath, true, false);
    for (const auto& candidate : candidates)
    {
        const auto module = juce::File(candidate);
        const auto cachedModule = cachedModuleIndex.find(module.getFullPathName().toStdString());
        const bool cachedBinaryIsUpToDate = cachedModule != cachedModuleIndex.end()
            && magdrum::plugins::isCachedVst3ModuleUpToDate(cachedModule->second, module);
        const bool hasSidecarManifest = module.getChildFile("Contents").getChildFile("Resources")
            .getChildFile("moduleinfo.json").existsAsFile()
            || module.getChildFile("Contents").getChildFile("moduleinfo.json").existsAsFile();
        if (hasSidecarManifest)
        {
            const auto action = magdrum::plugins::classifyVst3ModuleForDiscovery(
                moduleFilterCache, module, cachedBinaryIsUpToDate);
            if (action == magdrum::plugins::Vst3ModuleDiscoveryAction::ExcludeEffect)
            {
                // A manifest can change independently of the bundle's main-file timestamp.
                // Remove any stale instrument descriptions when it now identifies an effect.
                for (const auto& description : knownPlugins.getTypes())
                    if (description.pluginFormatName == "VST3"
                        && description.fileOrIdentifier == candidate)
                        knownPlugins.removeType(description);
                continue;
            }
            if (action == magdrum::plugins::Vst3ModuleDiscoveryAction::ScanInstrument)
                instrumentCandidates.add(candidate);
            continue;
        }

        if (magdrum::plugins::cachedVst3ModuleMayBeInstrument(moduleFilterCache, module)
                == std::optional<bool>(false))
            continue;

        if (cachedBinaryIsUpToDate && cachedModule->second.hasPluginDescriptions
            && !cachedModule->second.hasInstrumentDescription)
            continue;

        if (cachedBinaryIsUpToDate)
            continue;

        // Single-file VST3 modules do not expose a sidecar moduleinfo.json,
        // so keep them eligible for isolated discovery when their cache is stale.
        // A stale cache entry must not hide a module whose classes changed.
        // Sidecar metadata can still cheaply reject effects; legacy bundles are
        // kept eligible for JUCE discovery.
        if (magdrum::plugins::vst3ModuleMayBeInstrument(module))
            instrumentCandidates.add(candidate);
    }
    for (int index = instrumentCandidates.size(); --index >= 0;)
    {
        const juce::File candidate(instrumentCandidates[index]);
        const auto failed = std::find_if(progressState.failedModules.begin(), progressState.failedModules.end(),
            [&candidate](const juce::String& failedPath)
            { return failedPath.equalsIgnoreCase(candidate.getFullPathName()); });
        if (failed != progressState.failedModules.end())
            instrumentCandidates.remove(index);
    }
    // Persist effect-module exclusions before loading any candidate binary, so
    // a crash or cancellation during discovery still accelerates the next pass.
    magdrum::plugins::saveVst3ModuleInstrumentCache(moduleFilterCacheFile, moduleFilterCache);
    const int overallTotal = juce::jmax(progressTotal,
        progressBase + instrumentCandidates.size());
    publishProgress(progressBase, overallTotal, "scanning", {});
    juce::PluginDirectoryScanner scanner(knownPlugins, *vst3,
        juce::FileSearchPath{}, true,
        dataDirectory.getChildFile("RecentlyCrashedPluginsList"));
    scanner.setFilesOrIdentifiersToScan(instrumentCandidates);
    for (int completed = 0; completed < instrumentCandidates.size(); ++completed)
    {
        const auto candidatePath = juce::File(instrumentCandidates[completed]);
        const auto candidateName = candidatePath.getFileNameWithoutExtension();
        publishProgress(progressBase + completed, overallTotal, candidateName, candidatePath.getFullPathName());
        juce::String name;
        scanner.scanNextFile(true, name);
        const bool candidateHasSidecarManifest = candidatePath.getChildFile("Contents")
            .getChildFile("Resources").getChildFile("moduleinfo.json").existsAsFile()
            || candidatePath.getChildFile("Contents").getChildFile("moduleinfo.json").existsAsFile();
        if (!candidateHasSidecarManifest)
        {
            const auto moduleHasInstrument = magdrum::plugins::vst3ModuleHasInstrumentDescription(
                knownPlugins, candidatePath);
            magdrum::plugins::rememberVst3ModuleInstrumentStatus(moduleFilterCache, candidatePath,
                moduleHasInstrument);
            magdrum::plugins::saveVst3ModuleInstrumentCache(moduleFilterCacheFile, moduleFilterCache);
        }
        // Commit discoveries incrementally. If a third-party binary crashes
        // this helper, a later scan keeps completed results and the JUCE
        // dead-man's pedal skips the module that was being scanned.
        auto xml = knownPlugins.createXml();
        if (xml == nullptr || !xml->writeTo(inventoryFile))
            return 5;
        publishProgress(progressBase + completed + 1, overallTotal, name, {});
    }
    publishProgress(overallTotal, overallTotal, "complete", {});

    if (auto xml = knownPlugins.createXml())
        return xml->writeTo(inventoryFile) ? 0 : 5;
    return 6;
}

int runIsolatedVst3PresetWorker(const juce::File& requestFile)
{
    const auto requestValue = juce::JSON::parse(requestFile.loadFileAsString());
    if (! requestValue.isObject())
        return 2;
    const auto* request = requestValue.getDynamicObject();
    const auto descriptionFile = juce::File(request->getProperty("descriptionFile").toString());
    const auto presetFile = juce::File(request->getProperty("presetFile").toString());
    const auto stateFile = juce::File(request->getProperty("stateFile").toString());
    const auto resultFile = juce::File(request->getProperty("resultFile").toString());
    const auto operation = request->getProperty("operation").toString();
    const auto workingDirectory = requestFile.getParentDirectory();
    const auto writeResult = [&resultFile](bool ok, const juce::String& message)
    {
        auto* result = new juce::DynamicObject();
        result->setProperty("ok", ok);
        result->setProperty("message", message);
        return resultFile.replaceWithText(juce::JSON::toString(juce::var(result), true));
    };
    if (descriptionFile.getParentDirectory() != workingDirectory
        || stateFile.getParentDirectory() != workingDirectory
        || resultFile.getParentDirectory() != workingDirectory
        || (operation != "enumeratePrograms" && operation != "loadProgram" && operation != "loadPreset")
        || (operation == "loadPreset" && (! presetFile.existsAsFile()
            || ! presetFile.getFileExtension().equalsIgnoreCase(".vstpreset")
            || presetFile.getSize() <= 0 || presetFile.getSize() > 512LL * 1024LL * 1024LL)))
        return 3;

    const auto xml = juce::XmlDocument::parse(descriptionFile);
    juce::PluginDescription description;
    if (xml == nullptr || ! description.loadFromXml(*xml)
        || description.pluginFormatName != "VST3" || ! description.isInstrument)
    {
        writeResult(false, magdrum::ui::localisedText("The selected VST3 instrument description is invalid."));
        return 4;
    }

    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    juce::String loadError;
    auto plugin = formats.createPluginInstance(description, 44100.0, 512, loadError);
    if (plugin == nullptr)
    {
        writeResult(false, magdrum::ui::localisedText("Could not load the instrument in the isolated helper: ") + loadError);
        return 5;
    }

    if (operation == "enumeratePrograms")
    {
        const auto discovered = discoverPluginPrograms(*plugin);
        if (discovered.exceedsLimit)
        {
            writeResult(false, magdrum::ui::localisedText("This instrument exposes too many programs to list here."));
            return 10;
        }
        juce::Array<juce::var> programs;
        for (const auto& program : discovered.programs)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty("index", program.index);
            item->setProperty("name", program.name);
            programs.add(juce::var(item));
        }
        auto* result = new juce::DynamicObject();
        result->setProperty("ok", true);
        result->setProperty("message", juce::String());
        result->setProperty("programs", programs);
        return resultFile.replaceWithText(juce::JSON::toString(juce::var(result), true)) ? 0 : 9;
    }

    if (operation == "loadProgram")
    {
        const int programIndex = request->getProperty("programIndex");
        if (! juce::isPositiveAndBelow(programIndex, plugin->getNumPrograms()))
        {
            writeResult(false, magdrum::ui::localisedText("Choose a valid program from this instrument first."));
            return 11;
        }
        plugin->setCurrentProgram(programIndex);
    }

    juce::MemoryBlock preset;
    if (operation == "loadPreset" && (! presetFile.loadFileAsData(preset) || preset.isEmpty()))
    {
        writeResult(false, magdrum::ui::localisedText("The selected VST3 preset could not be read."));
        return 6;
    }
    struct PresetLoader final : juce::ExtensionsVisitor
    {
        explicit PresetLoader(const juce::MemoryBlock& presetData) : data(presetData) {}
        void visitVST3Client(const VST3Client& client) override
        {
            supported = true;
            loaded = client.setPreset(data);
        }
        const juce::MemoryBlock& data;
        bool supported = false;
        bool loaded = false;
    } loader(preset);
    if (operation == "loadPreset")
        plugin->getExtensions(loader);
    if (operation == "loadPreset" && (! loader.supported || ! loader.loaded))
    {
        writeResult(false, magdrum::ui::localisedText("This preset could not be loaded by the selected VST3 instrument."));
        return 7;
    }

    plugin->updateHostDisplay();
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    if (state.isEmpty() || ! stateFile.replaceWithData(state.getData(), state.getSize()))
    {
        writeResult(false, magdrum::ui::localisedText("The instrument did not provide a saveable state for this preset."));
        return 8;
    }
    return writeResult(true, {}) ? 0 : 9;
}

#if JUCE_WINDOWS
class HostTransportFixturePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return position; }
    PositionInfo position;
};

int runHostTransportSequencerFixture(const juce::File& module, const juce::File& stateFile)
{
    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager(formats);
    juce::AudioPluginFormat* vst3Format = nullptr;
    for (int index = 0; index < formats.getNumFormats(); ++index)
        if (formats.getFormat(index)->getName() == "VST3")
            vst3Format = formats.getFormat(index);
    if (vst3Format == nullptr) return 40;

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3Format->findAllTypesForFile(descriptions, module.getFullPathName());
    if (descriptions.isEmpty()) return 41;

    juce::String loadError;
    auto plugin = formats.createPluginInstance(*descriptions[0], 48000.0, 128, loadError);
    if (plugin == nullptr) return 42;

    juce::MemoryBlock state;
    if (!stateFile.loadFileAsData(state) || state.isEmpty()) return 43;
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    constexpr double expectedTail = 4096.0 / 44100.0;
    const auto tailDeadline = juce::Time::getMillisecondCounter() + 5000u;
    while (std::abs(plugin->getTailLengthSeconds() - expectedTail) > 0.0001
        && juce::Time::getMillisecondCounter() < tailDeadline)
        juce::Thread::sleep(1);
    if (std::abs(plugin->getTailLengthSeconds() - expectedTail) > 0.0001) return 44;

    HostTransportFixturePlayHead playHead;
    playHead.position.setIsPlaying(true);
    playHead.position.setPpqPosition(0.0);
    playHead.position.setBpm(120.0);
    plugin->setPlayHead(&playHead);
    plugin->prepareToPlay(48000.0, 128);

    const auto isolatedEditorDatabase = stateFile.getSiblingFile("host-sequencer-samples.sqlite3");
    SetEnvironmentVariableW(L"MAGDRUM_TEST_DATABASE_PATH", isolatedEditorDatabase.getFullPathName().toWideCharPointer());
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
    if (editor == nullptr) return 45;
    editor->setSize(1200, 820);
    editor->setVisible(false);
    editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
    auto* peer = editor->getPeer();
    if (peer == nullptr) return 46;
    const auto hwnd = static_cast<HWND>(peer->getNativeHandle());
    if (hwnd == nullptr) return 47;
    const int offscreenX = GetSystemMetrics(SM_XVIRTUALSCREEN) - 1400;
    const int offscreenY = GetSystemMetrics(SM_YVIRTUALSCREEN) - 1000;
    SetWindowPos(hwnd, nullptr, offscreenX, offscreenY, 1200, 820,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    const auto click = [hwnd](int x, int y)
    {
        POINT screenPoint{x, y};
        ClientToScreen(hwnd, &screenPoint);
        const auto sendToWindow = [](HWND target, POINT screen)
        {
            POINT point = screen;
            ScreenToClient(target, &point);
            RECT bounds{};
            if (!GetClientRect(target, &bounds) || point.x < 0 || point.y < 0
                || point.x >= bounds.right || point.y >= bounds.bottom)
                return;
            const auto position = MAKELPARAM(point.x, point.y);
            SendMessageW(target, WM_MOUSEMOVE, 0, position);
            SendMessageW(target, WM_LBUTTONDOWN, MK_LBUTTON, position);
            SendMessageW(target, WM_LBUTTONUP, 0, position);
        };
        sendToWindow(hwnd, screenPoint);
        EnumChildWindows(hwnd, [](HWND child, LPARAM data) -> BOOL
        {
            auto* screen = reinterpret_cast<POINT*>(data);
            POINT point = *screen;
            ScreenToClient(child, &point);
            RECT bounds{};
            if (GetClientRect(child, &bounds) && point.x >= 0 && point.y >= 0
                && point.x < bounds.right && point.y < bounds.bottom)
            {
                const auto position = MAKELPARAM(point.x, point.y);
                SendMessageW(child, WM_MOUSEMOVE, 0, position);
                SendMessageW(child, WM_LBUTTONDOWN, MK_LBUTTON, position);
                SendMessageW(child, WM_LBUTTONUP, 0, position);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&screenPoint));
    };
    click(411, 110); // Sequencer tab in the standard editor layout.
    click(65, 159);  // Play button in the sequencer toolbar.

    constexpr int blockSize=128;
    constexpr int totalSamples=24024+3072;
    constexpr std::array<int,3> hitStarts{24,12024,24024};
    constexpr std::array<double,3> hitFrequencies{440.0,220.0,1760.0};
    struct ToneCorrelation { double real = 0.0; double imaginary = 0.0; };
    std::array<std::array<ToneCorrelation,3>,3> correlations{};
    double beforeFirstHitPeak=0.0;
    juce::AudioBuffer<float> audio(2,blockSize);
    juce::MidiBuffer midi;
    for(int blockStart=0;blockStart<totalSamples;blockStart+=blockSize)
    {
        audio.clear();
        midi.clear();
        playHead.position.setPpqPosition(static_cast<double>(blockStart)/24000.0);
        plugin->processBlock(audio,midi);
        for(int sample=0;sample<blockSize&&blockStart+sample<totalSamples;++sample)
        {
            const int absoluteSample=blockStart+sample;
            double mono=0.0;
            for(int channel=0;channel<audio.getNumChannels();++channel)
                mono+=audio.getSample(channel,sample)/audio.getNumChannels();
            if(absoluteSample<24)
                beforeFirstHitPeak=juce::jmax(beforeFirstHitPeak,std::abs(mono));
            for(size_t eventIndex=0;eventIndex<hitStarts.size();++eventIndex)
            {
                const int relativeSample=absoluteSample-hitStarts[eventIndex];
                if(relativeSample<64||relativeSample>=2048)continue;
                for(size_t frequencyIndex=0;frequencyIndex<hitFrequencies.size();++frequencyIndex)
                {
                    const double phase=2.0*juce::MathConstants<double>::pi*hitFrequencies[frequencyIndex]
                        *relativeSample/48000.0;
                    correlations[eventIndex][frequencyIndex].real+=mono*std::cos(phase);
                    correlations[eventIndex][frequencyIndex].imaginary-=mono*std::sin(phase);
                }
            }
        }
        if(!midi.isEmpty())return 49;
    }
    bool routedEachSemanticLane=true;
    for(size_t eventIndex=0;eventIndex<hitStarts.size();++eventIndex)
    {
        double strongestWrongLane=0.0;
        for(size_t frequencyIndex=0;frequencyIndex<hitFrequencies.size();++frequencyIndex)
            if(frequencyIndex!=eventIndex)
                strongestWrongLane=juce::jmax(strongestWrongLane,std::hypot(
                    correlations[eventIndex][frequencyIndex].real,
                    correlations[eventIndex][frequencyIndex].imaginary));
        const auto targetMagnitude=std::hypot(correlations[eventIndex][eventIndex].real,
            correlations[eventIndex][eventIndex].imaginary);
        routedEachSemanticLane=routedEachSemanticLane&&targetMagnitude>1.0
            &&targetMagnitude>strongestWrongLane*1.5;
    }
    if(beforeFirstHitPeak>1.0e-7||!routedEachSemanticLane)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        stateFile.getSiblingFile("host-sequencer-render.txt").replaceWithText(
            "client=" + juce::String(client.right) + "x" + juce::String(client.bottom)
            + ", before=" + juce::String(beforeFirstHitPeak, 8)
            + ", kick=" + juce::String(std::hypot(correlations[0][0].real,correlations[0][0].imaginary), 8)
            + ", snare=" + juce::String(std::hypot(correlations[1][1].real,correlations[1][1].imaginary), 8)
            + ", closed-hihat=" + juce::String(std::hypot(correlations[2][2].real,correlations[2][2].imaginary), 8));
        return 49;
    }
    return 0;
}
#endif

class MagDrumApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MagDrum"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        magdrum::ui::initialiseMagDrumLocalization();
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel);
        juce::StringArray arguments;
        arguments.addTokens(commandLine, true);
#if JUCE_WINDOWS && JUCE_DEBUG
        if (arguments.size() == 1 && arguments[0] == "--magdrum-debug-assertion-worker")
        {
            makeDebugWorkerAssertionsNonInteractive();
            _CrtDbgReport(_CRT_ASSERT, __FILE__, __LINE__, nullptr,
                          "Intentional MagDrum isolated-worker assertion test");
            setApplicationReturnValue(9);
            quit();
            return;
        }
#endif
#if JUCE_WINDOWS
        if (arguments.size() == 3 && arguments[0] == "--magdrum-test-host-sequencer")
        {
            const int result = runHostTransportSequencerFixture(juce::File(arguments[1].unquoted()),
                                                                 juce::File(arguments[2].unquoted()));
            setApplicationReturnValue(result);
            quit();
            return;
        }
#endif
        if ((arguments.size() == 2 || arguments.size() == 3)
            && arguments[0] == "--magdrum-vst3-discovery-worker")
        {
#if JUCE_WINDOWS && JUCE_DEBUG
            // Third-party VST3 assertions must fail only this child process; a
            // modal CRT dialog would otherwise pause the whole discovery flow.
            makeDebugWorkerAssertionsNonInteractive();
#endif
            const auto progressFile = juce::File(arguments[1].unquoted());
            const auto additionalFolder = arguments.size() == 3 ? juce::File(arguments[2].unquoted()) : juce::File();
            if (! juce::Thread::launch([this, progressFile, additionalFolder]
                {
                    const auto exitCode = runIsolatedVst3Discovery(progressFile, additionalFolder);
                    juce::MessageManager::callAsync([this, exitCode]
                    {
                        setApplicationReturnValue(exitCode);
                        quit();
                    });
                }))
            {
                setApplicationReturnValue(7);
                quit();
            }
            return;
        }

        if (arguments.size() == 2 && arguments[0] == "--magdrum-vst3-preset-worker")
        {
            const auto request = juce::File(arguments[1].unquoted());
            if (! juce::Thread::launch([this, request]
                {
                    const auto exitCode = runIsolatedVst3PresetWorker(request);
                    juce::MessageManager::callAsync([this, exitCode]
                    {
                        setApplicationReturnValue(exitCode);
                        quit();
                    });
                }))
            {
                setApplicationReturnValue(7);
                quit();
            }
            return;
        }

        if (arguments.size() == 2 && arguments[0] == "--magdrum-plugin-scan-worker")
        {
            const auto request = juce::File(arguments[1].unquoted());
            if (! juce::Thread::launch([request]
                {
                    PluginScanProcess::runWorker(request);
                    juce::MessageManager::callAsync([]
                    {
                        if (auto* app = juce::JUCEApplication::getInstance()) app->quit();
                    });
                }))
                quit();
            return;
        }

        const auto logs = getMagDrumDataDirectory().getChildFile("logs");
        StructuredLogger::initialise(logs);
        StructuredLogger::log(StructuredLogger::Level::info, "application.started",
                              {{"version", getApplicationVersion()}, {"platform", juce::SystemStats::getOperatingSystemName()}});
        const bool openPlugins = arguments.size() == 1 && arguments[0] == "--magdrum-plugin-browser";
        const bool openSourceKit = arguments.size() == 2 && arguments[0] == "--magdrum-open-source-kit";
        window = std::make_unique<Window>(openPlugins);
        if (openPlugins)
            window->openPluginBrowser();
        else if (openSourceKit)
            window->openSourceKitRequest(juce::File(arguments[1].unquoted()));
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        StructuredLogger::log(StructuredLogger::Level::info, "application.stopped");
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    magdrum::ui::LookAndFeel lookAndFeel;

    class Window final : public juce::DocumentWindow
    {
    public:
        explicit Window(bool scannerOnly = false)
            : DocumentWindow("MagDrum", magdrum::ui::Palette::Background, allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(! scannerOnly), true);
            centreWithSize(1140, 790);
            setVisible(true);
        }

        void openPluginBrowser()
        {
            if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
                main->openPluginBrowser();
        }

        void openSourceKitRequest(const juce::File& file)
        {
            const auto json = file.existsAsFile() && file.getSize() <= 65536
                ? file.loadFileAsString() : juce::String();
            file.deleteFile();
            const auto request = PluginSourceKitRequest::fromVar(juce::JSON::parse(json));
            if (auto* main = dynamic_cast<MainComponent*>(getContentComponent()))
            {
                if (request)
                    main->openSourceKitRequest(*request);
                else
                    main->openSourceKitRequest({});
            }
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(MagDrumApplication)
