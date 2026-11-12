#include "PluginEditor.h"
#include "../core/MagDrumPaths.h"
#include "../core/ScopedFileAccess.h"
#include <array>
#include <cmath>

namespace
{
using magdrum::ui::Palette;
constexpr std::array<int, 16> padNotes{36,38,42,46,43,47,50,51,53,49,55,39,37,40,44,54};
constexpr std::array<const char*, 16> padNames{"Kick","Snare","Closed Hat","Open Hat","Low Tom","Mid Tom","High Tom","Ride","Ride Bell","Crash","Splash","Clap","Rimshot","Snare Rim","Pedal Hat","Percussion"};
constexpr std::array<int, 8> laneNotes{36,38,42,46,43,51,49,39};
constexpr std::array<const char*, 8> laneNames{"KICK","SNARE","CLOSED HAT","OPEN HAT","TOM","RIDE","CRASH","CLAP"};
juce::Colour categoryColour(const juce::String& value) { return Palette::forDrumCategory(value); }
juce::String noteName(int note)
{
    static constexpr const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}
int velocityMinimum(const juce::String& text)
{
    const auto trimmed=text.trim();const int dash=trimmed.indexOfChar('-');
    return dash>=0?trimmed.substring(0,dash).getIntValue():trimmed.getIntValue();
}
int velocityMaximum(const juce::String& text)
{
    const auto trimmed=text.trim();const int dash=trimmed.indexOfChar('-');
    return dash>=0?trimmed.substring(dash+1).getIntValue():trimmed.getIntValue();
}
}

class MagDrumSampleMap final : public juce::Component
{
public:
    std::function<void(int)> onSelect;
    void setSamples(const juce::Array<SampleBrowseRecord>& value, int selected)
    {
        samples=value;selectedIndex=selected;repaint();
    }
    void setSelected(int selected) { selectedIndex=selected;repaint(); }
    void paint(juce::Graphics& g) override
    {
        auto bounds=getLocalBounds().toFloat().reduced(14.0f);
        g.setColour(Palette::Panel);g.fillRoundedRectangle(bounds,9.0f);
        auto plot=bounds.reduced(24.0f,24.0f);plot.removeFromTop(16.0f);plot.removeFromBottom(20.0f);
        g.setColour(Palette::Border);g.drawRoundedRectangle(plot,5.0f,1.0f);
        for(int i=1;i<4;++i){const float x=plot.getX()+plot.getWidth()*i/4.0f;const float y=plot.getY()+plot.getHeight()*i/4.0f;g.setColour(Palette::Border.withAlpha(0.48f));g.drawVerticalLine(static_cast<int>(x),plot.getY(),plot.getBottom());g.drawHorizontalLine(static_cast<int>(y),plot.getX(),plot.getRight());}
        g.setColour(Palette::Muted);g.setFont(11.0f);g.drawText("SAMPLE MAP  ·  brightness × duration",bounds.toNearestInt().removeFromTop(24).reduced(12,0),juce::Justification::centredLeft);
        g.drawText("SHORTER",static_cast<int>(plot.getX()),static_cast<int>(plot.getBottom()+4),80,14,juce::Justification::left);
        g.drawText("LONGER",static_cast<int>(plot.getRight()-80),static_cast<int>(plot.getBottom()+4),80,14,juce::Justification::right);
        for(int i=0;i<samples.size();++i)
        {
            const auto& sample=samples.getReference(i);
            const double duration=juce::jlimit(0.0,1.0,std::log10(juce::jmax(0.01,sample.durationSeconds)+0.01)/2.0);
            const double brightness=juce::jlimit(0.0,1.0,std::log10(juce::jmax(100.0,sample.brightnessHz)/100.0)/2.5);
            const auto x=plot.getX()+static_cast<float>(duration)*(plot.getWidth()-12.0f)+6.0f;
            const auto y=plot.getBottom()-static_cast<float>(brightness)*(plot.getHeight()-12.0f)-6.0f;
            const bool selected=i==selectedIndex;const float radius=selected?7.0f:4.2f;
            g.setColour(categoryColour(sample.category).withAlpha(selected?1.0f:0.72f));g.fillEllipse(x-radius,y-radius,radius*2,radius*2);
            if(selected){g.setColour(Palette::Text);g.drawEllipse(x-radius-2,y-radius-2,(radius+2)*2,(radius+2)*2,1.4f);}
        }
        if(samples.isEmpty()){g.setColour(Palette::Muted);g.drawFittedText("Scan a sample folder in the MagDrum app to populate the library.",plot.toNearestInt().reduced(22),juce::Justification::centred,3);}
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if(samples.isEmpty())return;auto plot=getLocalBounds().toFloat().reduced(38.0f);plot.removeFromTop(18.0f);plot.removeFromBottom(20.0f);
        int closest=-1;float distance=100000.0f;
        for(int i=0;i<samples.size();++i){const auto& sample=samples.getReference(i);const float x=plot.getX()+static_cast<float>(juce::jlimit(0.0,1.0,std::log10(juce::jmax(0.01,sample.durationSeconds)+0.01)/2.0))*(plot.getWidth()-12.0f)+6.0f;const float y=plot.getBottom()-static_cast<float>(juce::jlimit(0.0,1.0,std::log10(juce::jmax(100.0,sample.brightnessHz)/100.0)/2.5))*(plot.getHeight()-12.0f)-6.0f;const float d=event.position.getDistanceFrom({x,y});if(d<distance){distance=d;closest=i;}}
        if(closest>=0&&distance<24.0f){selectedIndex=closest;repaint();if(onSelect)onSelect(closest);}
    }
private:
    juce::Array<SampleBrowseRecord> samples;int selectedIndex=-1;
};

class MagDrumWaveform final : public juce::Component, private juce::ChangeListener
{
public:
    MagDrumWaveform() : thumbnail(512, formats, cache)
    {
        formats.registerBasicFormats();thumbnail.addChangeListener(this);
    }
    ~MagDrumWaveform() override { thumbnail.removeChangeListener(this); }
    void setFile(const juce::File& file)
    {
        currentFile=file;thumbnail.setSource(file.existsAsFile()?new juce::FileInputSource(file):nullptr);repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto area=getLocalBounds().toFloat().reduced(2.0f);g.setColour(Palette::Panel);g.fillRoundedRectangle(area,7.0f);
        g.setColour(Palette::Muted);g.setFont(11.0f);g.drawText("SAMPLE WAVEFORM",getLocalBounds().reduced(12,7).removeFromTop(18),juce::Justification::centredLeft);
        auto wave=getLocalBounds().reduced(12,28).toFloat();g.setColour(Palette::Border);g.drawHorizontalLine(static_cast<int>(wave.getCentreY()),wave.getX(),wave.getRight());
        if(thumbnail.getNumChannels()>0){g.setColour(Palette::Coral);thumbnail.drawChannels(g,wave.toNearestInt(),0.0,thumbnail.getTotalLength(),1.0f);}
        else{g.setColour(Palette::Muted);g.drawFittedText(currentFile.existsAsFile()?"Reading waveform…":"No sample loaded",wave.toNearestInt(),juce::Justification::centred,2);}
    }
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    juce::AudioFormatManager formats;juce::AudioThumbnailCache cache{8};juce::AudioThumbnail thumbnail;juce::File currentFile;
};

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), indexedList("Samples",this), sampleMap(std::make_unique<MagDrumSampleMap>()), waveform(std::make_unique<MagDrumWaveform>())
{
    setLookAndFeel(&theme);setSize(1120,760);setResizable(true,true);setResizeLimits(900,620,1800,1200);
    for(auto* tab:{&libraryTab,&machineTab,&sequencerTab}){tab->addListener(this);addAndMakeVisible(*tab);}
    sampleSearch.setTextToShowWhenEmpty("Search indexed samples",Palette::Muted);addAndMakeVisible(sampleSearch);
    categoryFilter.addItem("All sounds",1);categoryFilter.addItem("Kick",2);categoryFilter.addItem("Snare",3);categoryFilter.addItem("Closed hi-hat",4);categoryFilter.addItem("Open hi-hat",5);categoryFilter.addItem("Pedal hi-hat",6);categoryFilter.addItem("Tom",7);categoryFilter.addItem("Ride",8);categoryFilter.addItem("Crash",9);categoryFilter.addItem("Clap",10);categoryFilter.addItem("Percussion",11);categoryFilter.addItem("Unknown",12);categoryFilter.setSelectedId(1);addAndMakeVisible(categoryFilter);
    searchButton.addListener(this);previewButton.addListener(this);assignButton.addListener(this);addAndMakeVisible(searchButton);addAndMakeVisible(previewButton);addAndMakeVisible(assignButton);
    indexedList.setModel(this);indexedList.setRowHeight(43);addAndMakeVisible(indexedList);
    sampleMap->onSelect=[this](int index){if(juce::isPositiveAndBelow(index,samples.size()))indexedList.selectRow(index);};addAndMakeVisible(*sampleMap);
    for(size_t i=0;i<pads.size();++i){auto& pad=pads[i];pad.setButtonText(juce::String(padNames[i])+"\n"+noteName(padNotes[i]));pad.setColour(juce::TextButton::buttonColourId,Palette::Surface);pad.setColour(juce::TextButton::buttonOnColourId,categoryColour(padNames[i]));pad.setClickingTogglesState(false);pad.addListener(this);addAndMakeVisible(pad);}
    selectedPadTitle.setFont(juce::Font(juce::FontOptions(22.0f,juce::Font::bold)));selectedPadTitle.setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(selectedPadTitle);
    layerSummary.setColour(juce::Label::textColourId,Palette::Text);layerSummary.setJustificationType(juce::Justification::topLeft);layerSummary.setMinimumHorizontalScale(0.8f);addAndMakeVisible(layerSummary);
    sampleDetails.setColour(juce::Label::textColourId,Palette::Muted);sampleDetails.setJustificationType(juce::Justification::topLeft);sampleDetails.setMinimumHorizontalScale(0.8f);addAndMakeVisible(sampleDetails);
    addAndMakeVisible(*waveform);
    velocityRange.setText("1-127");velocityRange.setInputRestrictions(7,"0123456789-");velocityRange.setTooltip("Inclusive velocity range for this sample layer (1–127).");addAndMakeVisible(velocityRange);
    for(auto* button:{&clearPadButton,&loadButton}){button->addListener(this);addAndMakeVisible(*button);}
    for(size_t row=0;row<laneLabels.size();++row){laneLabels[row].setText(laneNames[row],juce::dontSendNotification);laneLabels[row].setColour(juce::Label::textColourId,categoryColour(laneNames[row]));laneLabels[row].setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));addAndMakeVisible(laneLabels[row]);for(size_t step=0;step<16;++step){auto& cell=steps[row][step];cell.setClickingTogglesState(true);cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]));cell.setTooltip(juce::String(laneNames[row])+" · MIDI "+juce::String(laneNotes[row])+" · step "+juce::String(static_cast<int>(step+1)));addAndMakeVisible(cell);}}
    playSequenceButton.addListener(this);clearSequenceButton.addListener(this);exportSequenceButton.addListener(this);addAndMakeVisible(playSequenceButton);addAndMakeVisible(clearSequenceButton);addAndMakeVisible(exportSequenceButton);
    tempoEditor.setText("120");tempoEditor.setInputRestrictions(3,"0123456789");tempoEditor.setTooltip("Pattern preview and MIDI export tempo.");addAndMakeVisible(tempoEditor);
    status.setColour(juce::Label::textColourId,Palette::Muted);status.setJustificationType(juce::Justification::centredLeft);addAndMakeVisible(status);
    const auto dataDirectory=getMagDrumDataDirectory();
    if(dataDirectory!=juce::File()&&(dataDirectory.createDirectory().wasOk()||dataDirectory.isDirectory()))databaseReady=database.open(dataDirectory.getChildFile("samples.sqlite3"));
    if(!databaseReady)status.setText("Sample index unavailable. Load WAVs directly or scan a folder in the MagDrum app.",juce::dontSendNotification);
    else refreshSamples();
    setNote(36);setPage(0);
}

MagDrumAudioProcessorEditor::~MagDrumAudioProcessorEditor(){stopTimer();setLookAndFeel(nullptr);}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::Background);g.setColour(Palette::Accent);g.setFont(juce::Font(juce::FontOptions(30.0f,juce::Font::bold)));g.drawText("MagDrum",24,14,260,40,juce::Justification::centredLeft);
    g.setColour(Palette::Muted);g.setFont(13.0f);g.drawText("SAMPLE LIBRARY  /  DRUM MACHINE  /  STEP SEQUENCER",26,51,650,22,juce::Justification::centredLeft);
    g.setColour(Palette::Border);g.drawHorizontalLine(83,24.0f,static_cast<float>(getWidth()-24));
}

void MagDrumAudioProcessorEditor::resized()
{
    auto bounds=getLocalBounds().reduced(24);bounds.removeFromTop(67);auto tabs=bounds.removeFromTop(38);
    libraryTab.setBounds(tabs.removeFromLeft(190));tabs.removeFromLeft(6);machineTab.setBounds(tabs.removeFromLeft(190));tabs.removeFromLeft(6);sequencerTab.setBounds(tabs.removeFromLeft(190));bounds.removeFromTop(12);
    auto footer=bounds.removeFromBottom(26);status.setBounds(footer);bounds.removeFromBottom(8);
    libraryTab.setToggleState(page==0,juce::dontSendNotification);machineTab.setToggleState(page==1,juce::dontSendNotification);sequencerTab.setToggleState(page==2,juce::dontSendNotification);
    sampleSearch.setVisible(page==0);categoryFilter.setVisible(page==0);searchButton.setVisible(page==0);previewButton.setVisible(page==0);assignButton.setVisible(page==0);indexedList.setVisible(page==0);sampleMap->setVisible(page==0);
    for(auto& pad:pads)pad.setVisible(page==1);selectedPadTitle.setVisible(page==1);layerSummary.setVisible(page==1);sampleDetails.setVisible(page==1);waveform->setVisible(page==1);velocityRange.setVisible(page==1);clearPadButton.setVisible(page==1);loadButton.setVisible(page==1);
    for(auto& label:laneLabels)label.setVisible(page==2);for(auto& row:steps)for(auto& cell:row)cell.setVisible(page==2);playSequenceButton.setVisible(page==2);clearSequenceButton.setVisible(page==2);exportSequenceButton.setVisible(page==2);tempoEditor.setVisible(page==2);
    if(page==0){auto controls=bounds.removeFromTop(36);sampleSearch.setBounds(controls.removeFromLeft(300));controls.removeFromLeft(8);categoryFilter.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(8);searchButton.setBounds(controls.removeFromLeft(100));controls.removeFromLeft(8);previewButton.setBounds(controls.removeFromLeft(100));controls.removeFromLeft(8);assignButton.setBounds(controls.removeFromLeft(130));bounds.removeFromTop(10);auto content=bounds;const int left=juce::jlimit(320,440,content.getWidth()*2/5);indexedList.setBounds(content.removeFromLeft(left));content.removeFromLeft(12);sampleMap->setBounds(content);}
    else if(page==1){auto grid=bounds.removeFromLeft(bounds.getWidth()*3/5);const int gap=10;const int padW=(grid.getWidth()-3*gap)/4,padH=(grid.getHeight()-3*gap)/4;for(size_t i=0;i<pads.size();++i){const int row=static_cast<int>(i/4),col=static_cast<int>(i%4);pads[i].setBounds(grid.getX()+col*(padW+gap),grid.getY()+row*(padH+gap),padW,padH);}bounds.removeFromLeft(18);selectedPadTitle.setBounds(bounds.removeFromTop(38));bounds.removeFromTop(6);sampleDetails.setBounds(bounds.removeFromTop(58));bounds.removeFromTop(8);waveform->setBounds(bounds.removeFromTop(154));bounds.removeFromTop(8);layerSummary.setBounds(bounds.removeFromTop(juce::jmin(180,bounds.getHeight()/2)));bounds.removeFromTop(12);auto row=bounds.removeFromTop(34);velocityRange.setBounds(row.removeFromLeft(100));row.removeFromLeft(8);clearPadButton.setBounds(row.removeFromLeft(108));row.removeFromLeft(8);loadButton.setBounds(row);bounds.removeFromTop(14);}
    else{auto toolbar=bounds.removeFromTop(36);playSequenceButton.setBounds(toolbar.removeFromLeft(92));toolbar.removeFromLeft(8);clearSequenceButton.setBounds(toolbar.removeFromLeft(126));toolbar.removeFromLeft(8);exportSequenceButton.setBounds(toolbar.removeFromLeft(120));toolbar.removeFromLeft(18);tempoEditor.setBounds(toolbar.removeFromLeft(70));bounds.removeFromTop(16);const int labelW=122;const int cellW=juce::jmax(30,(bounds.getWidth()-labelW-15*4)/16);for(int row=0;row<8;++row){auto line=bounds.removeFromTop(juce::jmax(36,bounds.getHeight()/8));laneLabels[static_cast<size_t>(row)].setBounds(line.removeFromLeft(labelW));line.removeFromLeft(6);for(int step=0;step<16;++step){steps[static_cast<size_t>(row)][static_cast<size_t>(step)].setBounds(line.removeFromLeft(cellW));line.removeFromLeft(4);}}}
}

int MagDrumAudioProcessorEditor::getNumRows(){return samples.size();}
void MagDrumAudioProcessorEditor::paintListBoxItem(int row,juce::Graphics& g,int width,int height,bool selected)
{
    if(!juce::isPositiveAndBelow(row,samples.size()))return;const auto& sample=samples.getReference(row);g.fillAll(selected?Palette::Selection:Palette::Panel);g.setColour(categoryColour(sample.category));g.fillRoundedRectangle(6.0f,8.0f,4.0f,static_cast<float>(height-16),2.0f);g.setColour(Palette::Text);g.setFont(13.0f);g.drawText(sample.file.getFileName(),18,4,width-24,19,juce::Justification::centredLeft,true);g.setColour(Palette::Muted);g.setFont(11.0f);const auto info=sample.category+"  ·  "+juce::String(sample.durationSeconds,2)+" s  ·  "+juce::String(static_cast<int>(sample.brightnessHz))+" Hz";g.drawText(info,18,22,width-24,height-23,juce::Justification::centredLeft,true);
}
void MagDrumAudioProcessorEditor::selectedRowsChanged(int row){sampleMap->setSelected(row);if(juce::isPositiveAndBelow(row,samples.size()))sampleDetails.setText(samples.getReference(row).file.getFullPathName(),juce::dontSendNotification);}
void MagDrumAudioProcessorEditor::listBoxItemDoubleClicked(int row,const juce::MouseEvent&){if(juce::isPositiveAndBelow(row,samples.size())){if(processor.previewSample(samples.getReference(row).file))status.setText("Previewing "+samples.getReference(row).file.getFileName(),juce::dontSendNotification);}}

void MagDrumAudioProcessorEditor::refreshSamples()
{
    if(!databaseReady)return;const int id=categoryFilter.getSelectedId();juce::String category;switch(id){case 2:category="Kick";break;case 3:category="Snare";break;case 4:category="Closed hi-hat";break;case 5:category="Open hi-hat";break;case 6:category="Pedal hi-hat";break;case 7:category="Tom";break;case 8:category="Ride";break;case 9:category="Crash";break;case 10:category="Clap";break;case 11:category="Percussion";break;case 12:category="Unknown";break;default:break;}
    samples=database.searchSampleBrowseRecords(sampleSearch.getText(),category,1000);indexedList.updateContent();indexedList.deselectAllRows();sampleMap->setSamples(samples,-1);status.setText(juce::String(samples.size())+" samples · click a point to select · double-click or Preview to audition",juce::dontSendNotification);
}
juce::File MagDrumAudioProcessorEditor::selectedSample()const{const int row=indexedList.getSelectedRow();return juce::isPositiveAndBelow(row,samples.size())?samples.getReference(row).file:juce::File();}

void MagDrumAudioProcessorEditor::setPage(int value){page=juce::jlimit(0,2,value);resized();repaint();}
void MagDrumAudioProcessorEditor::setNote(int note){selectedNote=juce::jlimit(0,127,note);const int index=static_cast<int>(std::find(padNotes.begin(),padNotes.end(),selectedNote)-padNotes.begin());const int safeIndex=juce::jlimit(0,static_cast<int>(padNames.size())-1,index);for(size_t i=0;i<pads.size();++i){pads[i].setToggleState(static_cast<int>(i)==index,juce::dontSendNotification);pads[i].setColour(juce::TextButton::buttonColourId,static_cast<int>(i)==index?categoryColour(padNames[i]).withAlpha(0.34f):Palette::Surface);}selectedPadTitle.setText(juce::String(padNames[static_cast<size_t>(safeIndex)])+"   ·   "+noteName(selectedNote)+"   ·   MIDI "+juce::String(selectedNote),juce::dontSendNotification);updateLayerSummary();const auto layers=processor.getSampleLayersForNote(selectedNote);sampleDetails.setText(layers.empty()?"Selected pad sound\nNo sample loaded":juce::String("PAD SAMPLE\n")+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path,juce::dontSendNotification);}
void MagDrumAudioProcessorEditor::updateLayerSummary()
{
    const auto layers=processor.getSampleLayersForNote(selectedNote);juce::String text;
    if(layers.empty())text="No sample assigned\nLoad a WAV here or assign a library sound from Sample Library.";
    else for(size_t i=0;i<layers.size();++i){if(i>0)text+="\n";text+="VELOCITY "+juce::String(layers[i].minimumVelocity)+"–"+juce::String(layers[i].maximumVelocity)+"\n"+juce::File(layers[i].path).getFileName();}
    layerSummary.setText(text,juce::dontSendNotification);
    sampleDetails.setText(layers.empty()?"PAD SAMPLE\nNo sample loaded":juce::String("PAD SAMPLE\n")+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path,juce::dontSendNotification);
    waveform->setFile(layers.empty()?juce::File():juce::File(layers.front().path));
}

void MagDrumAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if(button==&libraryTab){setPage(0);return;}if(button==&machineTab){setPage(1);return;}if(button==&sequencerTab){setPage(2);return;}
    if(button==&searchButton){refreshSamples();return;}
    if(button==&previewButton){const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample to preview.",juce::dontSendNotification);return;}status.setText(processor.previewSample(file)?"Previewing "+file.getFileName():"Could not read this audio file.",juce::dontSendNotification);return;}
    if(button==&assignButton){const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample first.",juce::dontSendNotification);return;}if(processor.loadSampleForVelocityRange(selectedNote,velocityMinimum(velocityRange.getText()),velocityMaximum(velocityRange.getText()),file)){updateLayerSummary();status.setText("Assigned "+file.getFileName()+" to "+juce::String(padNames[static_cast<size_t>(std::max(0,static_cast<int>(std::find(padNotes.begin(),padNotes.end(),selectedNote)-padNotes.begin())))])+".",juce::dontSendNotification);}else status.setText("Assignment failed. Check velocity ranges and the eight-layer limit.",juce::dontSendNotification);return;}
    for(size_t i=0;i<pads.size();++i)if(button==&pads[i]){setNote(padNotes[i]);processor.triggerNote(selectedNote);status.setText(processor.getSampleLayersForNote(selectedNote).empty()?"Pad selected · no sample assigned yet.":"Played "+juce::String(padNames[i]),juce::dontSendNotification);return;}
    if(button==&clearPadButton){status.setText(processor.clearSamplesForNote(selectedNote)?"Pad cleared.":"Pad is already empty.",juce::dontSendNotification);updateLayerSummary();return;}
    if(button==&loadButton){chooser=std::make_unique<juce::FileChooser>("Choose a drum sample",juce::File(),"*.wav;*.aif;*.aiff;*.flac",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr)return;const auto file=fc.getResult();if(!file.existsAsFile())return;if(safe->processor.loadSampleForVelocityRange(safe->selectedNote,velocityMinimum(safe->velocityRange.getText()),velocityMaximum(safe->velocityRange.getText()),file)){safe->updateLayerSummary();safe->status.setText("Loaded "+file.getFileName(),juce::dontSendNotification);}else safe->status.setText("Could not assign sample. Check range, format and layer limit.",juce::dontSendNotification);});return;}
    if(button==&playSequenceButton){if(sequencerPlaying){sequencerPlaying=false;stopTimer();playSequenceButton.setButtonText("Play");}else{const int bpm=tempoEditor.getText().getIntValue();if(bpm<30||bpm>300){status.setText("Tempo must be between 30 and 300 BPM.",juce::dontSendNotification);return;}sequenceTempo=bpm;sequenceStep=0;sequencerPlaying=true;playSequenceButton.setButtonText("Stop");startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*4.0))));}return;}
    if(button==&clearSequenceButton){for(auto& row:steps)for(auto& cell:row)cell.setToggleState(false,juce::dontSendNotification);status.setText("Pattern cleared.",juce::dontSendNotification);return;}
    if(button==&exportSequenceButton){const int bpm=tempoEditor.getText().getIntValue();if(bpm<30||bpm>300){status.setText("Tempo must be between 30 and 300 BPM.",juce::dontSendNotification);return;}juce::MidiFile midi;midi.setTicksPerQuarterNote(960);juce::MidiMessageSequence track;track.addEvent(juce::MidiMessage::tempoMetaEvent(60000000/bpm),0.0);track.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4,4),0.0);for(int row=0;row<8;++row)for(int step=0;step<16;++step)if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState()){const double start=step*240.0;track.addEvent(juce::MidiMessage::noteOn(10,laneNotes[static_cast<size_t>(row)],static_cast<juce::uint8>(100)),start);track.addEvent(juce::MidiMessage::noteOff(10,laneNotes[static_cast<size_t>(row)]),start+120.0);}track.addEvent(juce::MidiMessage::endOfTrack(),3840.0);track.sort();midi.addTrack(track);chooser=std::make_unique<juce::FileChooser>("Export drum pattern",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Pattern.mid"),"*.mid",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe,midi](const juce::FileChooser& fc)mutable{if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=juce::File(file.getFullPathName()+".mid");auto stream=file.createOutputStream();const bool ok=stream&&midi.writeTo(*stream);if(stream)stream->flush();safe->status.setText(ok?"Exported MIDI pattern: "+file.getFileName():"Could not write MIDI file.",juce::dontSendNotification);});return;}
}

void MagDrumAudioProcessorEditor::timerCallback()
{
    if(!sequencerPlaying)return;const int step=sequenceStep;for(int row=0;row<8;++row)if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState())processor.triggerNote(laneNotes[static_cast<size_t>(row)],100);sequenceStep=(sequenceStep+1)%16;
}
