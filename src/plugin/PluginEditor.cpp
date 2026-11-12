#include "PluginEditor.h"
#include "../core/MagDrumPaths.h"
#include "../core/ScopedFileAccess.h"
#include "../core/WavScanner.h"
#include <array>
#include <cmath>

namespace
{
using magdrum::ui::Palette;
constexpr std::array<int, 16> defaultPadNotes{36,38,42,46,43,47,50,51,53,49,55,39,37,40,44,54};
constexpr std::array<const char*, 16> padNames{"Kick","Snare","Closed Hat","Open Hat","Low Tom","Mid Tom","High Tom","Ride","Ride Bell","Crash","Splash","Clap","Rimshot","Snare Rim","Pedal Hat","Percussion"};
constexpr std::array<int, 8> defaultLaneNotes{36,38,42,46,43,51,49,39};
constexpr std::array<const char*, 8> laneNames{"KICK","SNARE","CLOSED HAT","OPEN HAT","TOM","RIDE","CRASH","CLAP"};
using I=DrumInstrument;using A=DrumArticulation;
constexpr std::array<I,16> padInstruments{I::Kick,I::Snare,I::HiHat,I::HiHat,I::Tom,I::Tom,I::Tom,I::Ride,I::Ride,I::Crash,I::Crash,I::Clap,I::Rimshot,I::Snare,I::HiHat,I::Percussion};
constexpr std::array<A,16> padArticulations{A::Default,A::Default,A::Closed,A::Open,A::Low,A::Mid,A::High,A::Bow,A::Bell,A::Default,A::Default,A::Default,A::Default,A::Rimshot,A::Pedal,A::Default};
constexpr std::array<I,8> laneInstruments{I::Kick,I::Snare,I::HiHat,I::HiHat,I::Tom,I::Ride,I::Crash,I::Clap};
constexpr std::array<A,8> laneArticulations{A::Default,A::Default,A::Closed,A::Open,A::Low,A::Bow,A::Default,A::Default};
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
        if(samples.isEmpty()){g.setColour(Palette::Muted);g.drawFittedText("Add a sample folder above to populate the library.",plot.toNearestInt().reduced(22),juce::Justification::centred,3);}
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

class MagDrumSampleComparison final : public juce::Component
{
public:
    MagDrumSampleComparison(const juce::File& first,const juce::File& second)
    {
        for(auto* label:{&leftTitle,&rightTitle}){label->setJustificationType(juce::Justification::centredLeft);label->setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(*label);}
        leftTitle.setText(first.getFileName(),juce::dontSendNotification);rightTitle.setText(second.getFileName(),juce::dontSendNotification);leftWave.setFile(first);rightWave.setFile(second);addAndMakeVisible(leftWave);addAndMakeVisible(rightWave);
    }
    void resized() override
    {
        auto area=getLocalBounds().reduced(12);const int column=area.getWidth()/2;auto a=area.removeFromLeft(column).reduced(6);auto b=area.reduced(6);leftTitle.setBounds(a.removeFromTop(30));a.removeFromTop(6);leftWave.setBounds(a);rightTitle.setBounds(b.removeFromTop(30));b.removeFromTop(6);rightWave.setBounds(b);
    }
private:
    juce::Label leftTitle,rightTitle;MagDrumWaveform leftWave,rightWave;
};

class MagDrumSampleComparisonWindow final : public juce::DocumentWindow
{
public:
    MagDrumSampleComparisonWindow(const juce::File& first,const juce::File& second)
        :juce::DocumentWindow("Compare drum samples",Palette::Panel,juce::DocumentWindow::closeButton)
    {setUsingNativeTitleBar(true);setContentOwned(new MagDrumSampleComparison(first,second),true);setResizable(true,true);centreWithSize(900,420);setVisible(true);}
    void closeButtonPressed() override {setVisible(false);}
};

class PluginSampleScanJob final : public juce::ThreadPoolJob
{
public:
    using Completion=std::function<void(const WavScanner::Stats&,std::int64_t,const juce::String&)>;
    using Progress=std::function<void(const juce::File&)>;
    PluginSampleScanJob(juce::File root,juce::File databaseFile,std::shared_ptr<std::atomic<bool>> cancellation,Completion completion,Progress progress)
        :ThreadPoolJob("MagDrum VST3 sample folder scan"),rootFolder(std::move(root)),dbPath(std::move(databaseFile)),cancelFlag(std::move(cancellation)),onComplete(std::move(completion)),onProgress(std::move(progress)){}
    JobStatus runJob() override
    {
        WavScanner::Stats stats;std::int64_t indexed=0,scanId=0;juce::String error;SampleDatabase scanDatabase;
        if(!rootFolder.isDirectory())error="The selected sample folder is unavailable.";
        else if(!scanDatabase.open(dbPath))error="Could not open the shared MagDrum sample database.";
        const auto rootPath=rootFolder.getFullPathName();
        const auto sourceId=error.isEmpty()?scanDatabase.ensureSampleSource("folder",rootPath,rootFolder.getFileName().isNotEmpty()?rootFolder.getFileName():rootPath,rootPath):std::optional<std::int64_t>();
        if(error.isEmpty()&&!sourceId)error="Could not save this sample folder in the shared library.";
        juce::String resumeAfter;
        if(error.isEmpty())
        {
            resumeAfter=scanDatabase.getResumableSampleScanPath(rootFolder);
            const auto started=scanDatabase.beginSampleScan(rootFolder,resumeAfter);
            if(!started)error="Could not create the scan-history record.";else scanId=*started;
        }
        if(error.isEmpty())
        {
            int progressCount=0;WavScanner scanner;
            (void)scanner.scan(rootFolder,[this]{return shouldExit()||(cancelFlag&&cancelFlag->load());},
                [&scanDatabase](const juce::File& file,std::int64_t size,std::int64_t modified){return scanDatabase.isSampleUnchanged(file,size,modified);},
                &stats,[&](const ScannedSample& sample){if(!scanDatabase.insertSample(sample,*sourceId))return false;++indexed;return true;},resumeAfter,
                [this,&progressCount,&scanDatabase,scanId](const juce::File& file){if(onProgress&&((++progressCount%25)==1))onProgress(file);return scanDatabase.updateSampleScanCheckpoint(scanId,file);});
            if(!scanDatabase.assignSamplesToSource(rootFolder,*sourceId))++stats.errors;
            const auto state=stats.cancelled?"cancelled":"completed";
            if(!scanDatabase.finishSampleScan(scanId,state,stats.filesConsidered,stats.filesSkippedUnchanged,indexed,stats.errors))error="The scan finished, but its history record could not be updated.";
        }
        if(onComplete)onComplete(stats,indexed,error);
        return jobHasFinished;
    }
private:
    juce::File rootFolder,dbPath;std::shared_ptr<std::atomic<bool>>cancelFlag;Completion onComplete;Progress onProgress;
};

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), indexedList("Samples",this), sampleMap(std::make_unique<MagDrumSampleMap>()), waveform(std::make_unique<MagDrumWaveform>())
{
    setLookAndFeel(&theme);setSize(1120,760);setResizable(true,true);setResizeLimits(1000,620,1800,1200);
    for(auto* tab:{&libraryTab,&machineTab,&sequencerTab,&mapsTab}){tab->addListener(this);addAndMakeVisible(*tab);}
    sampleSearch.setTextToShowWhenEmpty("Search indexed samples",Palette::Muted);sampleSearch.onReturnKey=[this]{refreshSamples();};addAndMakeVisible(sampleSearch);
    categoryFilter.addItem("All sounds",1);categoryFilter.addItem("Kick",2);categoryFilter.addItem("Snare",3);categoryFilter.addItem("Closed hi-hat",4);categoryFilter.addItem("Open hi-hat",5);categoryFilter.addItem("Pedal hi-hat",6);categoryFilter.addItem("Tom",7);categoryFilter.addItem("Ride",8);categoryFilter.addItem("Crash",9);categoryFilter.addItem("Clap",10);categoryFilter.addItem("Percussion",11);categoryFilter.addItem("Unknown",12);categoryFilter.setSelectedId(1);addAndMakeVisible(categoryFilter);
    const char* features[]{"All sound features","Very short","Short","Medium","Long","Dark","Mid brightness","Bright"};for(int i=0;i<8;++i)featureFilter.addItem(features[i],i+1);featureFilter.setSelectedId(1);addAndMakeVisible(featureFilter);
    const char* sorts[]{"Name A–Z","Duration shortest","Duration longest","Brightest first","Darkest first"};for(int i=0;i<5;++i)sortFilter.addItem(sorts[i],i+1);sortFilter.setSelectedId(1);addAndMakeVisible(sortFilter);
    confidenceFilter.addItem("All confidence levels",1);confidenceFilter.addItem("Needs review",2);confidenceFilter.setSelectedId(1);addAndMakeVisible(confidenceFilter);
    sourceFilter.addItem("All sources",1);sourceFilter.setSelectedId(1);addAndMakeVisible(sourceFilter);kitFilter.addItem("All kits",1);kitFilter.setSelectedId(1);addAndMakeVisible(kitFilter);
    auto refreshOnChange=[this]{refreshSamples();};categoryFilter.onChange=refreshOnChange;featureFilter.onChange=refreshOnChange;sortFilter.onChange=refreshOnChange;confidenceFilter.onChange=refreshOnChange;sourceFilter.onChange=refreshOnChange;kitFilter.onChange=refreshOnChange;
    scanButton.addListener(this);searchButton.addListener(this);previewButton.addListener(this);assignButton.addListener(this);libraryToolsButton.addListener(this);moreSamplesButton.addListener(this);addAndMakeVisible(scanButton);addAndMakeVisible(searchButton);addAndMakeVisible(previewButton);addAndMakeVisible(assignButton);addAndMakeVisible(libraryToolsButton);addAndMakeVisible(moreSamplesButton);
    indexedList.setModel(this);indexedList.setRowHeight(43);indexedList.setMultipleSelectionEnabled(true);addAndMakeVisible(indexedList);
    sampleMap->onSelect=[this](int index){if(juce::isPositiveAndBelow(index,samples.size()))indexedList.selectRow(index);};addAndMakeVisible(*sampleMap);
    for(size_t i=0;i<pads.size();++i){auto& pad=pads[i];pad.setButtonText(juce::String(padNames[i])+"\n"+noteName(mappedPadNotes[i]));pad.setColour(juce::TextButton::buttonColourId,Palette::Surface);pad.setColour(juce::TextButton::buttonOnColourId,categoryColour(padNames[i]));pad.setClickingTogglesState(false);pad.addListener(this);addAndMakeVisible(pad);}
    selectedPadTitle.setFont(juce::Font(juce::FontOptions(22.0f,juce::Font::bold)));selectedPadTitle.setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(selectedPadTitle);
    layerSummary.setColour(juce::Label::textColourId,Palette::Text);layerSummary.setJustificationType(juce::Justification::topLeft);layerSummary.setMinimumHorizontalScale(0.8f);addAndMakeVisible(layerSummary);
    sampleDetails.setColour(juce::Label::textColourId,Palette::Muted);sampleDetails.setJustificationType(juce::Justification::topLeft);sampleDetails.setMinimumHorizontalScale(0.8f);addAndMakeVisible(sampleDetails);
    addAndMakeVisible(*waveform);
    velocityRange.setText("1-127");velocityRange.setInputRestrictions(7,"0123456789-");velocityRange.setTooltip("Inclusive velocity range for this sample layer (1–127).");addAndMakeVisible(velocityRange);
    for(auto* button:{&clearPadButton,&loadButton}){button->addListener(this);addAndMakeVisible(*button);}
    for(size_t row=0;row<laneLabels.size();++row){laneLabels[row].setText(laneNames[row],juce::dontSendNotification);laneLabels[row].setColour(juce::Label::textColourId,categoryColour(laneNames[row]));laneLabels[row].setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));addAndMakeVisible(laneLabels[row]);for(size_t step=0;step<64;++step){auto& cell=steps[row][step];cell.setClickingTogglesState(true);cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]));cell.setTooltip(juce::String(laneNames[row])+" · MIDI "+juce::String(mappedLaneNotes[row])+" · bar "+juce::String(static_cast<int>(step/16+1))+" · step "+juce::String(static_cast<int>(step%16+1)));cell.addListener(this);addAndMakeVisible(cell);}}
    for(int bar=1;bar<=4;++bar)barSelect.addItem("Bar "+juce::String(bar),bar);barSelect.setSelectedId(1);barSelect.onChange=[this]{for(size_t row=0;row<steps.size();++row)for(size_t index=0;index<steps[row].size();++index)steps[row][index].setVisible(page==2&&static_cast<int>(index/16+1)==barSelect.getSelectedId());};addAndMakeVisible(barSelect);
    playSequenceButton.addListener(this);clearSequenceButton.addListener(this);exportSequenceButton.addListener(this);addAndMakeVisible(playSequenceButton);addAndMakeVisible(clearSequenceButton);addAndMakeVisible(exportSequenceButton);
    importMidiButton.addListener(this);addAndMakeVisible(importMidiButton);
    tempoEditor.setText("120");tempoEditor.setInputRestrictions(3,"0123456789");tempoEditor.setTooltip("Pattern preview and MIDI export tempo.");addAndMakeVisible(tempoEditor);
    profileSelect.addItem("General MIDI",1);profileSelect.setSelectedId(1);profileSelect.onChange=[this]{const auto name=profileSelect.getText();if(name=="General MIDI")mappingProfile=MidiMappingProfile::generalMidi();else if(const auto loaded=database.loadMappingProfile(name))mappingProfile=*loaded;else return;profileName.setText(name,juce::dontSendNotification);updateDrumNotes();refreshMappingSummary();savePluginEditorState();};addAndMakeVisible(profileSelect);
    profileName.setText("General MIDI");profileName.setTextToShowWhenEmpty("Mapping profile name",Palette::Muted);addAndMakeVisible(profileName);
    const char* instrumentNames[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<9;++i)mappingInstrument.addItem(instrumentNames[i],i+1);mappingInstrument.setSelectedId(1);addAndMakeVisible(mappingInstrument);
    const char* articulationNames[]{"Default","Center","Rimshot","SideStick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};for(int i=0;i<13;++i)mappingArticulation.addItem(articulationNames[i],i+1);mappingArticulation.setSelectedId(1);addAndMakeVisible(mappingArticulation);
    mappingNote.setTextToShowWhenEmpty("MIDI note 0–127",Palette::Muted);mappingNote.setInputRestrictions(3,"0123456789");addAndMakeVisible(mappingNote);
    for(auto* button:{&loadProfileButton,&saveProfileButton,&importCsvButton,&exportCsvButton,&setMappingButton,&removeMappingButton}){button->addListener(this);addAndMakeVisible(*button);}
    mappingSummary.setColour(juce::Label::textColourId,Palette::Text);mappingSummary.setJustificationType(juce::Justification::topLeft);addAndMakeVisible(mappingSummary);
    status.setColour(juce::Label::textColourId,Palette::Muted);status.setJustificationType(juce::Justification::centredLeft);addAndMakeVisible(status);
    const auto dataDirectory=getMagDrumDataDirectory();
    if(dataDirectory!=juce::File()&&(dataDirectory.createDirectory().wasOk()||dataDirectory.isDirectory())){databaseFile=dataDirectory.getChildFile("samples.sqlite3");databaseReady=database.open(databaseFile);}
    if(!databaseReady)status.setText("Sample index unavailable. Load WAVs directly or scan a folder in the MagDrum app.",juce::dontSendNotification);
    else{refreshSourceKitFilters();refreshSamples();for(const auto& name:database.getMappingProfileNames())if(name!="General MIDI")profileSelect.addItem(name,profileSelect.getNumItems()+1);if(const auto stored=database.loadMappingProfile("General MIDI"))mappingProfile=*stored;}
    restorePluginEditorState();updateDrumNotes();setNote(mappedPadNotes[0]);refreshMappingSummary();setPage(0);
}

MagDrumAudioProcessorEditor::~MagDrumAudioProcessorEditor(){stopTimer();if(scanCancellation)scanCancellation->store(true);scanWorkers.removeAllJobs(true,-1);setLookAndFeel(nullptr);}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(Palette::Background);g.setColour(Palette::Accent);g.setFont(juce::Font(juce::FontOptions(30.0f,juce::Font::bold)));g.drawText("MagDrum",24,14,260,40,juce::Justification::centredLeft);
    g.setColour(Palette::Muted);g.setFont(13.0f);g.drawText("SAMPLE LIBRARY  /  DRUM MACHINE  /  STEP SEQUENCER  /  MIDI MAPS",26,51,850,22,juce::Justification::centredLeft);
    g.setColour(Palette::Border);g.drawHorizontalLine(83,24.0f,static_cast<float>(getWidth()-24));
}

void MagDrumAudioProcessorEditor::resized()
{
    auto bounds=getLocalBounds().reduced(24);bounds.removeFromTop(67);auto tabs=bounds.removeFromTop(38);
    libraryTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);machineTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);sequencerTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);mapsTab.setBounds(tabs.removeFromLeft(160));bounds.removeFromTop(12);
    auto footer=bounds.removeFromBottom(26);status.setBounds(footer);bounds.removeFromBottom(8);
    libraryTab.setToggleState(page==0,juce::dontSendNotification);machineTab.setToggleState(page==1,juce::dontSendNotification);sequencerTab.setToggleState(page==2,juce::dontSendNotification);mapsTab.setToggleState(page==3,juce::dontSendNotification);
    sampleSearch.setVisible(page==0);categoryFilter.setVisible(page==0);featureFilter.setVisible(page==0);sortFilter.setVisible(page==0);confidenceFilter.setVisible(page==0);sourceFilter.setVisible(page==0);kitFilter.setVisible(page==0);scanButton.setVisible(page==0);searchButton.setVisible(page==0);previewButton.setVisible(page==0);assignButton.setVisible(page==0);libraryToolsButton.setVisible(page==0);moreSamplesButton.setVisible(page==0);indexedList.setVisible(page==0);sampleMap->setVisible(page==0);
    for(auto& pad:pads)pad.setVisible(page==1);selectedPadTitle.setVisible(page==1);layerSummary.setVisible(page==1);sampleDetails.setVisible(page==1);waveform->setVisible(page==1);velocityRange.setVisible(page==1);clearPadButton.setVisible(page==1);loadButton.setVisible(page==1);
    for(auto& label:laneLabels)label.setVisible(page==2);for(auto& row:steps)for(size_t index=0;index<row.size();++index)row[index].setVisible(page==2&&static_cast<int>(index/16+1)==barSelect.getSelectedId());playSequenceButton.setVisible(page==2);clearSequenceButton.setVisible(page==2);exportSequenceButton.setVisible(page==2);importMidiButton.setVisible(page==2);tempoEditor.setVisible(page==2);barSelect.setVisible(page==2);
    profileSelect.setVisible(page==3);profileName.setVisible(page==3);mappingInstrument.setVisible(page==3);mappingArticulation.setVisible(page==3);mappingNote.setVisible(page==3);mappingSummary.setVisible(page==3);loadProfileButton.setVisible(page==3);saveProfileButton.setVisible(page==3);importCsvButton.setVisible(page==3);exportCsvButton.setVisible(page==3);setMappingButton.setVisible(page==3);removeMappingButton.setVisible(page==3);
    if(page==0){auto controls=bounds.removeFromTop(34);sampleSearch.setBounds(controls.removeFromLeft(225));controls.removeFromLeft(6);categoryFilter.setBounds(controls.removeFromLeft(135));controls.removeFromLeft(6);featureFilter.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);sortFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);confidenceFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);searchButton.setBounds(controls.removeFromLeft(78));controls.removeFromLeft(6);previewButton.setBounds(controls.removeFromLeft(78));bounds.removeFromTop(6);controls=bounds.removeFromTop(34);sourceFilter.setBounds(controls.removeFromLeft(180));controls.removeFromLeft(6);kitFilter.setBounds(controls.removeFromLeft(190));controls.removeFromLeft(6);scanButton.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);assignButton.setBounds(controls.removeFromLeft(118));controls.removeFromLeft(6);libraryToolsButton.setBounds(controls.removeFromLeft(115));controls.removeFromLeft(6);moreSamplesButton.setBounds(controls.removeFromLeft(110));moreSamplesButton.setEnabled(hasMoreSamples);bounds.removeFromTop(8);auto content=bounds;const int left=juce::jlimit(320,440,content.getWidth()*2/5);indexedList.setBounds(content.removeFromLeft(left));content.removeFromLeft(12);sampleMap->setBounds(content);}
    else if(page==1){auto grid=bounds.removeFromLeft(bounds.getWidth()*3/5);const int gap=10;const int padW=(grid.getWidth()-3*gap)/4,padH=(grid.getHeight()-3*gap)/4;for(size_t i=0;i<pads.size();++i){const int row=static_cast<int>(i/4),col=static_cast<int>(i%4);pads[i].setBounds(grid.getX()+col*(padW+gap),grid.getY()+row*(padH+gap),padW,padH);}bounds.removeFromLeft(18);selectedPadTitle.setBounds(bounds.removeFromTop(38));bounds.removeFromTop(6);sampleDetails.setBounds(bounds.removeFromTop(58));bounds.removeFromTop(8);waveform->setBounds(bounds.removeFromTop(154));bounds.removeFromTop(8);layerSummary.setBounds(bounds.removeFromTop(juce::jmin(180,bounds.getHeight()/2)));bounds.removeFromTop(12);auto row=bounds.removeFromTop(34);velocityRange.setBounds(row.removeFromLeft(100));row.removeFromLeft(8);clearPadButton.setBounds(row.removeFromLeft(108));row.removeFromLeft(8);loadButton.setBounds(row);bounds.removeFromTop(14);}
    else if(page==2){auto toolbar=bounds.removeFromTop(36);playSequenceButton.setBounds(toolbar.removeFromLeft(82));toolbar.removeFromLeft(6);clearSequenceButton.setBounds(toolbar.removeFromLeft(116));toolbar.removeFromLeft(6);importMidiButton.setBounds(toolbar.removeFromLeft(96));toolbar.removeFromLeft(6);exportSequenceButton.setBounds(toolbar.removeFromLeft(110));toolbar.removeFromLeft(12);tempoEditor.setBounds(toolbar.removeFromLeft(70));toolbar.removeFromLeft(8);barSelect.setBounds(toolbar.removeFromLeft(92));bounds.removeFromTop(16);const int labelW=122;const int cellW=juce::jmax(30,(bounds.getWidth()-labelW-15*4)/16);const size_t barOffset=static_cast<size_t>(juce::jlimit(0,3,barSelect.getSelectedId()-1))*16;for(int row=0;row<8;++row){auto line=bounds.removeFromTop(juce::jmax(36,bounds.getHeight()/8));laneLabels[static_cast<size_t>(row)].setBounds(line.removeFromLeft(labelW));line.removeFromLeft(6);for(int step=0;step<16;++step){steps[static_cast<size_t>(row)][barOffset+static_cast<size_t>(step)].setBounds(line.removeFromLeft(cellW));line.removeFromLeft(4);}}}
    else{auto top=bounds.removeFromTop(36);profileSelect.setBounds(top.removeFromLeft(210));top.removeFromLeft(8);profileName.setBounds(top.removeFromLeft(190));top.removeFromLeft(8);loadProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);saveProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);importCsvButton.setBounds(top.removeFromLeft(95));top.removeFromLeft(6);exportCsvButton.setBounds(top.removeFromLeft(95));bounds.removeFromTop(12);auto edit=bounds.removeFromTop(36);mappingInstrument.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingArticulation.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingNote.setBounds(edit.removeFromLeft(110));edit.removeFromLeft(8);setMappingButton.setBounds(edit.removeFromLeft(120));edit.removeFromLeft(8);removeMappingButton.setBounds(edit.removeFromLeft(130));bounds.removeFromTop(14);mappingSummary.setBounds(bounds);}
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
    loadSamplePage(true);
}
void MagDrumAudioProcessorEditor::refreshSourceKitFilters()
{
    const auto priorSource=sourceFilter.getText(),priorKit=kitFilter.getText();sourceFilter.clear(juce::dontSendNotification);sourceFilter.addItem("All sources",1);sourceFilter.addItem("Sample folders",2);int sourceId=3;
    juce::StringArray pluginNames;for(const auto& plugin:database.getPluginInstruments())pluginNames.addIfNotAlreadyThere(plugin.name);
    for(const auto& name:pluginNames){sourceFilter.addItem(name,sourceId);if(name==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    for(const auto& folder:database.getSampleSources("folder")){const auto label="Folder: "+folder.identifier;sourceFilter.addItem(label,sourceId);if(label==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    if(priorSource=="Sample folders")sourceFilter.setSelectedId(2,juce::dontSendNotification);else if(priorSource=="All sources"||priorSource.isEmpty()||sourceFilter.getSelectedId()==0)sourceFilter.setSelectedId(1,juce::dontSendNotification);
    kitFilter.clear(juce::dontSendNotification);kitFilter.addItem("All kits",1);int kitId=2;for(const auto& name:database.getPluginKitNames()){kitFilter.addItem(name,kitId);if(name==priorKit)kitFilter.setSelectedId(kitId,juce::dontSendNotification);++kitId;}if(priorKit=="All kits"||priorKit.isEmpty()||kitFilter.getSelectedId()==0)kitFilter.setSelectedId(1,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::loadSamplePage(bool reset)
{
    if(!databaseReady)return;if(reset){samples.clear();sampleOffset=0;hasMoreSamples=false;}
    SampleBrowseFilter options;options.query=sampleSearch.getText();if(categoryFilter.getSelectedId()>1)options.category=categoryFilter.getText();options.feature=juce::jmax(1,featureFilter.getSelectedId());options.sort=juce::jmax(1,sortFilter.getSelectedId());options.needsReview=confidenceFilter.getSelectedId()==2;
    const auto source=sourceFilter.getText();if(source=="Sample folders")options.sourceType="folder";else if(source.startsWith("Folder: ")){options.sourceType="folder";options.sourceIdentifier=source.fromFirstOccurrenceOf("Folder: ",false,false);}else if(source!="All sources"&&source.isNotEmpty()){options.sourceType="plugin";options.pluginName=source;}
    const auto kit=kitFilter.getText();if(kit!="All kits"&&kit.isNotEmpty()){options.sourceType="plugin";if(options.pluginName.isEmpty())options.pluginName=kit.upToFirstOccurrenceOf(" / ",false,false);options.kitName=kit.fromFirstOccurrenceOf(" / ",false,false);}
    const auto result=database.searchSampleBrowseRecords(options,250,sampleOffset);for(const auto& record:result.records)samples.add(record);sampleOffset=samples.size();hasMoreSamples=result.hasMore;
    indexedList.updateContent();if(reset)indexedList.deselectAllRows();sampleMap->setSamples(samples,-1);moreSamplesButton.setEnabled(hasMoreSamples);status.setText(juce::String(samples.size())+(hasMoreSamples?"+":"")+" samples · select, preview or use Sample tools",juce::dontSendNotification);
}
juce::File MagDrumAudioProcessorEditor::selectedSample()const{const int row=indexedList.getSelectedRow();return juce::isPositiveAndBelow(row,samples.size())?samples.getReference(row).file:juce::File();}

void MagDrumAudioProcessorEditor::chooseSampleFolder()
{
    if(scanning){if(scanCancellation)scanCancellation->store(true);scanButton.setButtonText("Cancelling…");scanButton.setEnabled(false);status.setText("Stopping the current folder scan…",juce::dontSendNotification);return;}
    chooser=std::make_unique<juce::FileChooser>("Choose a drum sample folder",juce::File(),juce::String());
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser& fileChooser){if(safe!=nullptr){const auto folder=fileChooser.getResult();if(folder.isDirectory())safe->startSampleScan(folder);}});
}

void MagDrumAudioProcessorEditor::startSampleScan(const juce::File& folder)
{
    if(!databaseReady||databaseFile==juce::File()){status.setText("The shared MagDrum sample database is unavailable.",juce::dontSendNotification);return;}
    scanning=true;scanCancellation=std::make_shared<std::atomic<bool>>(false);scanButton.setEnabled(true);scanButton.setButtonText("Cancel scan");status.setText("Scanning "+folder.getFileName()+"…",juce::dontSendNotification);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    auto completion=[safe](const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error){juce::MessageManager::callAsync([safe,stats,indexed,error]{if(safe!=nullptr)safe->finishSampleScan(stats,indexed,error);});};
    auto progress=[safe](const juce::File& file){juce::MessageManager::callAsync([safe,name=file.getFileName()]{if(safe!=nullptr&&safe->scanning)safe->status.setText("Scanning sample: "+name,juce::dontSendNotification);});};
    scanWorkers.addJob(new PluginSampleScanJob(folder,databaseFile,scanCancellation,std::move(completion),std::move(progress)),true);
}

void MagDrumAudioProcessorEditor::finishSampleScan(const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error)
{
    scanning=false;scanCancellation.reset();scanButton.setEnabled(true);scanButton.setButtonText("Add sample folder");refreshSourceKitFilters();refreshSamples();
    if(error.isNotEmpty())status.setText(error,juce::dontSendNotification);
    else status.setText((stats.cancelled?"Scan cancelled. ":"Scan complete. ")+juce::String(indexed)+" indexed, "+juce::String(stats.filesSkippedUnchanged)+" unchanged, "+juce::String(stats.errors)+" errors.",juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::showLibraryTools()
{
    juce::PopupMenu menu;menu.addItem(1,"Edit sample tags…");
    juce::PopupMenu categories;const char* names[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<12;++i)categories.addItem(100+i,names[i]);menu.addSubMenu("Set drum category",categories);
    menu.addSeparator();menu.addItem(2,"Find similar samples");menu.addItem(3,"Find exact duplicates");menu.addItem(4,"Find near duplicates");menu.addItem(5,"Compare two selected waveforms");menu.addSeparator();menu.addItem(6,"Scan history");menu.addItem(7,"Export portable metadata…");menu.addItem(8,"Import portable metadata…");
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryToolsButton),[safe](int result){if(safe!=nullptr)safe->handleLibraryTool(result);});
}

void MagDrumAudioProcessorEditor::handleLibraryTool(int action)
{
    if(action>=100&&action<112)
    {
        const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample before changing its category.",juce::dontSendNotification);return;}
        static const char* categories[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        if(database.setUserCategory(file,categories[action-100])){refreshSamples();status.setText("Category saved as "+juce::String(categories[action-100])+".",juce::dontSendNotification);}else status.setText("Could not save the sample category.",juce::dontSendNotification);return;
    }
    if(action==0)return;
    if(action==1)
    {
        const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample before editing its tags.",juce::dontSendNotification);return;}
        auto* dialog=new juce::AlertWindow("Edit sample tags",file.getFileName(),juce::AlertWindow::NoIcon);dialog->addTextEditor("tags",database.getUserTags(file).joinIntoString(", "),"Comma-separated tags");dialog->addButton("Save",1);dialog->addButton("Cancel",0);dialog->setSize(480,180);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,safeDialog,file](int result){if(result!=1||safe==nullptr||safeDialog==nullptr)return;juce::StringArray tags;tags.addTokens(safeDialog->getTextEditorContents("tags"),",","\"");tags.removeEmptyStrings();if(safe->database.setUserTags(file,tags)){safe->refreshSamples();safe->status.setText("Sample tags saved.",juce::dontSendNotification);}else safe->status.setText("Could not save sample tags.",juce::dontSendNotification);}),true);return;
    }
    const auto file=selectedSample();
    if(action>=2&&action<=4&&file==juce::File()){status.setText("Select a sample first.",juce::dontSendNotification);return;}
    if(action==2)
    {
        const auto matches=database.findSimilar(file,20);juce::String details;for(const auto& match:matches)details<<match.getFileName()<<"\n"<<match.getFullPathName()<<"\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Similar sample candidates",matches.isEmpty()?"No similar samples found.":details,"Close");return;
    }
    if(action==3)
    {
        const auto matches=database.findExactDuplicates(file,100);juce::String details;for(const auto& match:matches)details<<match.getFileName()<<"\n"<<match.getFullPathName()<<"\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Exact file duplicates",matches.isEmpty()?"No exact duplicates found.":details,"Close");return;
    }
    if(action==4)
    {
        const auto matches=database.findPerceptualDuplicates(file);juce::String details;for(const auto& match:matches)details<<juce::String(match.similarity*100.0,1)<<"%  "<<match.file.getFileName()<<"\n"<<match.file.getFullPathName()<<"\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Near-duplicate candidates",matches.empty()?"No likely near duplicates found.":details+"\nThese are feature-based candidates. Listen before replacing or deleting samples.","Close");return;
    }
    if(action==5)
    {
        const auto selected=indexedList.getSelectedRows();if(selected.size()!=2){status.setText("Ctrl-click exactly two samples in the list to compare their waveforms.",juce::dontSendNotification);return;}const auto first=juce::isPositiveAndBelow(selected[0],samples.size())?samples.getReference(selected[0]).file:juce::File();const auto second=juce::isPositiveAndBelow(selected[1],samples.size())?samples.getReference(selected[1]).file:juce::File();if(first==juce::File()||second==juce::File())return;comparisonWindow=std::make_unique<MagDrumSampleComparisonWindow>(first,second);return;
    }
    if(action==6)
    {
        const auto scans=database.getRecentSampleScans();juce::String report;for(const auto& scan:scans)report<<scan.startedAt<<"  ·  "<<scan.status<<"\n"<<scan.rootPath<<"\n"<<scan.filesIndexed<<" indexed, "<<scan.filesSkipped<<" unchanged, "<<scan.errors<<" errors\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Sample scan history",report.isEmpty()?"No sample scans have been recorded.":report,"Close");return;
    }
    if(action==7||action==8)
    {
        const bool exporting=action==7;chooser=std::make_unique<juce::FileChooser>(exporting?"Export MagDrum metadata":"Import MagDrum metadata",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Metadata.json"),"*.json");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(exporting?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),[safe,exporting](const juce::FileChooser& fc){if(safe==nullptr)return;auto target=fc.getResult();if(target==juce::File())return;if(exporting){const auto json=juce::JSON::toString(safe->database.exportPortableMetadata(),true);if(target.getFileExtension().isEmpty())target=target.withFileExtension(".json");const bool saved=target.replaceWithText(json);safe->status.setText(saved?"Portable metadata exported.":"Could not write metadata archive.",juce::dontSendNotification);}else{const auto archive=juce::JSON::parse(target.loadFileAsString());if(archive.isVoid()){safe->status.setText("The metadata archive is not valid JSON.",juce::dontSendNotification);return;}const auto merged=safe->database.mergePortableMetadata(archive);safe->refreshSamples();safe->status.setText(merged.ok?"Metadata imported: "+juce::String(merged.tagsAdded)+" tags added, "+juce::String(merged.profilesAdded)+" mapping profiles added.":"Metadata import failed: "+merged.error,juce::dontSendNotification);}});return;
    }
}

void MagDrumAudioProcessorEditor::setPage(int value){page=juce::jlimit(0,3,value);resized();repaint();}
void MagDrumAudioProcessorEditor::setNote(int note){selectedNote=juce::jlimit(0,127,note);const int index=static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin());const int safeIndex=juce::jlimit(0,static_cast<int>(padNames.size())-1,index);for(size_t i=0;i<pads.size();++i){pads[i].setButtonText(juce::String(padNames[i])+"\n"+noteName(mappedPadNotes[i]));pads[i].setToggleState(static_cast<int>(i)==index,juce::dontSendNotification);pads[i].setColour(juce::TextButton::buttonColourId,static_cast<int>(i)==index?categoryColour(padNames[i]).withAlpha(0.34f):Palette::Surface);}selectedPadTitle.setText(juce::String(padNames[static_cast<size_t>(safeIndex)])+"   ·   "+noteName(selectedNote)+"   ·   MIDI "+juce::String(selectedNote),juce::dontSendNotification);updateLayerSummary();const auto layers=processor.getSampleLayersForNote(selectedNote);sampleDetails.setText(layers.empty()?"Selected pad sound\nNo sample loaded":juce::String("PAD SAMPLE\n")+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path,juce::dontSendNotification);}
void MagDrumAudioProcessorEditor::updateDrumNotes()
{
    int selectedPad=static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin());selectedPad=juce::jlimit(0,static_cast<int>(mappedPadNotes.size())-1,selectedPad);
    mappedPadNotes=defaultPadNotes;mappedLaneNotes=defaultLaneNotes;
    setNote(mappedPadNotes[static_cast<size_t>(selectedPad)]);
}

void MagDrumAudioProcessorEditor::refreshMappingSummary()
{
    static const char* instruments[]{"Kick","Snare","HiHat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};static const char* articulations[]{"Default","Center","Rimshot","SideStick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
    juce::String text="PROFILE: "+profileName.getText()+"\nThis profile maps MIDI file import/export. Cubase pads remain on their named GM notes.\n\n";for(const auto& entry:mappingProfile.entries())text+=juce::String(instruments[static_cast<size_t>(entry.instrument)])+"  /  "+articulations[static_cast<size_t>(entry.articulation)]+"   →   MIDI "+juce::String(entry.note)+"  ("+noteName(entry.note)+")\n";mappingSummary.setText(text,juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::savePluginEditorState()
{
    juce::XmlElement saved("PLUGIN_EDITOR_STATE");
    saved.setAttribute("profileName",profileName.getText().trim());
    saved.setAttribute("profileCsv",juce::String::fromUTF8(mappingProfile.toCsv().c_str()));
    saved.setAttribute("tempo",tempoEditor.getText());
    for(size_t lane=0;lane<steps.size();++lane)for(size_t step=0;step<steps[lane].size();++step)if(steps[lane][step].getToggleState()){auto* cell=saved.createNewChildElement("STEP");cell->setAttribute("lane",static_cast<int>(lane));cell->setAttribute("step",static_cast<int>(step));}
    processor.setPluginEditorState(saved);
}

void MagDrumAudioProcessorEditor::restorePluginEditorState()
{
    const auto saved=processor.getPluginEditorState();if(!saved)return;
    const auto name=saved->getStringAttribute("profileName");const auto csv=saved->getStringAttribute("profileCsv");
    if(name.isNotEmpty()&&csv.isNotEmpty())if(const auto parsed=MidiMappingProfile::fromCsv(csv.toStdString())){mappingProfile=*parsed;profileName.setText(name,juce::dontSendNotification);if(name!="General MIDI"&&databaseReady){database.saveMappingProfile(name,mappingProfile);int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);}}
    const auto tempo=saved->getStringAttribute("tempo");if(tempo.isNotEmpty())tempoEditor.setText(tempo,juce::dontSendNotification);
    for(auto* child=saved->getFirstChildElement();child!=nullptr;child=child->getNextElement())if(child->hasTagName("STEP")){const int lane=child->getIntAttribute("lane",-1),step=child->getIntAttribute("step",-1);if(juce::isPositiveAndBelow(lane,static_cast<int>(steps.size()))&&juce::isPositiveAndBelow(step,64))steps[static_cast<size_t>(lane)][static_cast<size_t>(step)].setToggleState(true,juce::dontSendNotification);}
}

void MagDrumAudioProcessorEditor::chooseMappingCsv(bool exportCsv)
{
    chooser=std::make_unique<juce::FileChooser>(exportCsv?"Export MIDI mapping profile":"Import MIDI mapping profile",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Mapping.csv"),"*.csv");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(exportCsv?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),[safe,exportCsv](const juce::FileChooser& fc){if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(exportCsv){if(file.getFileExtension().isEmpty())file=file.withFileExtension(".csv");const bool ok=file.replaceWithText(juce::String::fromUTF8(safe->mappingProfile.toCsv().c_str()));safe->status.setText(ok?"MIDI mapping profile exported.":"Could not write CSV profile.",juce::dontSendNotification);}else{const auto parsed=MidiMappingProfile::fromCsv(file.loadFileAsString().toStdString());if(!parsed){safe->status.setText("The CSV file is not a valid MIDI mapping profile.",juce::dontSendNotification);return;}safe->mappingProfile=*parsed;if(safe->profileName.getText().trim().isEmpty())safe->profileName.setText(file.getFileNameWithoutExtension(),juce::dontSendNotification);safe->database.saveMappingProfile(safe->profileName.getText(),safe->mappingProfile);safe->updateDrumNotes();safe->refreshMappingSummary();safe->savePluginEditorState();safe->status.setText("Imported MIDI mapping profile.",juce::dontSendNotification);}});
}

void MagDrumAudioProcessorEditor::importMidiPattern()
{
    chooser=std::make_unique<juce::FileChooser>("Import drum MIDI pattern",juce::File(),"*.mid;*.midi");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr)return;const auto file=fc.getResult();if(!file.existsAsFile())return;MidiPattern pattern;const auto report=MidiPatternIO{}.read(file,safe->mappingProfile,pattern);if(!report.ok){safe->status.setText("Could not import the selected MIDI file.",juce::dontSendNotification);return;}for(auto& row:safe->steps)for(auto& cell:row)cell.setToggleState(false,juce::dontSendNotification);int imported=0,outside=0;for(const auto& event:pattern.events){int lane=-1;switch(event.instrument){case DrumInstrument::Kick:lane=0;break;case DrumInstrument::Snare:case DrumInstrument::Rimshot:lane=1;break;case DrumInstrument::HiHat:lane=event.articulation==DrumArticulation::Open?3:2;break;case DrumInstrument::Tom:lane=4;break;case DrumInstrument::Ride:lane=5;break;case DrumInstrument::Crash:lane=6;break;case DrumInstrument::Clap:lane=7;break;default:break;}const int step=juce::roundToInt(event.positionBeats*4.0);if(lane<0||step<0||step>=64){++outside;continue;}safe->steps[static_cast<size_t>(lane)][static_cast<size_t>(step)].setToggleState(true,juce::dontSendNotification);++imported;}for(const auto& meta:pattern.tempoAndSignature)if(meta.message.isTempoMetaEvent()){const double seconds=meta.message.getTempoSecondsPerQuarterNote();if(seconds>0.0)safe->tempoEditor.setText(juce::String(juce::roundToInt(60.0/seconds)),juce::dontSendNotification);break;}safe->savePluginEditorState();safe->status.setText("Imported "+juce::String(imported)+" hits from "+file.getFileName()+". "+juce::String(report.unmappedNotes.size())+" unmapped notes; "+juce::String(report.ambiguousNotes.size())+" ambiguous notes; "+juce::String(outside)+" hits outside the four-bar grid.",juce::dontSendNotification);});
}
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
    if(button==&libraryTab){setPage(0);return;}if(button==&machineTab){setPage(1);return;}if(button==&sequencerTab){setPage(2);return;}if(button==&mapsTab){setPage(3);return;}
    for(auto& row:steps)for(auto& cell:row)if(button==&cell){savePluginEditorState();return;}
    if(button==&scanButton){chooseSampleFolder();return;}
    if(button==&searchButton){refreshSamples();return;}if(button==&libraryToolsButton){showLibraryTools();return;}if(button==&moreSamplesButton){if(hasMoreSamples)loadSamplePage(false);return;}
    if(button==&previewButton){const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample to preview.",juce::dontSendNotification);return;}status.setText(processor.previewSample(file)?"Previewing "+file.getFileName():"Could not read this audio file.",juce::dontSendNotification);return;}
    if(button==&assignButton){const auto file=selectedSample();if(file==juce::File()){status.setText("Select a sample first.",juce::dontSendNotification);return;}if(processor.loadSampleForVelocityRange(selectedNote,velocityMinimum(velocityRange.getText()),velocityMaximum(velocityRange.getText()),file)){updateLayerSummary();status.setText("Assigned "+file.getFileName()+" to "+juce::String(padNames[static_cast<size_t>(std::max(0,static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin())))])+".",juce::dontSendNotification);}else status.setText("Assignment failed. Check velocity ranges and the eight-layer limit.",juce::dontSendNotification);return;}
    for(size_t i=0;i<pads.size();++i)if(button==&pads[i]){setNote(mappedPadNotes[i]);processor.triggerNote(selectedNote);status.setText(processor.getSampleLayersForNote(selectedNote).empty()?"Pad selected · no sample assigned yet.":"Played "+juce::String(padNames[i]),juce::dontSendNotification);return;}
    if(button==&clearPadButton){status.setText(processor.clearSamplesForNote(selectedNote)?"Pad cleared.":"Pad is already empty.",juce::dontSendNotification);updateLayerSummary();return;}
    if(button==&loadButton){chooser=std::make_unique<juce::FileChooser>("Choose a drum sample",juce::File(),"*.wav;*.aif;*.aiff;*.flac",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr)return;const auto file=fc.getResult();if(!file.existsAsFile())return;if(safe->processor.loadSampleForVelocityRange(safe->selectedNote,velocityMinimum(safe->velocityRange.getText()),velocityMaximum(safe->velocityRange.getText()),file)){safe->updateLayerSummary();safe->status.setText("Loaded "+file.getFileName(),juce::dontSendNotification);}else safe->status.setText("Could not assign sample. Check range, format and layer limit.",juce::dontSendNotification);});return;}
    if(button==&loadProfileButton){const auto name=profileSelect.getText();if(name=="General MIDI"){mappingProfile=MidiMappingProfile::generalMidi();}else if(const auto loaded=database.loadMappingProfile(name))mappingProfile=*loaded;else{status.setText("Could not load mapping profile.",juce::dontSendNotification);return;}profileName.setText(name,juce::dontSendNotification);updateDrumNotes();refreshMappingSummary();status.setText("Loaded MIDI profile: "+name,juce::dontSendNotification);return;}
    if(button==&saveProfileButton){const auto name=profileName.getText().trim();if(name.isEmpty()||!database.saveMappingProfile(name,mappingProfile)){status.setText("Could not save mapping profile.",juce::dontSendNotification);return;}int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);savePluginEditorState();status.setText("Saved MIDI profile: "+name,juce::dontSendNotification);return;}
    if(button==&setMappingButton||button==&removeMappingButton){const int instrument=mappingInstrument.getSelectedId()-1,articulation=mappingArticulation.getSelectedId()-1,note=mappingNote.getText().getIntValue();if(instrument<0||articulation<0||(button==&setMappingButton&&(note<0||note>127))){status.setText("Choose a drum, articulation and MIDI note from 0 to 127.",juce::dontSendNotification);return;}const bool changed=button==&setMappingButton?mappingProfile.setMapping({static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation),static_cast<std::uint8_t>(note)}):mappingProfile.removeMapping(static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation));if(changed){database.saveMappingProfile(profileName.getText().trim().isEmpty()?"General MIDI":profileName.getText().trim(),mappingProfile);updateDrumNotes();refreshMappingSummary();savePluginEditorState();status.setText("MIDI mapping updated.",juce::dontSendNotification);}else status.setText("No matching mapping was changed.",juce::dontSendNotification);return;}
    if(button==&importCsvButton){chooseMappingCsv(false);return;}if(button==&exportCsvButton){chooseMappingCsv(true);return;}if(button==&importMidiButton){importMidiPattern();return;}
    if(button==&playSequenceButton){if(sequencerPlaying){sequencerPlaying=false;stopTimer();playSequenceButton.setButtonText("Play");}else{const int bpm=tempoEditor.getText().getIntValue();if(bpm<30||bpm>300){status.setText("Tempo must be between 30 and 300 BPM.",juce::dontSendNotification);return;}savePluginEditorState();sequenceTempo=bpm;sequenceStep=0;sequencerPlaying=true;playSequenceButton.setButtonText("Stop");startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*4.0))));}return;}
    if(button==&clearSequenceButton){for(auto& row:steps)for(auto& cell:row)cell.setToggleState(false,juce::dontSendNotification);savePluginEditorState();status.setText("Pattern cleared.",juce::dontSendNotification);return;}
    if(button==&exportSequenceButton){const int bpm=tempoEditor.getText().getIntValue();if(bpm<30||bpm>300){status.setText("Tempo must be between 30 and 300 BPM.",juce::dontSendNotification);return;}savePluginEditorState();MidiPattern pattern;pattern.lengthBeats=16.0;setPatternTempo(pattern,bpm);setPatternTimeSignature(pattern,4,4);for(int row=0;row<8;++row)for(int step=0;step<64;++step)if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState()){DrumEvent event;event.instrument=laneInstruments[static_cast<size_t>(row)];event.articulation=laneArticulations[static_cast<size_t>(row)];event.velocity=100;event.positionBeats=step*0.25;event.durationBeats=0.12;pattern.events.push_back(event);}chooser=std::make_unique<juce::FileChooser>("Export drum pattern",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Pattern.mid"),"*.mid",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe,pattern](const juce::FileChooser& fc){if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=file.withFileExtension(".mid");std::vector<DrumEvent> unsupported;std::vector<MidiFallbackUse> fallbacks;const bool ok=MidiPatternIO{}.write(file,pattern,safe->mappingProfile,true,&unsupported,&fallbacks);safe->status.setText(ok?"Exported MIDI pattern: "+file.getFileName()+". "+juce::String(fallbacks.size())+" mapping fallbacks.":"Could not write MIDI file.",juce::dontSendNotification);});return;}
}

void MagDrumAudioProcessorEditor::timerCallback()
{
    if(!sequencerPlaying)return;const int step=sequenceStep;for(int row=0;row<8;++row)if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState())processor.triggerNote(mappedLaneNotes[static_cast<size_t>(row)],100);sequenceStep=(sequenceStep+1)%64;
}
