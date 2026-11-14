#include "PluginEditor.h"
#include "../core/MagDrumPaths.h"
#include "../core/PluginSourceKitRequest.h"
#include "../core/DefaultLibraryDiscovery.h"
#include "../core/ScopedFileAccess.h"
#include "../core/WavScanner.h"
#include "../ui/MagDrumLocalization.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
using magdrum::ui::Palette;
// Pad array order is row-major to match the Boss DR-660: kick/snare/pedal HH/cowbell on the bottom row.
constexpr std::array<int, 16> defaultPadNotes{49,57,53,51,50,47,45,43,37,39,42,46,36,38,44,56};
constexpr std::array<const char*, 16> padNames{"Crash 1","Crash 2","Bell","Ride","Tom 1","Tom 2","Tom 3","Tom 4","Side Stick","Clap","Closed HH","Open HH","Kick","Snare","Pedal HH","Cowbell"};
constexpr std::array<int, 8> defaultLaneNotes{36,38,42,46,43,51,49,39};
constexpr std::array<const char*, 8> laneNames{"KICK","SNARE","CLOSED HAT","OPEN HAT","TOM","RIDE","CRASH","CLAP"};
constexpr std::array<const char*, 8> laneDisplayNames{"Kick","Snare","Closed hi-hat","Open hi-hat","Tom","Ride","Crash","Clap"};
using I=DrumInstrument;using A=DrumArticulation;
constexpr std::array<I,16> padInstruments{I::Crash,I::Crash,I::Ride,I::Ride,I::Tom,I::Tom,I::Tom,I::Tom,I::Rimshot,I::Clap,I::HiHat,I::HiHat,I::Kick,I::Snare,I::HiHat,I::Percussion};
constexpr std::array<A,16> padArticulations{A::Default,A::Default,A::Bell,A::Bow,A::High,A::Mid,A::Low,A::Low,A::SideStick,A::Default,A::Closed,A::Open,A::Default,A::Default,A::Pedal,A::Default};
constexpr std::array<I,8> laneInstruments{I::Kick,I::Snare,I::HiHat,I::HiHat,I::Tom,I::Ride,I::Crash,I::Clap};
constexpr std::array<A,8> laneArticulations{A::Default,A::Default,A::Closed,A::Open,A::Low,A::Bow,A::Default,A::Default};
juce::Colour categoryColour(const juce::String& value) { return Palette::forDrumCategory(value); }
juce::String noteName(int note)
{
    static constexpr const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}
juce::String localizedPadName(size_t index)
{
    const auto source = juce::String(padNames[index]);
    if (source.startsWith("Crash ")) return magdrum::ui::localisedText("Crash") + source.fromFirstOccurrenceOf(" ", false, false);
    if (source.startsWith("Tom ")) return magdrum::ui::localisedText("Tom") + source.fromFirstOccurrenceOf(" ", false, false);
    if (source == "Side Stick") return magdrum::ui::localisedText("Side stick");
    if (source == "Closed HH") return magdrum::ui::localisedText("Closed hi-hat");
    if (source == "Open HH") return magdrum::ui::localisedText("Open hi-hat");
    if (source == "Pedal HH") return magdrum::ui::localisedText("Pedal hi-hat");
    return magdrum::ui::localisedText(padNames[index]);
}
juce::File bundledScannerApplication()
{
#if JUCE_WINDOWS
    static int moduleAnchor = 0;
    HMODULE module = nullptr;
    const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&moduleAnchor), &module))
        return {};
    std::array<wchar_t, 32768> modulePath{};
    const auto length = GetModuleFileNameW(module, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (length == 0 || length >= modulePath.size())
        return {};
    return juce::File(juce::String(modulePath.data(), static_cast<int>(length)))
        .getParentDirectory().getChildFile("MagDrum.exe");
#else
    return {};
#endif
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

class PluginMidiEventEditor final : public juce::Component, private juce::ListBoxModel, private juce::Button::Listener
{
public:
    PluginMidiEventEditor(MidiPattern& value,std::function<void()> callback):pattern(value),onChange(std::move(callback)),events("Drum events",this)
    {
        events.setRowHeight(27);addAndMakeVisible(events);
        const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
        for(int i=0;i<9;++i)instrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);
        for(int i=0;i<13;++i)articulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);
        instrument.setSelectedId(1,juce::dontSendNotification);articulation.setSelectedId(1,juce::dontSendNotification);velocity.setText("100",juce::dontSendNotification);beat.setText("0",juce::dontSendNotification);duration.setText("0.12",juce::dontSendNotification);
        for(auto* component:{static_cast<juce::Component*>(&instrument),static_cast<juce::Component*>(&articulation),static_cast<juce::Component*>(&velocity),static_cast<juce::Component*>(&beat),static_cast<juce::Component*>(&duration),static_cast<juce::Component*>(&apply),static_cast<juce::Component*>(&add),static_cast<juce::Component*>(&remove)})addAndMakeVisible(*component);
        velocity.setInputRestrictions(3,"0123456789");beat.setInputRestrictions(12,"0123456789.");duration.setInputRestrictions(12,"0123456789.");
        velocity.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity"),Palette::Muted);beat.setTextToShowWhenEmpty(magdrum::ui::localisedText("Beat"),Palette::Muted);duration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Length in beats"),Palette::Muted);
        apply.setButtonText(magdrum::ui::localisedText("Apply edit"));add.setButtonText(magdrum::ui::localisedText("Add hit"));remove.setButtonText(magdrum::ui::localisedText("Delete hit"));
        for(auto* button:{&apply,&add,&remove})button->addListener(this);
        message.setColour(juce::Label::textColourId,Palette::Muted);addAndMakeVisible(message);
        message.setText(magdrum::ui::localisedText("Select a hit to edit its drum, articulation, velocity, beat and duration."),juce::dontSendNotification);
        refresh();
    }
    void resized() override
    {
        auto area=getLocalBounds().reduced(10);events.setBounds(area.removeFromTop(juce::jmax(150,area.getHeight()-78)));area.removeFromTop(6);
        auto row=area.removeFromTop(32);instrument.setBounds(row.removeFromLeft(125));row.removeFromLeft(5);articulation.setBounds(row.removeFromLeft(120));row.removeFromLeft(5);velocity.setBounds(row.removeFromLeft(78));row.removeFromLeft(5);beat.setBounds(row.removeFromLeft(110));row.removeFromLeft(5);duration.setBounds(row.removeFromLeft(115));row.removeFromLeft(5);apply.setBounds(row.removeFromLeft(90));row.removeFromLeft(5);add.setBounds(row.removeFromLeft(76));row.removeFromLeft(5);remove.setBounds(row.removeFromLeft(92));
        area.removeFromTop(4);message.setBounds(area.removeFromTop(26));
    }
private:
    int getNumRows() override{return static_cast<int>(pattern.events.size());}
    void paintListBoxItem(int row,juce::Graphics& g,int width,int height,bool selected) override
    {
        if(!juce::isPositiveAndBelow(row,pattern.events.size()))return;
        if(selected)g.fillAll(Palette::Selection);
        const auto& event=pattern.events[static_cast<size_t>(row)];
        static constexpr const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        static constexpr const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
        const auto instrumentIndex=static_cast<size_t>(event.instrument),articulationIndex=static_cast<size_t>(event.articulation);
        const auto instrumentName=magdrum::ui::localisedText(instrumentIndex<9?instruments[instrumentIndex]:"Unknown");const auto articulationName=magdrum::ui::localisedText(articulationIndex<13?articulations[articulationIndex]:"Unknown");
        g.setColour(Palette::Text);g.drawText(juce::String(row+1)+".  "+instrumentName+" / "+articulationName+"    vel "+juce::String(event.velocity)+"    beat "+juce::String(event.positionBeats,3)+"    length "+juce::String(event.durationBeats,3),8,0,width-16,height,juce::Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override
    {
        if(!juce::isPositiveAndBelow(row,pattern.events.size()))return;
        const auto& event=pattern.events[static_cast<size_t>(row)];instrument.setSelectedId(static_cast<int>(event.instrument)+1,juce::dontSendNotification);articulation.setSelectedId(static_cast<int>(event.articulation)+1,juce::dontSendNotification);velocity.setText(juce::String(event.velocity),juce::dontSendNotification);beat.setText(juce::String(event.positionBeats,6),juce::dontSendNotification);duration.setText(juce::String(event.durationBeats,6),juce::dontSendNotification);
    }
    void buttonClicked(juce::Button* button) override
    {
        if(button==&remove){const int row=events.getSelectedRow();if(!juce::isPositiveAndBelow(row,pattern.events.size())){message.setText(magdrum::ui::localisedText("Select a hit first."),juce::dontSendNotification);return;}pattern.events.erase(pattern.events.begin()+row);refresh();events.selectRow(juce::jmin(row,static_cast<int>(pattern.events.size())-1));changed(magdrum::ui::localisedText("Hit deleted."));return;}
        const int selected=events.getSelectedRow();
        const int instrumentId=instrument.getSelectedId(),articulationId=articulation.getSelectedId(),velocityValue=velocity.getText().getIntValue();const double beatValue=beat.getText().getDoubleValue(),durationValue=duration.getText().getDoubleValue();
        if(instrumentId<1||instrumentId>9||articulationId<1||articulationId>13||velocityValue<1||velocityValue>127||!std::isfinite(beatValue)||beatValue<0.0||!std::isfinite(durationValue)||durationValue<=0.0){message.setText(magdrum::ui::localisedText("Enter a valid drum, articulation, velocity 1–127, non-negative beat and positive duration."),juce::dontSendNotification);return;}
        DrumEvent event{static_cast<DrumInstrument>(instrumentId-1),static_cast<DrumArticulation>(articulationId-1),static_cast<std::uint8_t>(velocityValue),beatValue,durationValue};
        if(button==&add)pattern.events.push_back(event);
        else if(button==&apply&&juce::isPositiveAndBelow(selected,pattern.events.size()))pattern.events[static_cast<size_t>(selected)]=event;
        else{message.setText(magdrum::ui::localisedText("Select a hit to edit, or choose Add hit."),juce::dontSendNotification);return;}
        std::stable_sort(pattern.events.begin(),pattern.events.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});refresh();changed(magdrum::ui::localisedText(button==&add?"Hit added.":"Hit updated and sorted by beat."));
    }
    void refresh(){events.updateContent();if(!pattern.events.empty()&&events.getSelectedRow()<0)events.selectRow(0);}
    void changed(const juce::String& text){message.setText(text,juce::dontSendNotification);if(onChange)onChange();}
    MidiPattern& pattern;std::function<void()> onChange;juce::ListBox events;juce::ComboBox instrument,articulation;juce::TextEditor velocity,beat,duration;juce::TextButton apply,add,remove;juce::Label message;
};

class PluginMidiEventWindow final : public juce::DocumentWindow
{
public:
    PluginMidiEventWindow(juce::Component* content,std::function<void()> onClose):DocumentWindow(magdrum::ui::localisedText("Edit MIDI events"),Palette::Panel,allButtons),closeCallback(std::move(onClose))
    {setUsingNativeTitleBar(true);setContentOwned(content,true);setResizable(true,true);centreWithSize(1040,430);setVisible(true);}
    void closeButtonPressed() override{if(closeCallback)closeCallback();}
private:
    std::function<void()> closeCallback;
};

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
        juce::ColourGradient panel(Palette::Surface, bounds.getX(), bounds.getY(),
                                   Palette::Panel, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(panel);g.fillRoundedRectangle(bounds,9.0f);
        auto plot=bounds.reduced(24.0f,24.0f);plot.removeFromTop(16.0f);plot.removeFromBottom(20.0f);
        g.setColour(Palette::Background.withAlpha(0.8f));g.fillRoundedRectangle(plot,5.0f);
        g.setColour(Palette::Border);g.drawRoundedRectangle(plot,5.0f,1.0f);
        for(int i=1;i<4;++i){const float x=plot.getX()+plot.getWidth()*i/4.0f;const float y=plot.getY()+plot.getHeight()*i/4.0f;g.setColour(Palette::Accent.withAlpha(0.10f));g.drawVerticalLine(static_cast<int>(x),plot.getY(),plot.getBottom());g.drawHorizontalLine(static_cast<int>(y),plot.getX(),plot.getRight());}
        g.setColour(Palette::Muted);g.setFont(11.0f);g.drawText(magdrum::ui::localisedText("SAMPLE MAP  ·  brightness × duration"),bounds.toNearestInt().removeFromTop(24).reduced(12,0),juce::Justification::centredLeft);
        g.drawText(magdrum::ui::localisedText("SHORTER"),static_cast<int>(plot.getX()),static_cast<int>(plot.getBottom()+4),80,14,juce::Justification::left);
        g.drawText(magdrum::ui::localisedText("LONGER"),static_cast<int>(plot.getRight()-80),static_cast<int>(plot.getBottom()+4),80,14,juce::Justification::right);
        for(int i=0;i<samples.size();++i)
        {
            const auto& sample=samples.getReference(i);
            const double duration=juce::jlimit(0.0,1.0,std::log10(juce::jmax(0.01,sample.durationSeconds)+0.01)/2.0);
            const double brightness=juce::jlimit(0.0,1.0,std::log10(juce::jmax(100.0,sample.brightnessHz)/100.0)/2.5);
            const auto x=plot.getX()+static_cast<float>(duration)*(plot.getWidth()-12.0f)+6.0f;
            const auto y=plot.getBottom()-static_cast<float>(brightness)*(plot.getHeight()-12.0f)-6.0f;
            const bool selected=i==selectedIndex;const float radius=selected?7.0f:4.2f;
            if(selected){g.setColour(categoryColour(sample.category).withAlpha(0.18f));g.fillEllipse(x-radius-7.0f,y-radius-7.0f,(radius+7.0f)*2,(radius+7.0f)*2);}
            g.setColour(categoryColour(sample.category).withAlpha(selected?1.0f:0.78f));g.fillEllipse(x-radius,y-radius,radius*2,radius*2);
            if(selected){g.setColour(Palette::Text);g.drawEllipse(x-radius-2,y-radius-2,(radius+2)*2,(radius+2)*2,1.4f);}
        }
        if(samples.isEmpty()){g.setColour(Palette::Muted);g.drawFittedText(magdrum::ui::localisedText("Scan installed libraries or add a sample folder to populate the library."),plot.toNearestInt().reduced(22),juce::Justification::centred,3);}
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
        auto area=getLocalBounds().toFloat().reduced(2.0f);g.setColour(Palette::Panel);g.fillRoundedRectangle(area,7.0f);g.setColour(Palette::Border.withAlpha(0.7f));g.drawRoundedRectangle(area,7.0f,1.0f);
        g.setColour(Palette::Muted);g.setFont(11.0f);g.drawText(magdrum::ui::localisedText("SAMPLE WAVEFORM"),getLocalBounds().reduced(12,7).removeFromTop(18),juce::Justification::centredLeft);
        auto wave=getLocalBounds().reduced(12,28).toFloat();g.setColour(Palette::Accent.withAlpha(0.18f));g.drawHorizontalLine(static_cast<int>(wave.getCentreY()),wave.getX(),wave.getRight());
        if(thumbnail.getNumChannels()>0){g.setColour(Palette::Accent);thumbnail.drawChannels(g,wave.toNearestInt(),0.0,thumbnail.getTotalLength(),1.0f);}
        else{g.setColour(Palette::Muted);g.drawFittedText(magdrum::ui::localisedText(currentFile.existsAsFile()?"Reading waveform…":"No sample loaded"),wave.toNearestInt(),juce::Justification::centred,2);}
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
        :juce::DocumentWindow(magdrum::ui::localisedText("Compare drum samples"),Palette::Panel,juce::DocumentWindow::closeButton)
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

class PluginMidiPatternDiscoveryJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(std::vector<juce::File>)>;
    PluginMidiPatternDiscoveryJob(std::vector<juce::File> roots, Completion completion)
        : ThreadPoolJob("MagDrum VST3 MIDI pattern discovery"), searchRoots(std::move(roots)), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        auto matches=magdrum::library::findDrumMidiPatternFiles(searchRoots,200,500000,[this]{return shouldExit();});
        if (onComplete)
            onComplete(std::move(matches));
        return jobHasFinished;
    }

private:
    std::vector<juce::File> searchRoots;
    Completion onComplete;
};

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p)
    : AudioProcessorEditor(p), processor(p), indexedList("Samples",this), sampleMap(std::make_unique<MagDrumSampleMap>()), waveform(std::make_unique<MagDrumWaveform>())
{
    magdrum::ui::initialiseMagDrumLocalization();setLookAndFeel(&theme);setSize(1120,760);setResizable(true,true);setResizeLimits(1000,620,1800,1200);
    languageButton.setTooltip(magdrum::ui::localisedText("Menu"));languageButton.onClick=[this]{magdrum::ui::showLanguageMenu(languageButton,[this]{applyLanguage();});};addAndMakeVisible(languageButton);
    libraryTab.setButtonText(magdrum::ui::localisedText("SAMPLE LIBRARY"));machineTab.setButtonText(magdrum::ui::localisedText("DRUM MACHINE"));sequencerTab.setButtonText(magdrum::ui::localisedText("SEQUENCER"));mapsTab.setButtonText(magdrum::ui::localisedText("MIDI MAPS"));
    for(auto* tab:{&libraryTab,&machineTab,&sequencerTab,&mapsTab}){tab->addListener(this);addAndMakeVisible(*tab);}
    sampleSearch.setTextToShowWhenEmpty(magdrum::ui::localisedText("Search indexed samples"),Palette::Muted);sampleSearch.onReturnKey=[this]{refreshSamples();};addAndMakeVisible(sampleSearch);
    categoryFilter.addItem(magdrum::ui::localisedText("All sounds"),1);categoryFilter.addItem(magdrum::ui::localisedText("Kick"),2);categoryFilter.addItem(magdrum::ui::localisedText("Snare"),3);categoryFilter.addItem(magdrum::ui::localisedText("Closed hi-hat"),4);categoryFilter.addItem(magdrum::ui::localisedText("Open hi-hat"),5);categoryFilter.addItem(magdrum::ui::localisedText("Pedal hi-hat"),6);categoryFilter.addItem(magdrum::ui::localisedText("Tom"),7);categoryFilter.addItem(magdrum::ui::localisedText("Ride"),8);categoryFilter.addItem(magdrum::ui::localisedText("Crash"),9);categoryFilter.addItem(magdrum::ui::localisedText("Clap"),10);categoryFilter.addItem(magdrum::ui::localisedText("Percussion"),11);categoryFilter.addItem(magdrum::ui::localisedText("Unknown"),12);categoryFilter.setSelectedId(1);addAndMakeVisible(categoryFilter);
    const char* features[]{"All sound features","Very short","Short","Medium","Long","Dark","Mid brightness","Bright"};for(int i=0;i<8;++i)featureFilter.addItem(magdrum::ui::localisedText(features[i]),i+1);featureFilter.setSelectedId(1);addAndMakeVisible(featureFilter);
    const char* sorts[]{"Name A–Z","Duration shortest","Duration longest","Brightest first","Darkest first"};for(int i=0;i<5;++i)sortFilter.addItem(magdrum::ui::localisedText(sorts[i]),i+1);sortFilter.setSelectedId(1);addAndMakeVisible(sortFilter);
    confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review"),2);confidenceFilter.setSelectedId(1);addAndMakeVisible(confidenceFilter);
    sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.setSelectedId(1);addAndMakeVisible(sourceFilter);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);kitFilter.setSelectedId(1);addAndMakeVisible(kitFilter);
    auto refreshOnChange=[this]{refreshSamples();};categoryFilter.onChange=refreshOnChange;featureFilter.onChange=refreshOnChange;sortFilter.onChange=refreshOnChange;confidenceFilter.onChange=refreshOnChange;sourceFilter.onChange=refreshOnChange;kitFilter.onChange=refreshOnChange;
    scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Scan installed libraries"));instrumentScannerButton.setButtonText(magdrum::ui::localisedText("Plugin instruments"));openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));openSourceKitButton.setTooltip(magdrum::ui::localisedText("Open the selected sample's source instrument and recall its saved kit in a separate MagDrum window."));openSourceKitButton.setEnabled(false);searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));
    scanButton.addListener(this);scanDefaultsButton.addListener(this);instrumentScannerButton.addListener(this);openSourceKitButton.addListener(this);searchButton.addListener(this);previewButton.addListener(this);assignButton.addListener(this);libraryToolsButton.addListener(this);moreSamplesButton.addListener(this);addAndMakeVisible(scanButton);addAndMakeVisible(scanDefaultsButton);addAndMakeVisible(instrumentScannerButton);addAndMakeVisible(openSourceKitButton);addAndMakeVisible(searchButton);addAndMakeVisible(previewButton);addAndMakeVisible(assignButton);addAndMakeVisible(libraryToolsButton);addAndMakeVisible(moreSamplesButton);
    instrumentScannerButton.setTooltip("Open MagDrum's isolated instrument browser to load VST instruments, choose kits and capture MIDI notes.");
    scanDefaultsButton.setTooltip("Scan audio files in common Native Instruments, Spectrasonics and Steinberg library locations.");
    indexedList.setModel(this);indexedList.setRowHeight(43);indexedList.setMultipleSelectionEnabled(true);addAndMakeVisible(indexedList);
    sampleMap->onSelect=[this](int index){if(juce::isPositiveAndBelow(index,samples.size()))indexedList.selectRow(index);};addAndMakeVisible(*sampleMap);
    for(size_t i=0;i<pads.size();++i){auto& pad=pads[i];pad.setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));pad.setColour(juce::TextButton::buttonColourId,Palette::Surface);pad.setColour(juce::TextButton::buttonOnColourId,categoryColour(padNames[i]));pad.setClickingTogglesState(false);pad.addListener(this);addAndMakeVisible(pad);}
    selectedPadTitle.setFont(juce::Font(juce::FontOptions(22.0f,juce::Font::bold)));selectedPadTitle.setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(selectedPadTitle);
    layerSummary.setColour(juce::Label::textColourId,Palette::Text);layerSummary.setJustificationType(juce::Justification::topLeft);layerSummary.setMinimumHorizontalScale(0.8f);addAndMakeVisible(layerSummary);
    sampleDetails.setColour(juce::Label::textColourId,Palette::Muted);sampleDetails.setJustificationType(juce::Justification::topLeft);sampleDetails.setMinimumHorizontalScale(0.8f);addAndMakeVisible(sampleDetails);
    addAndMakeVisible(*waveform);
    velocityRange.setText("1-127");velocityRange.setInputRestrictions(7,"0123456789-");velocityRange.setTooltip("Inclusive velocity range for this sample layer (1–127).");addAndMakeVisible(velocityRange);
    clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load WAV"));for(auto* button:{&clearPadButton,&loadButton}){button->addListener(this);addAndMakeVisible(*button);}
    for(auto& row:stepVelocities)row.fill(100);
    for(size_t row=0;row<laneLabels.size();++row)
    {
        laneLabels[row].setText(magdrum::ui::localisedText(laneDisplayNames[row]),juce::dontSendNotification);laneLabels[row].setColour(juce::Label::textColourId,categoryColour(laneNames[row]));laneLabels[row].setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));addAndMakeVisible(laneLabels[row]);
        for(size_t step=0;step<512;++step)
        {
            auto& cell=steps[row][step];cell.setClickingTogglesState(true);cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(static_cast<float>(stepVelocities[row][step])/127.0f));cell.setTooltip(juce::String(laneNames[row])+" · MIDI "+juce::String(mappedLaneNotes[row])+" · bar "+juce::String(static_cast<int>(step/static_cast<size_t>(stepsPerBar())+1))+" · step "+juce::String(static_cast<int>(step%static_cast<size_t>(stepsPerBar())+1))+" · velocity "+juce::String(stepVelocities[row][step])+" (right-click to edit)");
            cell.onRightClick=[this,row,step]
            {
                juce::PopupMenu menu;for(int velocity:{50,70,85,100,115,127})menu.addItem(velocity,magdrum::ui::localisedText("Velocity")+" "+juce::String(velocity));juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&steps[row][step]),[safe,row,step](int value)
                {
                    if(safe==nullptr||value<1)return;safe->stepVelocities[row][step]=value;auto& target=safe->steps[row][step];target.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(static_cast<float>(value)/127.0f));safe->refreshStepTooltips();safe->syncSequenceEventForCell(static_cast<int>(row),static_cast<int>(step));safe->savePluginEditorState();
                });
            };
            cell.addListener(this);addAndMakeVisible(cell);
        }
    }
    barSelect.setSelectedId(1);
    barSelect.onChange=[this]{updateSequencerGrid();};
    barSelect.setTooltip("Select a bar in the current pattern.");
    addAndMakeVisible(barSelect);
    for(int bars=1;bars<=magdrum::sequencer::maximumBars;++bars)
        lengthSelect.addItem(juce::String(bars)+" "+magdrum::ui::localisedText(bars==1?"bar":"bars"),bars);
    lengthSelect.setSelectedId(magdrum::sequencer::maximumBars,juce::dontSendNotification);
    lengthSelect.setTooltip("Pattern length in bars (1–16).");
    lengthSelect.onChange=[this]{
        const int requested=lengthSelect.getSelectedId();
        const int newStepCount=magdrum::sequencer::stepsForBars(timeSignatureNumerator(),timeSignatureDenominator(),requested);
        for(const auto& row:steps)for(size_t index=static_cast<size_t>(newStepCount);index<row.size();++index)
            if(row[index].getToggleState()){
                lengthSelect.setSelectedId(previousPatternBars,juce::dontSendNotification);
                status.setText(magdrum::ui::localisedText("That pattern length would hide existing hits. Move or clear those hits first."),juce::dontSendNotification);
                return;
            }
        previousPatternBars=requested;
        updateSequencerGrid();
        savePluginEditorState();
    };
    addAndMakeVisible(lengthSelect);
    for(const int denominator : {2,4,8,16})
        for(int numerator=1;numerator<=32*denominator/16;++numerator)
            meterSelect.addItem(juce::String(numerator)+"/"+juce::String(denominator),numerator*100+denominator);
    meterSelect.setSelectedId(404,juce::dontSendNotification);
    meterSelect.setTooltip("Time signature. The sequencer keeps a sixteenth-note grid and supports up to 32 steps per bar.");
    meterSelect.onChange=[this]{
        const int maximumSteps=magdrum::sequencer::stepsForBars(timeSignatureNumerator(),timeSignatureDenominator(),previousPatternBars);
        for(const auto& row:steps)for(size_t index=static_cast<size_t>(maximumSteps);index<row.size();++index)
            if(row[index].getToggleState()){
                meterSelect.setSelectedId(previousMeterId,juce::dontSendNotification);
                status.setText(magdrum::ui::localisedText("That meter would hide existing hits in the selected pattern length. Move or clear those hits first."),juce::dontSendNotification);
                return;
            }
        previousMeterId=meterSelect.getSelectedId();
        updateSequencerGrid();
        savePluginEditorState();
    };
    addAndMakeVisible(meterSelect);
    playSequenceButton.setButtonText(magdrum::ui::localisedText("Play"));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));playSequenceButton.addListener(this);clearSequenceButton.addListener(this);exportSequenceButton.addListener(this);addAndMakeVisible(playSequenceButton);addAndMakeVisible(clearSequenceButton);addAndMakeVisible(exportSequenceButton);
    importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));browseMidiButton.setButtonText(magdrum::ui::localisedText("Find drum MIDI"));editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));importMidiButton.addListener(this);browseMidiButton.addListener(this);editMidiEventsButton.addListener(this);reviewMidiButton.addListener(this);addAndMakeVisible(importMidiButton);addAndMakeVisible(browseMidiButton);addAndMakeVisible(editMidiEventsButton);addAndMakeVisible(reviewMidiButton);reviewMidiButton.setEnabled(false);browseMidiButton.setTooltip("Search the detected default libraries for drum-related MIDI files.");editMidiEventsButton.setTooltip("Edit individual hits, including articulation, off-grid beat position and duration.");reviewMidiButton.setTooltip("Compare the imported MIDI pattern with your current sequencer edits.");
    tempoEditor.setText("120");tempoEditor.setInputRestrictions(3,"0123456789");tempoEditor.setTooltip("Pattern preview and MIDI export tempo.");addAndMakeVisible(tempoEditor);
    metadataType.addItem(magdrum::ui::localisedText("Tempo event"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(1,juce::dontSendNotification);metadataType.setTooltip("Choose the MIDI metadata event to edit.");
    metadataBeat.setText("0",juce::dontSendNotification);metadataBeat.setInputRestrictions(12,"0123456789.");metadataBeat.setTooltip("Event position in quarter-note beats from the start of the MIDI pattern.");
    metadataValue.setText("120",juce::dontSendNotification);metadataValue.setInputRestrictions(8,"0123456789/");metadataValue.setTooltip("Tempo in BPM, or a time signature such as 4/4.");
    metadataType.onChange=[this]{metadataValue.setText(metadataType.getSelectedId()==1?"120":"4/4",juce::dontSendNotification);};
    for(auto* control:{static_cast<juce::Component*>(&metadataType),static_cast<juce::Component*>(&metadataBeat),static_cast<juce::Component*>(&metadataValue),static_cast<juce::Component*>(&metadataSummary)})addAndMakeVisible(*control);
    for(auto* button:{&setMetadataButton,&removeMetadataButton}){button->addListener(this);addAndMakeVisible(*button);}
    metadataSummary.setColour(juce::Label::textColourId,Palette::Muted);metadataSummary.setMinimumHorizontalScale(0.7f);metadataSummary.setTooltip("Tempo and time-signature events stored with this sequencer pattern.");refreshSequenceMetadata();
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
    if(!databaseReady)status.setText(magdrum::ui::localisedText("Sample index unavailable. Load WAVs directly or scan a folder in the MagDrum app."),juce::dontSendNotification);
    else{refreshSourceKitFilters();refreshSamples();for(const auto& name:database.getMappingProfileNames())if(name!="General MIDI")profileSelect.addItem(name,profileSelect.getNumItems()+1);if(const auto stored=database.loadMappingProfile("General MIDI"))mappingProfile=*stored;}
    restorePluginEditorState();if(!sequenceEventsInitialised)syncSequenceEventsFromGrid();updateSequencerGrid();updateDrumNotes();setNote(mappedPadNotes[0]);refreshMappingSummary();setPage(0);applyLanguage();
}

MagDrumAudioProcessorEditor::~MagDrumAudioProcessorEditor(){midiEventWindow.reset();comparisonWindow.reset();stopTimer();if(scanCancellation)scanCancellation->store(true);scanWorkers.removeAllJobs(true,-1);setLookAndFeel(nullptr);}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    magdrum::ui::paintTexturedBackground(g,getLocalBounds(),0.42f);g.setColour(Palette::Accent);g.setFont(juce::Font(juce::FontOptions(30.0f,juce::Font::bold)));g.drawText("MagDrum",24,14,260,40,juce::Justification::centredLeft);
    g.setColour(Palette::Muted);g.setFont(13.0f);g.drawText(magdrum::ui::localisedText("SAMPLE LIBRARY")+"  /  "+magdrum::ui::localisedText("DRUM MACHINE")+"  /  "+magdrum::ui::localisedText("SEQUENCER")+"  /  "+magdrum::ui::localisedText("MIDI MAPS"),26,51,850,22,juce::Justification::centredLeft);
    g.setColour(Palette::Border);g.drawHorizontalLine(83,24.0f,static_cast<float>(getWidth()-24));
}

void MagDrumAudioProcessorEditor::resized()
{
    auto bounds=getLocalBounds().reduced(24);bounds.removeFromTop(67);auto tabs=bounds.removeFromTop(38);
    languageButton.setBounds(getWidth()-82, 91, 58, 34);
    libraryTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);machineTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);sequencerTab.setBounds(tabs.removeFromLeft(160));tabs.removeFromLeft(6);mapsTab.setBounds(tabs.removeFromLeft(160));bounds.removeFromTop(12);
    auto footer=bounds.removeFromBottom(26);status.setBounds(footer);bounds.removeFromBottom(8);
    libraryTab.setToggleState(page==0,juce::dontSendNotification);machineTab.setToggleState(page==1,juce::dontSendNotification);sequencerTab.setToggleState(page==2,juce::dontSendNotification);mapsTab.setToggleState(page==3,juce::dontSendNotification);
    sampleSearch.setVisible(page==0);categoryFilter.setVisible(page==0);featureFilter.setVisible(page==0);sortFilter.setVisible(page==0);confidenceFilter.setVisible(page==0);sourceFilter.setVisible(page==0);kitFilter.setVisible(page==0);scanButton.setVisible(page==0);scanDefaultsButton.setVisible(page==0);instrumentScannerButton.setVisible(page==0);openSourceKitButton.setVisible(page==0);searchButton.setVisible(page==0);previewButton.setVisible(page==0);assignButton.setVisible(page==0);libraryToolsButton.setVisible(page==0);moreSamplesButton.setVisible(page==0);indexedList.setVisible(page==0);sampleMap->setVisible(page==0);
    for(auto& pad:pads)pad.setVisible(page==1);selectedPadTitle.setVisible(page==1);layerSummary.setVisible(page==1);sampleDetails.setVisible(page==1);waveform->setVisible(page==1);velocityRange.setVisible(page==1);clearPadButton.setVisible(page==1);loadButton.setVisible(page==1);
    for(auto& label:laneLabels)label.setVisible(page==2);for(auto& row:steps)for(size_t index=0;index<row.size();++index)row[index].setVisible(page==2&&static_cast<int>(index/static_cast<size_t>(stepsPerBar())+1)==barSelect.getSelectedId()&&index<static_cast<size_t>(patternStepCount()));playSequenceButton.setVisible(page==2);clearSequenceButton.setVisible(page==2);exportSequenceButton.setVisible(page==2);importMidiButton.setVisible(page==2);browseMidiButton.setVisible(page==2);editMidiEventsButton.setVisible(page==2);reviewMidiButton.setVisible(page==2);tempoEditor.setVisible(page==2);barSelect.setVisible(page==2);meterSelect.setVisible(page==2);lengthSelect.setVisible(page==2);metadataType.setVisible(page==2);metadataBeat.setVisible(page==2);metadataValue.setVisible(page==2);setMetadataButton.setVisible(page==2);removeMetadataButton.setVisible(page==2);metadataSummary.setVisible(page==2);
    profileSelect.setVisible(page==3);profileName.setVisible(page==3);mappingInstrument.setVisible(page==3);mappingArticulation.setVisible(page==3);mappingNote.setVisible(page==3);mappingSummary.setVisible(page==3);loadProfileButton.setVisible(page==3);saveProfileButton.setVisible(page==3);importCsvButton.setVisible(page==3);exportCsvButton.setVisible(page==3);setMappingButton.setVisible(page==3);removeMappingButton.setVisible(page==3);
    if(page==0){auto controls=bounds.removeFromTop(34);sampleSearch.setBounds(controls.removeFromLeft(225));controls.removeFromLeft(6);categoryFilter.setBounds(controls.removeFromLeft(135));controls.removeFromLeft(6);featureFilter.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);sortFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);confidenceFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);searchButton.setBounds(controls.removeFromLeft(78));controls.removeFromLeft(6);previewButton.setBounds(controls.removeFromLeft(78));bounds.removeFromTop(6);controls=bounds.removeFromTop(34);sourceFilter.setBounds(controls.removeFromLeft(180));controls.removeFromLeft(6);kitFilter.setBounds(controls.removeFromLeft(190));bounds.removeFromTop(6);controls=bounds.removeFromTop(34);scanButton.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);scanDefaultsButton.setBounds(controls.removeFromLeft(190));controls.removeFromLeft(8);instrumentScannerButton.setBounds(controls.removeFromLeft(140));controls.removeFromLeft(8);openSourceKitButton.setBounds(controls.removeFromLeft(140));controls.removeFromLeft(8);assignButton.setBounds(controls.removeFromLeft(118));controls.removeFromLeft(6);libraryToolsButton.setBounds(controls.removeFromLeft(115));controls.removeFromLeft(6);moreSamplesButton.setBounds(controls.removeFromLeft(110));moreSamplesButton.setEnabled(hasMoreSamples);bounds.removeFromTop(8);auto content=bounds;const int left=juce::jlimit(320,440,content.getWidth()*2/5);indexedList.setBounds(content.removeFromLeft(left));content.removeFromLeft(12);sampleMap->setBounds(content);}
    else if(page==1){auto grid=bounds.removeFromLeft(bounds.getWidth()*3/5);const int gap=10;const int padW=(grid.getWidth()-3*gap)/4,padH=(grid.getHeight()-3*gap)/4;for(size_t i=0;i<pads.size();++i){const int row=static_cast<int>(i/4),col=static_cast<int>(i%4);pads[i].setBounds(grid.getX()+col*(padW+gap),grid.getY()+row*(padH+gap),padW,padH);}bounds.removeFromLeft(18);selectedPadTitle.setBounds(bounds.removeFromTop(38));bounds.removeFromTop(6);sampleDetails.setBounds(bounds.removeFromTop(58));bounds.removeFromTop(8);waveform->setBounds(bounds.removeFromTop(154));bounds.removeFromTop(8);layerSummary.setBounds(bounds.removeFromTop(juce::jmin(180,bounds.getHeight()/2)));bounds.removeFromTop(12);auto row=bounds.removeFromTop(34);velocityRange.setBounds(row.removeFromLeft(100));row.removeFromLeft(8);clearPadButton.setBounds(row.removeFromLeft(108));row.removeFromLeft(8);loadButton.setBounds(row);bounds.removeFromTop(14);}
    else if(page==2){auto toolbar=bounds.removeFromTop(36);playSequenceButton.setBounds(toolbar.removeFromLeft(82));toolbar.removeFromLeft(6);clearSequenceButton.setBounds(toolbar.removeFromLeft(116));toolbar.removeFromLeft(6);importMidiButton.setBounds(toolbar.removeFromLeft(96));toolbar.removeFromLeft(6);browseMidiButton.setBounds(toolbar.removeFromLeft(142));toolbar.removeFromLeft(6);exportSequenceButton.setBounds(toolbar.removeFromLeft(110));toolbar.removeFromLeft(6);editMidiEventsButton.setBounds(toolbar.removeFromLeft(84));toolbar.removeFromLeft(6);reviewMidiButton.setBounds(toolbar.removeFromLeft(120));toolbar.removeFromLeft(6);tempoEditor.setBounds(toolbar.removeFromLeft(70));toolbar.removeFromLeft(8);meterSelect.setBounds(toolbar.removeFromLeft(74));toolbar.removeFromLeft(6);lengthSelect.setBounds(toolbar.removeFromLeft(78));toolbar.removeFromLeft(6);barSelect.setBounds(toolbar.removeFromLeft(82));auto metaRow=bounds.removeFromTop(32);metadataType.setBounds(metaRow.removeFromLeft(132));metaRow.removeFromLeft(6);metadataBeat.setBounds(metaRow.removeFromLeft(74));metaRow.removeFromLeft(6);metadataValue.setBounds(metaRow.removeFromLeft(74));metaRow.removeFromLeft(6);setMetadataButton.setBounds(metaRow.removeFromLeft(86));metaRow.removeFromLeft(6);removeMetadataButton.setBounds(metaRow.removeFromLeft(100));metaRow.removeFromLeft(8);metadataSummary.setBounds(metaRow);bounds.removeFromTop(10);const int labelW=122;const int activeSteps=stepsPerBar();const int cellW=juce::jmax(16,(bounds.getWidth()-labelW-(activeSteps-1)*4)/activeSteps);const size_t barOffset=static_cast<size_t>(juce::jlimit(0,magdrum::sequencer::maximumBars-1,barSelect.getSelectedId()-1))*static_cast<size_t>(activeSteps);for(int row=0;row<8;++row){auto line=bounds.removeFromTop(juce::jmax(36,bounds.getHeight()/8));laneLabels[static_cast<size_t>(row)].setBounds(line.removeFromLeft(labelW));line.removeFromLeft(6);for(int step=0;step<activeSteps;++step){steps[static_cast<size_t>(row)][barOffset+static_cast<size_t>(step)].setBounds(line.removeFromLeft(cellW));line.removeFromLeft(4);}}}
    else{auto top=bounds.removeFromTop(36);profileSelect.setBounds(top.removeFromLeft(210));top.removeFromLeft(8);profileName.setBounds(top.removeFromLeft(190));top.removeFromLeft(8);loadProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);saveProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);importCsvButton.setBounds(top.removeFromLeft(95));top.removeFromLeft(6);exportCsvButton.setBounds(top.removeFromLeft(95));bounds.removeFromTop(12);auto edit=bounds.removeFromTop(36);mappingInstrument.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingArticulation.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingNote.setBounds(edit.removeFromLeft(110));edit.removeFromLeft(8);setMappingButton.setBounds(edit.removeFromLeft(120));edit.removeFromLeft(8);removeMappingButton.setBounds(edit.removeFromLeft(130));bounds.removeFromTop(14);mappingSummary.setBounds(bounds);}
}

void MagDrumAudioProcessorEditor::applyLanguage()
{
    libraryTab.setButtonText(magdrum::ui::localisedText("SAMPLE LIBRARY"));machineTab.setButtonText(magdrum::ui::localisedText("DRUM MACHINE"));sequencerTab.setButtonText(magdrum::ui::localisedText("SEQUENCER"));mapsTab.setButtonText(magdrum::ui::localisedText("MIDI MAPS"));languageButton.setTooltip(magdrum::ui::localisedText("Menu"));
    sampleSearch.setTextToShowWhenEmpty(magdrum::ui::localisedText("Search indexed samples"),Palette::Muted);
    searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));
    libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));
    clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load WAV"));
    playSequenceButton.setButtonText(magdrum::ui::localisedText(sequencerPlaying ? "Stop" : "Play"));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));
    importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));
    setMetadataButton.setButtonText(magdrum::ui::localisedText("Set event"));removeMetadataButton.setButtonText(magdrum::ui::localisedText("Remove event"));
    loadProfileButton.setButtonText(magdrum::ui::localisedText("Load profile"));saveProfileButton.setButtonText(magdrum::ui::localisedText("Save profile"));importCsvButton.setButtonText(magdrum::ui::localisedText("Import CSV"));exportCsvButton.setButtonText(magdrum::ui::localisedText("Export CSV"));setMappingButton.setButtonText(magdrum::ui::localisedText("Set mapping"));removeMappingButton.setButtonText(magdrum::ui::localisedText("Remove mapping"));
    instrumentScannerButton.setTooltip(magdrum::ui::localisedText("Open MagDrum's isolated instrument browser to load VST instruments, choose kits and capture MIDI notes."));
    openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));
    openSourceKitButton.setTooltip(magdrum::ui::localisedText("Open the selected sample's source instrument and recall its saved kit in a separate MagDrum window."));
    scanDefaultsButton.setTooltip(magdrum::ui::localisedText("Scan audio files in common Native Instruments, Spectrasonics and Steinberg library locations."));
    velocityRange.setTooltip(magdrum::ui::localisedText("Inclusive velocity range for this sample layer (1–127)."));
    barSelect.setTooltip(magdrum::ui::localisedText("Select a bar in the current pattern."));
    lengthSelect.setTooltip(magdrum::ui::localisedText("Pattern length in bars (1–16)."));
    meterSelect.setTooltip(magdrum::ui::localisedText("Time signature. The sequencer keeps a sixteenth-note grid and supports up to 32 steps per bar."));
    browseMidiButton.setTooltip(magdrum::ui::localisedText("Search the detected default libraries for drum-related MIDI files."));
    editMidiEventsButton.setTooltip(magdrum::ui::localisedText("Edit individual hits, including articulation, off-grid beat position and duration."));
    reviewMidiButton.setTooltip(magdrum::ui::localisedText("Compare the imported MIDI pattern with your current sequencer edits."));
    tempoEditor.setTooltip(magdrum::ui::localisedText("Pattern preview and MIDI export tempo."));
    metadataType.setTooltip(magdrum::ui::localisedText("Choose the MIDI metadata event to edit."));
    metadataBeat.setTooltip(magdrum::ui::localisedText("Event position in quarter-note beats from the start of the MIDI pattern."));
    metadataValue.setTooltip(magdrum::ui::localisedText("Tempo in BPM, or a time signature such as 4/4."));
    metadataSummary.setTooltip(magdrum::ui::localisedText("Tempo and time-signature events stored with this sequencer pattern."));
    scanButton.setTooltip(magdrum::ui::localisedText("Add sample folder"));searchButton.setTooltip(magdrum::ui::localisedText("Search indexed samples"));previewButton.setTooltip(magdrum::ui::localisedText("Preview selected sample"));assignButton.setTooltip(magdrum::ui::localisedText("Assign selected sample to the selected pad"));
    libraryToolsButton.setTooltip(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setTooltip(magdrum::ui::localisedText("More samples"));clearPadButton.setTooltip(magdrum::ui::localisedText("Clear pad"));loadButton.setTooltip(magdrum::ui::localisedText("Load WAV"));
    profileName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Mapping profile name"),Palette::Muted);
    mappingNote.setTextToShowWhenEmpty(magdrum::ui::localisedText("MIDI note 0–127"),Palette::Muted);
    const int selectedCategory=categoryFilter.getSelectedId(),selectedFeature=featureFilter.getSelectedId(),selectedSort=sortFilter.getSelectedId(),selectedConfidence=confidenceFilter.getSelectedId(),selectedLength=lengthSelect.getSelectedId();
    const char* categories[]{"All sounds","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Percussion","Unknown"};categoryFilter.clear(juce::dontSendNotification);for(int i=0;i<12;++i)categoryFilter.addItem(magdrum::ui::localisedText(categories[i]),i+1);categoryFilter.setSelectedId(selectedCategory,juce::dontSendNotification);
    const char* features[]{"All sound features","Very short","Short","Medium","Long","Dark","Mid brightness","Bright"};featureFilter.clear(juce::dontSendNotification);for(int i=0;i<8;++i)featureFilter.addItem(magdrum::ui::localisedText(features[i]),i+1);featureFilter.setSelectedId(selectedFeature,juce::dontSendNotification);
    const char* sorts[]{"Name A–Z","Duration shortest","Duration longest","Brightest first","Darkest first"};sortFilter.clear(juce::dontSendNotification);for(int i=0;i<5;++i)sortFilter.addItem(magdrum::ui::localisedText(sorts[i]),i+1);sortFilter.setSelectedId(selectedSort,juce::dontSendNotification);
    confidenceFilter.clear(juce::dontSendNotification);confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review"),2);confidenceFilter.setSelectedId(selectedConfidence,juce::dontSendNotification);
    lengthSelect.clear(juce::dontSendNotification);for(int bars=1;bars<=magdrum::sequencer::maximumBars;++bars)lengthSelect.addItem(juce::String(bars)+" "+magdrum::ui::localisedText(bars==1?"bar":"bars"),bars);lengthSelect.setSelectedId(selectedLength,juce::dontSendNotification);
    scanButton.setButtonText(magdrum::ui::localisedText(scanningDefaultLibraries?"Searching MIDI…":scanning?"Cancel scan":"Add sample folder"));scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Scan installed libraries"));instrumentScannerButton.setButtonText(magdrum::ui::localisedText("Plugin instruments"));openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load WAV"));playSequenceButton.setButtonText(magdrum::ui::localisedText(sequencerPlaying?"Stop":"Play"));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));browseMidiButton.setButtonText(defaultMidiPatterns.empty()?magdrum::ui::localisedText("Find drum MIDI"):magdrum::ui::localisedText("Drum MIDI (")+juce::String(static_cast<int>(defaultMidiPatterns.size()))+")");editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));
    for(size_t i=0;i<pads.size();++i)pads[i].setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));
    for(size_t row=0;row<laneLabels.size();++row)laneLabels[row].setText(magdrum::ui::localisedText(laneDisplayNames[row]),juce::dontSendNotification);
    setMetadataButton.setButtonText(magdrum::ui::localisedText("Set event"));removeMetadataButton.setButtonText(magdrum::ui::localisedText("Remove event"));loadProfileButton.setButtonText(magdrum::ui::localisedText("Load profile"));saveProfileButton.setButtonText(magdrum::ui::localisedText("Save profile"));importCsvButton.setButtonText(magdrum::ui::localisedText("Import CSV"));exportCsvButton.setButtonText(magdrum::ui::localisedText("Export CSV"));setMappingButton.setButtonText(magdrum::ui::localisedText("Set mapping"));removeMappingButton.setButtonText(magdrum::ui::localisedText("Remove mapping"));
    const int selectedMetadata=metadataType.getSelectedId();metadataType.clear(juce::dontSendNotification);metadataType.addItem(magdrum::ui::localisedText("Tempo event"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(selectedMetadata,juce::dontSendNotification);
    const int selectedInstrument=mappingInstrument.getSelectedId(),selectedArticulation=mappingArticulation.getSelectedId();const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};mappingInstrument.clear(juce::dontSendNotification);for(int i=0;i<9;++i)mappingInstrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);mappingInstrument.setSelectedId(selectedInstrument,juce::dontSendNotification);const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};mappingArticulation.clear(juce::dontSendNotification);for(int i=0;i<13;++i)mappingArticulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);mappingArticulation.setSelectedId(selectedArticulation,juce::dontSendNotification);
    refreshSequenceMetadata();refreshMappingSummary();updateLayerSummary();setNote(selectedNote);
    refreshStepTooltips();refreshSourceKitFilters();refreshSamples();sampleMap->repaint();waveform->repaint();repaint();
}

int MagDrumAudioProcessorEditor::timeSignatureNumerator() const
{
    const int id=meterSelect.getSelectedId();
    return id>0?id/100:4;
}

int MagDrumAudioProcessorEditor::timeSignatureDenominator() const
{
    const int id=meterSelect.getSelectedId();
    return id>0?id%100:4;
}

int MagDrumAudioProcessorEditor::stepsPerBar() const
{
    const int result=magdrum::sequencer::stepsPerBar(timeSignatureNumerator(),timeSignatureDenominator());
    return result>0?result:16;
}

int MagDrumAudioProcessorEditor::patternStepCount() const
{
    const int result=magdrum::sequencer::stepsForBars(timeSignatureNumerator(),timeSignatureDenominator(),lengthSelect.getSelectedId());
    return result>0?result:stepsPerBar()*magdrum::sequencer::maximumBars;
}

int MagDrumAudioProcessorEditor::sequenceTempoAtBeat(double beat) const
{
    int bpm=tempoEditor.getText().getIntValue();
    if(bpm<20||bpm>300)bpm=120;
    double lastBeat=-1.0;
    for(const auto& meta:sequenceMetadata)
        if(meta.message.isTempoMetaEvent()&&std::isfinite(meta.positionBeats)&&meta.positionBeats<=beat+1.0e-9&&meta.positionBeats>=lastBeat){
            const double seconds=meta.message.getTempoSecondsPerQuarterNote();
            if(seconds>0.0&&std::isfinite(seconds)){bpm=juce::roundToInt(60.0/seconds);lastBeat=meta.positionBeats;}
        }
    return juce::jlimit(20,300,bpm);
}

void MagDrumAudioProcessorEditor::refreshSequenceMetadata()
{
    juce::String summary;
    for(const auto& meta:sequenceMetadata){
        if(meta.message.isTempoMetaEvent())summary+=(summary.isEmpty()?"":"   ·   ")+juce::String(juce::roundToInt(60.0/meta.message.getTempoSecondsPerQuarterNote()))+" BPM @ "+juce::String(meta.positionBeats,2);
        else if(meta.message.isTimeSignatureMetaEvent()){
            int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);
            summary+=(summary.isEmpty()?"":"   ·   ")+juce::String(numerator)+"/"+juce::String(denominator)+" @ "+juce::String(meta.positionBeats,2);
        }
    }
    if(summary.isEmpty())summary=magdrum::ui::localisedText("No MIDI metadata events; export uses the controls above.");
    metadataSummary.setText(summary,juce::dontSendNotification);
    metadataSummary.setTooltip(summary);
}

bool MagDrumAudioProcessorEditor::editSequenceMetadata(bool remove)
{
    const double beat=metadataBeat.getText().getDoubleValue();
    if(!std::isfinite(beat)||beat<0.0){status.setText(magdrum::ui::localisedText("Enter a non-negative beat position."),juce::dontSendNotification);return false;}
    MidiPattern edited;edited.tempoAndSignature=sequenceMetadata;
    const bool tempoEvent=metadataType.getSelectedId()==1;
    if(remove){
        const bool changed=tempoEvent?removePatternTempo(edited,beat):removePatternTimeSignature(edited,beat);
        if(!changed){status.setText(magdrum::ui::localisedText("No matching MIDI metadata exists at that beat."),juce::dontSendNotification);return false;}
    }else if(tempoEvent){
        const int bpm=metadataValue.getText().getIntValue();
        if(!setPatternTempo(edited,bpm,beat)){status.setText(magdrum::ui::localisedText("Use a tempo from 20 to 300 BPM and a non-negative beat."),juce::dontSendNotification);return false;}
        if(std::abs(beat)<1.0e-9)tempoEditor.setText(juce::String(bpm),juce::dontSendNotification);
    }else{
        const auto value=metadataValue.getText().trim();const int slash=value.indexOfChar('/');
        if(slash<=0){status.setText(magdrum::ui::localisedText("Enter a time signature such as 4/4."),juce::dontSendNotification);return false;}
        const int numerator=value.substring(0,slash).getIntValue(),denominator=value.substring(slash+1).getIntValue();
        if(!setPatternTimeSignature(edited,numerator,denominator,beat)){status.setText(magdrum::ui::localisedText("Use a numerator 1–32 and denominator 1, 2, 4, 8 or 16."),juce::dontSendNotification);return false;}
        if(std::abs(beat)<1.0e-9){
            const int id=numerator*100+denominator;
            if(meterSelect.indexOfItemId(id)<0){status.setText(magdrum::ui::localisedText("That opening time signature is not available in the sequencer grid."),juce::dontSendNotification);return false;}
            const int newSteps=magdrum::sequencer::stepsForBars(numerator,denominator,lengthSelect.getSelectedId());
            for(const auto& row:steps)for(size_t index=static_cast<size_t>(newSteps);index<row.size();++index)
                if(row[index].getToggleState()){status.setText(magdrum::ui::localisedText("That opening meter would hide existing sequencer hits."),juce::dontSendNotification);return false;}
            meterSelect.setSelectedId(id,juce::dontSendNotification);previousMeterId=id;updateSequencerGrid();
        }
    }
    sequenceMetadata=std::move(edited.tempoAndSignature);
    refreshSequenceMetadata();
    savePluginEditorState();
    if(remove)status.setText(magdrum::ui::localisedText("MIDI metadata removed at beat ")+juce::String(beat,3)+".",juce::dontSendNotification);
    else status.setText(magdrum::ui::localisedText(tempoEvent?"Tempo event saved at beat ":"Time-signature event saved at beat ")+juce::String(beat,3)+".",juce::dontSendNotification);
    return true;
}

MidiPattern MagDrumAudioProcessorEditor::currentSequencePattern() const
{
    MidiPattern pattern;
    pattern.lengthBeats=patternStepCount()*0.25;
    pattern.tempoAndSignature=sequenceMetadata;
    if(std::none_of(pattern.tempoAndSignature.begin(),pattern.tempoAndSignature.end(),[](const auto& meta){return meta.message.isTempoMetaEvent()&&std::abs(meta.positionBeats)<1.0e-9;}))
        setPatternTempo(pattern,tempoEditor.getText().getIntValue());
    if(std::none_of(pattern.tempoAndSignature.begin(),pattern.tempoAndSignature.end(),[](const auto& meta){return meta.message.isTimeSignatureMetaEvent()&&std::abs(meta.positionBeats)<1.0e-9;}))
        setPatternTimeSignature(pattern,timeSignatureNumerator(),timeSignatureDenominator());
    pattern.events=sequenceEvents;
    return pattern;
}

void MagDrumAudioProcessorEditor::syncSequenceEventsFromGrid()
{
    sequenceEvents.clear();
    for(int row=0;row<8;++row)
        for(int step=0;step<patternStepCount();++step)
            if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState())
                sequenceEvents.push_back({laneInstruments[static_cast<size_t>(row)],laneArticulations[static_cast<size_t>(row)],static_cast<std::uint8_t>(stepVelocities[static_cast<size_t>(row)][static_cast<size_t>(step)]),step*0.25,0.12});
    sequenceEventsInitialised=true;
}

void MagDrumAudioProcessorEditor::syncSequenceEventForCell(int lane,int step)
{
    if(!juce::isPositiveAndBelow(lane,8)||!juce::isPositiveAndBelow(step,patternStepCount()))return;
    const auto mapsToCell=[lane,step](const DrumEvent& event)
    {
        int eventLane=-1;
        switch(event.instrument){case DrumInstrument::Kick:eventLane=0;break;case DrumInstrument::Snare:case DrumInstrument::Rimshot:eventLane=1;break;case DrumInstrument::HiHat:eventLane=event.articulation==DrumArticulation::Open?3:2;break;case DrumInstrument::Tom:eventLane=4;break;case DrumInstrument::Ride:eventLane=5;break;case DrumInstrument::Crash:eventLane=6;break;case DrumInstrument::Clap:eventLane=7;break;default:break;}
        return eventLane==lane&&juce::roundToInt(event.positionBeats*4.0)==step;
    };
    sequenceEvents.erase(std::remove_if(sequenceEvents.begin(),sequenceEvents.end(),mapsToCell),sequenceEvents.end());
    if(steps[static_cast<size_t>(lane)][static_cast<size_t>(step)].getToggleState())
        sequenceEvents.push_back({laneInstruments[static_cast<size_t>(lane)],laneArticulations[static_cast<size_t>(lane)],static_cast<std::uint8_t>(stepVelocities[static_cast<size_t>(lane)][static_cast<size_t>(step)]),step*0.25,0.12});
    std::stable_sort(sequenceEvents.begin(),sequenceEvents.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});
    sequenceEventsInitialised=true;
}

void MagDrumAudioProcessorEditor::syncSequencerGridFromEvents()
{
    for(size_t row=0;row<steps.size();++row)
        for(size_t step=0;step<steps[row].size();++step)
        {
            stepVelocities[row][step]=100;steps[row][step].setToggleState(false,juce::dontSendNotification);
            steps[row][step].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(100.0f/127.0f));
        }
    for(const auto& event:sequenceEvents)
    {
        int lane=-1;
        switch(event.instrument){case DrumInstrument::Kick:lane=0;break;case DrumInstrument::Snare:case DrumInstrument::Rimshot:lane=1;break;case DrumInstrument::HiHat:lane=event.articulation==DrumArticulation::Open?3:2;break;case DrumInstrument::Tom:lane=4;break;case DrumInstrument::Ride:lane=5;break;case DrumInstrument::Crash:lane=6;break;case DrumInstrument::Clap:lane=7;break;default:break;}
        const int step=juce::roundToInt(event.positionBeats*4.0);
        if(!juce::isPositiveAndBelow(lane,8)||!juce::isPositiveAndBelow(step,patternStepCount()))continue;
        const auto row=static_cast<size_t>(lane),column=static_cast<size_t>(step);stepVelocities[row][column]=event.velocity;steps[row][column].setToggleState(true,juce::dontSendNotification);steps[row][column].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(static_cast<float>(event.velocity)/127.0f));
    }
    resized();repaint();
}

void MagDrumAudioProcessorEditor::openMidiEventEditor()
{
    if(midiEventWindow!=nullptr){midiEventWindow->toFront(true);return;}
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    midiEventEditingPattern=currentSequencePattern();
    auto* component=new PluginMidiEventEditor(midiEventEditingPattern,[safe](){if(safe==nullptr)return;safe->sequenceEvents=safe->midiEventEditingPattern.events;safe->sequenceEventsInitialised=true;safe->syncSequencerGridFromEvents();safe->savePluginEditorState();});
    for(auto& row:steps)for(auto& cell:row)cell.setEnabled(false);
    clearSequenceButton.setEnabled(false);importMidiButton.setEnabled(false);browseMidiButton.setEnabled(false);
    auto* window=new PluginMidiEventWindow(component,[safe](){if(safe==nullptr)return;for(auto& row:safe->steps)for(auto& cell:row)cell.setEnabled(true);safe->clearSequenceButton.setEnabled(true);safe->importMidiButton.setEnabled(true);safe->browseMidiButton.setEnabled(true);safe->midiEventWindow.reset();});
    midiEventWindow.reset(window);
}

void MagDrumAudioProcessorEditor::reviewMidiChanges()
{
    if(!hasImportedMidiBaseline){status.setText(magdrum::ui::localisedText("Import a MIDI pattern before reviewing changes."),juce::dontSendNotification);return;}
    const auto diff=compareMidiPatterns(importedMidiBaseline,currentSequencePattern());
    juce::String details="Compared with the imported pattern.\n\n";
    details+="Removed hits: "+juce::String(static_cast<int>(diff.removedEvents.size()))+"\nAdded hits: "+juce::String(static_cast<int>(diff.addedEvents.size()))
        +"\nRemoved metadata events: "+juce::String(static_cast<int>(diff.removedMetadata.size()))+"\nAdded metadata events: "+juce::String(static_cast<int>(diff.addedMetadata.size()))
        +"\nPattern length changed: "+juce::String(diff.lengthChanged?"yes":"no");
    const auto describeEvent=[](const DrumEvent& event)
    {
        static constexpr const char* names[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        const auto index=static_cast<size_t>(event.instrument);
        const auto name=index<9?names[index]:"Drum";
        return juce::String(name)+" · beat "+juce::String(event.positionBeats,2)+" · velocity "+juce::String(event.velocity);
    };
    for(const auto& event:diff.removedEvents)details+="\n− "+describeEvent(event);
    for(const auto& event:diff.addedEvents)details+="\n+ "+describeEvent(event);
    for(const auto& meta:diff.removedMetadata)
        details+="\n− "+juce::String(meta.message.isTempoMetaEvent()?"Tempo":"Time signature")+" metadata @ beat "+juce::String(meta.positionBeats,2);
    for(const auto& meta:diff.addedMetadata)
        details+="\n+ "+juce::String(meta.message.isTempoMetaEvent()?"Tempo":"Time signature")+" metadata @ beat "+juce::String(meta.positionBeats,2);
    if(diff.removedEvents.empty()&&diff.addedEvents.empty()&&diff.removedMetadata.empty()&&diff.addedMetadata.empty()&&!diff.lengthChanged)
        details+="\n\nNo changes since import.";
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"MIDI pattern changes",details);
}

void MagDrumAudioProcessorEditor::updateSequencerGrid()
{
    const int perBar=stepsPerBar();
    const int bars=juce::jlimit(1,magdrum::sequencer::maximumBars,lengthSelect.getSelectedId()>0?lengthSelect.getSelectedId():magdrum::sequencer::maximumBars);
    const int selectedBar=barSelect.getSelectedId()>0?barSelect.getSelectedId():1;
    barSelect.clear(juce::dontSendNotification);
    for(int bar=1;bar<=bars;++bar)barSelect.addItem(magdrum::ui::localisedText("Bar ")+juce::String(bar),bar);
    barSelect.setSelectedId(juce::jlimit(1,bars,selectedBar),juce::dontSendNotification);
    for(size_t lane=0;lane<steps.size();++lane)
        for(size_t step=0;step<steps[lane].size();++step)
            steps[lane][step].setTooltip(juce::String(laneNames[lane])+" · MIDI "+juce::String(mappedLaneNotes[lane])+" · bar "+juce::String(static_cast<int>(step/static_cast<size_t>(perBar)+1))+" · step "+juce::String(static_cast<int>(step%static_cast<size_t>(perBar)+1))+" · velocity "+juce::String(stepVelocities[lane][step])+" (right-click to edit)");
    refreshStepTooltips();
    resized();
    repaint();
}

void MagDrumAudioProcessorEditor::refreshStepTooltips()
{
    for(size_t lane=0;lane<steps.size();++lane)
        for(size_t step=0;step<steps[lane].size();++step)
        {
            const auto tooltip=magdrum::ui::localisedText(laneDisplayNames[lane])+" · MIDI "+juce::String(mappedLaneNotes[lane])+" · "+magdrum::ui::localisedText("bar")+" "+juce::String(static_cast<int>(step/static_cast<size_t>(stepsPerBar())+1))+" · "+magdrum::ui::localisedText("step")+" "+juce::String(static_cast<int>(step%static_cast<size_t>(stepsPerBar())+1))+" · "+magdrum::ui::localisedText("Velocity")+" "+juce::String(stepVelocities[lane][step])+" ("+magdrum::ui::localisedText("Right-click to edit")+")";
            steps[lane][step].setTooltip(tooltip);
        }
}

int MagDrumAudioProcessorEditor::getNumRows(){return samples.size();}
void MagDrumAudioProcessorEditor::paintListBoxItem(int row,juce::Graphics& g,int width,int height,bool selected)
{
    if(!juce::isPositiveAndBelow(row,samples.size()))return;
    const auto& sample=samples.getReference(row);
    g.fillAll(selected?Palette::Selection:Palette::Panel);
    g.setColour(categoryColour(sample.category));
    g.fillRoundedRectangle(6.0f,8.0f,4.0f,static_cast<float>(height-16),2.0f);
    g.setColour(Palette::Text);g.setFont(13.0f);
    g.drawText(sample.file.getFileName(),18,4,width-24,19,juce::Justification::centredLeft,true);
    juce::String info=magdrum::ui::localisedText(sample.category)+"  ·  "+juce::String(sample.durationSeconds,2)+" s  ·  "+juce::String(static_cast<int>(sample.brightnessHz))+" Hz";
    if(sample.pluginIdentifier.isNotEmpty())
    {
        info+="  ·  "+sample.pluginName+" / "+sample.kitName+"  ·  MIDI "+juce::String(sample.midiNote)+"  ·  "+magdrum::ui::localisedText("Velocity")+" "+juce::String(sample.velocity);
        if(sample.articulation.isNotEmpty())info+="  ·  "+sample.articulation;
        if(sample.takeIndex>1)info+="  ·  "+magdrum::ui::localisedText("Take")+" "+juce::String(sample.takeIndex);
    }
    g.setColour(Palette::Muted);g.setFont(10.5f);
    g.drawText(info,18,22,width-24,height-23,juce::Justification::centredLeft,true);
}
void MagDrumAudioProcessorEditor::selectedRowsChanged(int row)
{
    sampleMap->setSelected(row);
    if(!juce::isPositiveAndBelow(row,samples.size())){openSourceKitButton.setEnabled(false);return;}
    const auto& sample=samples.getReference(row);
    openSourceKitButton.setEnabled(sample.pluginIdentifier.isNotEmpty());
    juce::String details=sample.file.getFullPathName();
    if(sample.pluginIdentifier.isNotEmpty())
    {
        details=sample.pluginName+" / "+sample.kitName+"  ·  MIDI "+juce::String(sample.midiNote)+"  ·  "+magdrum::ui::localisedText("Velocity")+" "+juce::String(sample.velocity);
        if(sample.articulation.isNotEmpty())details+="  ·  "+sample.articulation;
        if(sample.takeIndex>1)details+="  ·  "+magdrum::ui::localisedText("Take")+" "+juce::String(sample.takeIndex);
        details+="\n"+sample.file.getFullPathName();
    }
    sampleDetails.setText(details,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::listBoxItemDoubleClicked(int row,const juce::MouseEvent&){if(juce::isPositiveAndBelow(row,samples.size())){if(processor.previewSample(samples.getReference(row).file))status.setText(magdrum::ui::localisedText("Previewing ")+samples.getReference(row).file.getFileName(),juce::dontSendNotification);}}

void MagDrumAudioProcessorEditor::refreshSamples()
{
    loadSamplePage(true);
}
void MagDrumAudioProcessorEditor::refreshSourceKitFilters()
{
    const auto priorSource=sourceFilter.getText(),priorKit=kitFilter.getText();sourceFilter.clear(juce::dontSendNotification);sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.addItem(magdrum::ui::localisedText("Sample folders"),2);int sourceId=3;
    juce::StringArray pluginNames;for(const auto& plugin:database.getPluginInstruments())pluginNames.addIfNotAlreadyThere(plugin.name);
    for(const auto& name:pluginNames){sourceFilter.addItem(name,sourceId);if(name==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    for(const auto& folder:database.getSampleSources("folder")){const auto label=magdrum::ui::localisedText("Folder: ")+folder.identifier;sourceFilter.addItem(label,sourceId);if(label==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    if(priorSource==magdrum::ui::localisedText("Sample folders"))sourceFilter.setSelectedId(2,juce::dontSendNotification);else if(priorSource==magdrum::ui::localisedText("All sources")||priorSource.isEmpty()||sourceFilter.getSelectedId()==0)sourceFilter.setSelectedId(1,juce::dontSendNotification);
    kitFilter.clear(juce::dontSendNotification);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);int kitId=2;for(const auto& name:database.getPluginKitNames()){kitFilter.addItem(name,kitId);if(name==priorKit)kitFilter.setSelectedId(kitId,juce::dontSendNotification);++kitId;}if(priorKit==magdrum::ui::localisedText("All kits")||priorKit.isEmpty()||kitFilter.getSelectedId()==0)kitFilter.setSelectedId(1,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::loadSamplePage(bool reset)
{
    if(!databaseReady)return;if(reset){samples.clear();sampleOffset=0;hasMoreSamples=false;openSourceKitButton.setEnabled(false);}
    SampleBrowseFilter options;options.query=sampleSearch.getText();if(categoryFilter.getSelectedId()>1){static constexpr const char* categories[]{"All sounds","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Percussion","Unknown"};const int categoryId=categoryFilter.getSelectedId()-1;if(juce::isPositiveAndBelow(categoryId,12))options.category=categories[categoryId];}options.feature=juce::jmax(1,featureFilter.getSelectedId());options.sort=juce::jmax(1,sortFilter.getSelectedId());options.needsReview=confidenceFilter.getSelectedId()==2;
    const auto source=sourceFilter.getText();if(sourceFilter.getSelectedId()==2)options.sourceType="folder";else if(source.startsWith(magdrum::ui::localisedText("Folder: "))){options.sourceType="folder";options.sourceIdentifier=source.fromFirstOccurrenceOf(magdrum::ui::localisedText("Folder: "),false,false);}else if(sourceFilter.getSelectedId()>2&&source.isNotEmpty()){options.sourceType="plugin";options.pluginName=source;}
    const auto kit=kitFilter.getText();if(kitFilter.getSelectedId()>1&&kit.isNotEmpty()){options.sourceType="plugin";if(options.pluginName.isEmpty())options.pluginName=kit.upToFirstOccurrenceOf(" / ",false,false);options.kitName=kit.fromFirstOccurrenceOf(" / ",false,false);}
    const auto result=database.searchSampleBrowseRecords(options,250,sampleOffset);for(const auto& record:result.records)samples.add(record);sampleOffset=samples.size();hasMoreSamples=result.hasMore;
    indexedList.updateContent();if(reset)indexedList.deselectAllRows();sampleMap->setSamples(samples,-1);moreSamplesButton.setEnabled(hasMoreSamples);status.setText(juce::String(samples.size())+(hasMoreSamples?"+":"")+" "+magdrum::ui::localisedText("samples · select, preview or use Sample tools"),juce::dontSendNotification);
}
juce::File MagDrumAudioProcessorEditor::selectedSample()const{const int row=indexedList.getSelectedRow();return juce::isPositiveAndBelow(row,samples.size())?samples.getReference(row).file:juce::File();}

void MagDrumAudioProcessorEditor::chooseSampleFolder()
{
    if(scanning){if(scanningDefaultLibraries){scanningDefaultLibraries=false;defaultLibraryFolders.clear();}if(scanCancellation)scanCancellation->store(true);scanButton.setButtonText(magdrum::ui::localisedText("Cancelling…"));scanButton.setEnabled(false);status.setText(magdrum::ui::localisedText("Stopping the current folder scan…"),juce::dontSendNotification);return;}
    chooser=std::make_unique<juce::FileChooser>("Choose a drum sample folder",juce::File(),juce::String());
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser& fileChooser){if(safe!=nullptr){const auto folder=fileChooser.getResult();if(folder.isDirectory())safe->startSampleScan(folder);}});
}

void MagDrumAudioProcessorEditor::scanDefaultLibraryFolders()
{
    if(scanning)
    {
        status.setText(magdrum::ui::localisedText("A sample scan is already running."),juce::dontSendNotification);
        return;
    }
    if(!databaseReady||databaseFile==juce::File())
    {
        status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),juce::dontSendNotification);
        return;
    }

    defaultLibraryFolders=magdrum::library::findDefaultLibraryFolders();
    if(defaultLibraryFolders.empty())
    {
        status.setText(magdrum::ui::localisedText("No default library folders were found. Use Add sample folder to choose a custom location."),juce::dontSendNotification);
        return;
    }

    defaultLibraryFolderIndex=0;
    defaultLibraryIndexedCount=0;
    defaultLibraryErrorCount=0;
    defaultMidiPatterns.clear();
    browseMidiButton.setButtonText(magdrum::ui::localisedText("Find drum MIDI"));
    browseMidiButton.setEnabled(false);
    scanningDefaultLibraries=true;
    scanDefaultsButton.setEnabled(false);
    startNextDefaultLibraryFolder();
}

void MagDrumAudioProcessorEditor::launchInstrumentScanner()
{
    const auto helper = bundledScannerApplication();
    if (!helper.existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }
    if (!helper.startAsProcess("--magdrum-plugin-browser"))
    {
        status.setText(magdrum::ui::localisedText("Could not start the MagDrum instrument scanner."), juce::dontSendNotification);
        return;
    }
    status.setText(magdrum::ui::localisedText("Opened the isolated instrument scanner. Its captured samples will appear in this shared library."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::openSourceKitForSelectedSample()
{
    const int row = indexedList.getSelectedRow();
    if (! juce::isPositiveAndBelow(row, samples.size()))
    {
        status.setText(magdrum::ui::localisedText("Select a sample first."), juce::dontSendNotification);
        return;
    }

    const auto& sample = samples.getReference(row);
    PluginSourceKitRequest request{ sample.pluginIdentifier, sample.kitName };
    if (! request.isValid())
    {
        status.setText(magdrum::ui::localisedText("The selected sample has no saved source kit."), juce::dontSendNotification);
        return;
    }

    const auto helper = bundledScannerApplication();
    if (! helper.existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum helper was not found beside this VST3."), juce::dontSendNotification);
        return;
    }

    const auto requestDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("MagDrum");
    if (! requestDirectory.isDirectory() && ! requestDirectory.createDirectory().wasOk())
    {
        status.setText(magdrum::ui::localisedText("Could not open the saved source kit."), juce::dontSendNotification);
        return;
    }

    const auto requestFile = requestDirectory.getNonexistentChildFile("source-kit-", ".json", false);
    if (! requestFile.replaceWithText(juce::JSON::toString(request.toVar(), true)))
    {
        status.setText(magdrum::ui::localisedText("Could not open the saved source kit."), juce::dontSendNotification);
        return;
    }

    const auto arguments = "--magdrum-open-source-kit \"" + requestFile.getFullPathName().replace("\"", "\\\"") + "\"";
    if (! helper.startAsProcess(arguments))
    {
        requestFile.deleteFile();
        status.setText(magdrum::ui::localisedText("Could not open the saved source kit."), juce::dontSendNotification);
        return;
    }

    status.setText(magdrum::ui::localisedText("Opened the saved source kit in MagDrum."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::startNextDefaultLibraryFolder()
{
    if(defaultLibraryFolderIndex>=defaultLibraryFolders.size())
    {
        searchDefaultMidiPatterns();
        return;
    }

    startSampleScan(defaultLibraryFolders[defaultLibraryFolderIndex]);
}

void MagDrumAudioProcessorEditor::searchDefaultMidiPatterns()
{
    if(defaultLibraryFolders.empty())
    {
        finishDefaultMidiPatternSearch({});
        return;
    }
    scanButton.setEnabled(false);
    scanButton.setButtonText(magdrum::ui::localisedText("Searching MIDI…"));
    status.setText(magdrum::ui::localisedText("Searching detected library folders for drum MIDI patterns…"),juce::dontSendNotification);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    auto completion=[safe](std::vector<juce::File> patterns)
    {
        juce::MessageManager::callAsync([safe,patterns=std::move(patterns)]() mutable
        {
            if(safe!=nullptr)
                safe->finishDefaultMidiPatternSearch(std::move(patterns));
        });
    };
    scanWorkers.addJob(new PluginMidiPatternDiscoveryJob(defaultLibraryFolders,std::move(completion)),true);
}

void MagDrumAudioProcessorEditor::finishDefaultMidiPatternSearch(std::vector<juce::File> patterns)
{
    defaultMidiPatterns=std::move(patterns);
    const auto folderCount=static_cast<int>(defaultLibraryFolders.size());
    scanningDefaultLibraries=false;
    defaultLibraryFolders.clear();
    scanButton.setEnabled(true);
    scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));
    scanDefaultsButton.setEnabled(true);
    browseMidiButton.setButtonText(defaultMidiPatterns.empty()?magdrum::ui::localisedText("Find drum MIDI"):magdrum::ui::localisedText("Drum MIDI (")+juce::String(static_cast<int>(defaultMidiPatterns.size()))+")");
    browseMidiButton.setEnabled(true);
    refreshSourceKitFilters();
    refreshSamples();
    status.setText(magdrum::ui::localisedText("Library scan complete")+" · "+juce::String(defaultLibraryIndexedCount)+" "+magdrum::ui::localisedText("audio samples indexed across")+" "+juce::String(folderCount)+" "+magdrum::ui::localisedText("locations")+" · "+juce::String(static_cast<int>(defaultMidiPatterns.size()))+" "+magdrum::ui::localisedText("drum MIDI patterns found")+" · "+juce::String(defaultLibraryErrorCount)+" "+magdrum::ui::localisedText("errors")+".",juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::browseDefaultMidiPatterns()
{
    if(defaultMidiPatterns.empty())
    {
        status.setText(magdrum::ui::localisedText("Run Scan installed libraries to search their default locations for drum MIDI patterns."),juce::dontSendNotification);
        return;
    }
    juce::PopupMenu menu;
    const auto count=juce::jmin(200,static_cast<int>(defaultMidiPatterns.size()));
    for(int i=0;i<count;++i)
    {
        const auto& file=defaultMidiPatterns[static_cast<size_t>(i)];
        menu.addItem(i+1,file.getFileName()+"   ·   "+file.getParentDirectory().getFileName());
    }
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&browseMidiButton),[safe](int result)
    {
        if(safe==nullptr||result<1||result>static_cast<int>(safe->defaultMidiPatterns.size()))
            return;
        const auto file=safe->defaultMidiPatterns[static_cast<size_t>(result-1)];
        safe->loadMidiPatternFile(file);
    });
}

void MagDrumAudioProcessorEditor::startSampleScan(const juce::File& folder)
{
    if(!databaseReady||databaseFile==juce::File()){status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),juce::dontSendNotification);return;}
    scanning=true;scanCancellation=std::make_shared<std::atomic<bool>>(false);scanButton.setEnabled(true);scanButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));status.setText(magdrum::ui::localisedText("Scanning folder: ")+folder.getFileName()+"…",juce::dontSendNotification);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    auto completion=[safe](const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error){juce::MessageManager::callAsync([safe,stats,indexed,error]{if(safe!=nullptr)safe->finishSampleScan(stats,indexed,error);});};
    auto progress=[safe](const juce::File& file){juce::MessageManager::callAsync([safe,name=file.getFileName()]{if(safe!=nullptr&&safe->scanning)safe->status.setText(magdrum::ui::localisedText("Scanning sample: ")+name,juce::dontSendNotification);});};
    scanWorkers.addJob(new PluginSampleScanJob(folder,databaseFile,scanCancellation,std::move(completion),std::move(progress)),true);
}

void MagDrumAudioProcessorEditor::finishSampleScan(const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error)
{
    scanning=false;scanCancellation.reset();scanButton.setEnabled(true);scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));refreshSourceKitFilters();refreshSamples();
    if(scanningDefaultLibraries)
    {
        defaultLibraryIndexedCount+=indexed;
        defaultLibraryErrorCount+=stats.errors+(error.isNotEmpty()?1:0);
        if(stats.cancelled)
        {
            scanningDefaultLibraries=false;
            defaultLibraryFolders.clear();
            scanDefaultsButton.setEnabled(true);
            browseMidiButton.setEnabled(true);
            status.setText(magdrum::ui::localisedText("Library scan cancelled · ")+juce::String(defaultLibraryIndexedCount)+" "+magdrum::ui::localisedText("audio samples indexed so far."),juce::dontSendNotification);
            return;
        }
        ++defaultLibraryFolderIndex;
        startNextDefaultLibraryFolder();
        return;
    }
    if(error.isNotEmpty())status.setText(magdrum::ui::localisedText(error),juce::dontSendNotification);
    else status.setText(magdrum::ui::localisedText(stats.cancelled?"Scan cancelled. ":"Scan complete. ")+juce::String(indexed)+" "+magdrum::ui::localisedText("indexed, ")+juce::String(stats.filesSkippedUnchanged)+" "+magdrum::ui::localisedText("unchanged, ")+juce::String(stats.errors)+" "+magdrum::ui::localisedText("errors."),juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::showLibraryTools()
{
    juce::PopupMenu menu;menu.addItem(1,magdrum::ui::localisedText("Edit sample tags…"));
    juce::PopupMenu categories;const char* names[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<12;++i)categories.addItem(100+i,magdrum::ui::localisedText(names[i]));menu.addSubMenu(magdrum::ui::localisedText("Set drum category"),categories);
    menu.addSeparator();menu.addItem(2,magdrum::ui::localisedText("Find similar samples"));menu.addItem(3,magdrum::ui::localisedText("Find exact duplicates"));menu.addItem(4,magdrum::ui::localisedText("Find near duplicates"));menu.addItem(5,magdrum::ui::localisedText("Compare two selected waveforms"));menu.addSeparator();menu.addItem(6,magdrum::ui::localisedText("Scan history"));menu.addItem(7,magdrum::ui::localisedText("Export portable metadata…"));menu.addItem(8,magdrum::ui::localisedText("Import portable metadata…"));
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryToolsButton),[safe](int result){if(safe!=nullptr)safe->handleLibraryTool(result);});
}

void MagDrumAudioProcessorEditor::handleLibraryTool(int action)
{
    if(action>=100&&action<112)
    {
        const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample before changing its category."),juce::dontSendNotification);return;}
        static const char* categories[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        if(database.setUserCategory(file,categories[action-100])){refreshSamples();status.setText(magdrum::ui::localisedText("Category saved as ")+magdrum::ui::localisedText(categories[action-100])+".",juce::dontSendNotification);}else status.setText(magdrum::ui::localisedText("Could not save the sample category."),juce::dontSendNotification);return;
    }
    if(action==0)return;
    if(action==1)
    {
        const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample before editing its tags."),juce::dontSendNotification);return;}
        auto* dialog=new juce::AlertWindow("Edit sample tags",file.getFileName(),juce::AlertWindow::NoIcon);dialog->addTextEditor("tags",database.getUserTags(file).joinIntoString(", "),"Comma-separated tags");dialog->addButton("Save",1);dialog->addButton("Cancel",0);dialog->setSize(480,180);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,safeDialog,file](int result){if(result!=1||safe==nullptr||safeDialog==nullptr)return;juce::StringArray tags;tags.addTokens(safeDialog->getTextEditorContents("tags"),",","\"");tags.removeEmptyStrings();if(safe->database.setUserTags(file,tags)){safe->refreshSamples();safe->status.setText(magdrum::ui::localisedText("Sample tags saved."),juce::dontSendNotification);}else safe->status.setText(magdrum::ui::localisedText("Could not save sample tags."),juce::dontSendNotification);}),true);return;
    }
    const auto file=selectedSample();
    if(action>=2&&action<=4&&file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}
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
        const auto selected=indexedList.getSelectedRows();if(selected.size()!=2){status.setText(magdrum::ui::localisedText("Ctrl-click exactly two samples in the list to compare their waveforms."),juce::dontSendNotification);return;}const auto first=juce::isPositiveAndBelow(selected[0],samples.size())?samples.getReference(selected[0]).file:juce::File();const auto second=juce::isPositiveAndBelow(selected[1],samples.size())?samples.getReference(selected[1]).file:juce::File();if(first==juce::File()||second==juce::File())return;comparisonWindow=std::make_unique<MagDrumSampleComparisonWindow>(first,second);return;
    }
    if(action==6)
    {
        const auto scans=database.getRecentSampleScans();juce::String report;for(const auto& scan:scans)report<<scan.startedAt<<"  ·  "<<scan.status<<"\n"<<scan.rootPath<<"\n"<<scan.filesIndexed<<" indexed, "<<scan.filesSkipped<<" unchanged, "<<scan.errors<<" errors\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,"Sample scan history",report.isEmpty()?"No sample scans have been recorded.":report,"Close");return;
    }
    if(action==7||action==8)
    {
        const bool exporting=action==7;chooser=std::make_unique<juce::FileChooser>(exporting?"Export MagDrum metadata":"Import MagDrum metadata",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Metadata.json"),"*.json");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(exporting?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),[safe,exporting](const juce::FileChooser& fc){if(safe==nullptr)return;auto target=fc.getResult();if(target==juce::File())return;if(exporting){const auto json=juce::JSON::toString(safe->database.exportPortableMetadata(),true);if(target.getFileExtension().isEmpty())target=target.withFileExtension(".json");const bool saved=target.replaceWithText(json);safe->status.setText(magdrum::ui::localisedText(saved?"Portable metadata exported.":"Could not write metadata archive."),juce::dontSendNotification);}else{const auto archive=juce::JSON::parse(target.loadFileAsString());if(archive.isVoid()){safe->status.setText(magdrum::ui::localisedText("The metadata archive is not valid JSON."),juce::dontSendNotification);return;}const auto merged=safe->database.mergePortableMetadata(archive);safe->refreshSamples();auto message=magdrum::ui::localisedText(merged.ok?"Metadata imported: {tags} tags added, {profiles} mapping profiles added.":"Metadata import failed: {error}");message=message.replace("{tags}",juce::String(merged.tagsAdded)).replace("{profiles}",juce::String(merged.profilesAdded)).replace("{error}",merged.error);safe->status.setText(message,juce::dontSendNotification);}});return;
    }
}

void MagDrumAudioProcessorEditor::setPage(int value){page=juce::jlimit(0,3,value);resized();repaint();}
void MagDrumAudioProcessorEditor::setNote(int note){selectedNote=juce::jlimit(0,127,note);const int index=static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin());const int safeIndex=juce::jlimit(0,static_cast<int>(padNames.size())-1,index);for(size_t i=0;i<pads.size();++i){pads[i].setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));pads[i].setToggleState(static_cast<int>(i)==index,juce::dontSendNotification);pads[i].setColour(juce::TextButton::buttonColourId,static_cast<int>(i)==index?categoryColour(padNames[i]).withAlpha(0.34f):Palette::Surface);}selectedPadTitle.setText(localizedPadName(static_cast<size_t>(safeIndex))+"   ·   "+noteName(selectedNote)+"   ·   MIDI "+juce::String(selectedNote),juce::dontSendNotification);updateLayerSummary();const auto layers=processor.getSampleLayersForNote(selectedNote);sampleDetails.setText(layers.empty()?magdrum::ui::localisedText("Selected pad sound")+"\n"+magdrum::ui::localisedText("No sample loaded"):magdrum::ui::localisedText("PAD SAMPLE")+"\n"+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path,juce::dontSendNotification);}
void MagDrumAudioProcessorEditor::updateDrumNotes()
{
    const int previousNote=selectedNote;
    mappedPadNotes=defaultPadNotes;mappedLaneNotes=defaultLaneNotes;
    const bool noteStillPresent=std::find(mappedPadNotes.begin(),mappedPadNotes.end(),previousNote)!=mappedPadNotes.end();
    setNote(noteStillPresent?previousNote:36);
}

void MagDrumAudioProcessorEditor::refreshMappingSummary()
{
    static const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};static const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
    juce::String text=magdrum::ui::localisedText("MIDI mapping")+": "+profileName.getText()+"\n\n";
    for(const auto& entry:mappingProfile.entries())
        text+=magdrum::ui::localisedText(instruments[static_cast<size_t>(entry.instrument)])+"  /  "+magdrum::ui::localisedText(articulations[static_cast<size_t>(entry.articulation)])+"   →   MIDI "+juce::String(entry.note)+"  ("+noteName(entry.note)+")\n";
    mappingSummary.setText(text,juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::savePluginEditorState()
{
    juce::XmlElement saved("PLUGIN_EDITOR_STATE");
    saved.setAttribute("eventModel",1);
    saved.setAttribute("profileName",profileName.getText().trim());
    saved.setAttribute("profileCsv",juce::String::fromUTF8(mappingProfile.toCsv().c_str()));
    saved.setAttribute("tempo",tempoEditor.getText());
    saved.setAttribute("meter",meterSelect.getText());
    saved.setAttribute("bars",lengthSelect.getSelectedId()>0?lengthSelect.getSelectedId():magdrum::sequencer::maximumBars);
    for(size_t lane=0;lane<steps.size();++lane)for(size_t step=0;step<steps[lane].size();++step)if(steps[lane][step].getToggleState()){auto* cell=saved.createNewChildElement("STEP");cell->setAttribute("lane",static_cast<int>(lane));cell->setAttribute("step",static_cast<int>(step));cell->setAttribute("velocity",stepVelocities[lane][step]);}
    for(const auto& meta:sequenceMetadata){
        auto* child=saved.createNewChildElement("META");child->setAttribute("beat",meta.positionBeats);
        if(meta.message.isTempoMetaEvent()){child->setAttribute("kind","tempo");child->setAttribute("microsecondsPerQuarterNote",juce::roundToInt(meta.message.getTempoSecondsPerQuarterNote()*1000000.0));}
        else if(meta.message.isTimeSignatureMetaEvent()){int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);child->setAttribute("kind","meter");child->setAttribute("numerator",numerator);child->setAttribute("denominator",denominator);}
    }
    for(const auto& event:sequenceEvents){auto* child=saved.createNewChildElement("EVENT");child->setAttribute("instrument",static_cast<int>(event.instrument));child->setAttribute("articulation",static_cast<int>(event.articulation));child->setAttribute("velocity",static_cast<int>(event.velocity));child->setAttribute("beat",event.positionBeats);child->setAttribute("duration",event.durationBeats);}
    processor.setPluginEditorState(saved);
}

void MagDrumAudioProcessorEditor::restorePluginEditorState()
{
    const auto saved=processor.getPluginEditorState();if(!saved)return;
    const auto name=saved->getStringAttribute("profileName");const auto csv=saved->getStringAttribute("profileCsv");
    if(name.isNotEmpty()&&csv.isNotEmpty())if(const auto parsed=MidiMappingProfile::fromCsv(csv.toStdString())){mappingProfile=*parsed;profileName.setText(name,juce::dontSendNotification);if(name!="General MIDI"&&databaseReady){database.saveMappingProfile(name,mappingProfile);int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);}}
    const auto tempo=saved->getStringAttribute("tempo");if(tempo.isNotEmpty())tempoEditor.setText(tempo,juce::dontSendNotification);
    const auto meter=saved->getStringAttribute("meter");const int slash=meter.indexOfChar('/');if(slash>0){const int id=meter.substring(0,slash).getIntValue()*100+meter.substring(slash+1).getIntValue();if(meterSelect.indexOfItemId(id)>=0)meterSelect.setSelectedId(id,juce::dontSendNotification);}previousMeterId=meterSelect.getSelectedId();
    const int savedBars=juce::jlimit(1,magdrum::sequencer::maximumBars,saved->getIntAttribute("bars",magdrum::sequencer::maximumBars));lengthSelect.setSelectedId(savedBars,juce::dontSendNotification);previousPatternBars=savedBars;
    sequenceMetadata.clear();sequenceEvents.clear();sequenceEventsInitialised=saved->getIntAttribute("eventModel",0)==1;
    for(auto* child=saved->getFirstChildElement();child!=nullptr;child=child->getNextElement()){
        if(child->hasTagName("STEP")){const int lane=child->getIntAttribute("lane",-1),step=child->getIntAttribute("step",-1);if(juce::isPositiveAndBelow(lane,static_cast<int>(steps.size()))&&juce::isPositiveAndBelow(step,512)){const auto r=static_cast<size_t>(lane),s=static_cast<size_t>(step);steps[r][s].setToggleState(true,juce::dontSendNotification);stepVelocities[r][s]=juce::jlimit(1,127,child->getIntAttribute("velocity",100));steps[r][s].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[r]).withAlpha(static_cast<float>(stepVelocities[r][s])/127.0f));}}
        else if(child->hasTagName("META")){const double beat=child->getDoubleAttribute("beat",-1.0);if(!std::isfinite(beat)||beat<0.0)continue;const auto kind=child->getStringAttribute("kind");if(kind=="tempo"){const int microseconds=child->getIntAttribute("microsecondsPerQuarterNote",0);if(microseconds>0&&microseconds<=0x00ffffff)sequenceMetadata.push_back({beat,juce::MidiMessage::tempoMetaEvent(microseconds)});}else if(kind=="meter"){MidiPattern checked;if(setPatternTimeSignature(checked,child->getIntAttribute("numerator",0),child->getIntAttribute("denominator",0),beat)&&!checked.tempoAndSignature.empty())sequenceMetadata.push_back(checked.tempoAndSignature.front());}}
        else if(child->hasTagName("EVENT")){const int instrument=child->getIntAttribute("instrument",-1),articulation=child->getIntAttribute("articulation",-1),velocity=child->getIntAttribute("velocity",0);const double beat=child->getDoubleAttribute("beat",-1.0),duration=child->getDoubleAttribute("duration",0.0);if(juce::isPositiveAndBelow(instrument,9)&&juce::isPositiveAndBelow(articulation,13)&&velocity>=1&&velocity<=127&&std::isfinite(beat)&&beat>=0.0&&std::isfinite(duration)&&duration>0.0)sequenceEvents.push_back({static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation),static_cast<std::uint8_t>(velocity),beat,duration});}
    }
    if(!sequenceEventsInitialised)syncSequenceEventsFromGrid();
    std::stable_sort(sequenceMetadata.begin(),sequenceMetadata.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});
    refreshSequenceMetadata();
    updateSequencerGrid();
}

void MagDrumAudioProcessorEditor::chooseMappingCsv(bool exportCsv)
{
    chooser=std::make_unique<juce::FileChooser>(exportCsv?"Export MIDI mapping profile":"Import MIDI mapping profile",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Mapping.csv"),"*.csv");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(exportCsv?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),[safe,exportCsv](const juce::FileChooser& fc){if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(exportCsv){if(file.getFileExtension().isEmpty())file=file.withFileExtension(".csv");const bool ok=file.replaceWithText(juce::String::fromUTF8(safe->mappingProfile.toCsv().c_str()));safe->status.setText(magdrum::ui::localisedText(ok?"MIDI mapping profile exported.":"Could not write CSV profile."),juce::dontSendNotification);}else{const auto parsed=MidiMappingProfile::fromCsv(file.loadFileAsString().toStdString());if(!parsed){safe->status.setText(magdrum::ui::localisedText("The CSV file is not a valid MIDI mapping profile."),juce::dontSendNotification);return;}safe->mappingProfile=*parsed;if(safe->profileName.getText().trim().isEmpty())safe->profileName.setText(file.getFileNameWithoutExtension(),juce::dontSendNotification);safe->database.saveMappingProfile(safe->profileName.getText(),safe->mappingProfile);safe->updateDrumNotes();safe->refreshMappingSummary();safe->savePluginEditorState();safe->status.setText(magdrum::ui::localisedText("Imported MIDI mapping profile."),juce::dontSendNotification);}});
}

void MagDrumAudioProcessorEditor::importMidiPattern()
{
    chooser=std::make_unique<juce::FileChooser>("Import drum MIDI pattern",juce::File(),"*.mid;*.midi");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe!=nullptr)safe->loadMidiPatternFile(fc.getResult());});
}

void MagDrumAudioProcessorEditor::loadMidiPatternFile(const juce::File& file)
{
    if(!file.existsAsFile())return;
    MidiPattern pattern;
    const auto report=MidiPatternIO{}.read(file,mappingProfile,pattern);
    if(!report.ok){status.setText(magdrum::ui::localisedText("Could not import the selected MIDI file."),juce::dontSendNotification);return;}
    int importedNumerator=4,importedDenominator=4;
    bool foundInitialMeter=false,ignoredMeterChanges=false,unsupportedMeter=false;
    for(const auto& meta:pattern.tempoAndSignature)
        if(meta.message.isTimeSignatureMetaEvent())
        {
            int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);
            if(std::abs(meta.positionBeats)<0.000001&&!foundInitialMeter){importedNumerator=numerator;importedDenominator=denominator;foundInitialMeter=true;}
            else ignoredMeterChanges=true;
        }
    meterSelect.setSelectedId(404,juce::dontSendNotification);
    if(foundInitialMeter)
    {
        const int id=importedNumerator*100+importedDenominator;
        if(meterSelect.indexOfItemId(id)>=0)meterSelect.setSelectedId(id,juce::dontSendNotification);
        else unsupportedMeter=true;
    }
    previousMeterId=meterSelect.getSelectedId();
    sequenceMetadata=pattern.tempoAndSignature;
    refreshSequenceMetadata();
    const double beatsPerBar=static_cast<double>(timeSignatureNumerator())*4.0/static_cast<double>(timeSignatureDenominator());
    const int importedBars=juce::jlimit(1,magdrum::sequencer::maximumBars,static_cast<int>(std::ceil(std::max(0.0,pattern.lengthBeats)/beatsPerBar-1.0e-9)));
    lengthSelect.setSelectedId(importedBars,juce::dontSendNotification);previousPatternBars=importedBars;
    updateSequencerGrid();
    for(size_t lane=0;lane<steps.size();++lane)for(size_t step=0;step<steps[lane].size();++step){steps[lane][step].setToggleState(false,juce::dontSendNotification);stepVelocities[lane][step]=100;steps[lane][step].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[lane]).withAlpha(100.0f/127.0f));}
    int imported=0,outside=0;
    for(const auto& event:pattern.events)
    {
        int lane=-1;
        switch(event.instrument){case DrumInstrument::Kick:lane=0;break;case DrumInstrument::Snare:case DrumInstrument::Rimshot:lane=1;break;case DrumInstrument::HiHat:lane=event.articulation==DrumArticulation::Open?3:2;break;case DrumInstrument::Tom:lane=4;break;case DrumInstrument::Ride:lane=5;break;case DrumInstrument::Crash:lane=6;break;case DrumInstrument::Clap:lane=7;break;default:break;}
        const int step=juce::roundToInt(event.positionBeats*4.0);
        if(lane<0||step<0||step>=patternStepCount()){++outside;continue;}
        const auto row=static_cast<size_t>(lane),column=static_cast<size_t>(step);
        steps[row][column].setToggleState(true,juce::dontSendNotification);
        stepVelocities[row][column]=event.velocity;
        steps[row][column].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(static_cast<float>(event.velocity)/127.0f));
        ++imported;
    }
    for(const auto& meta:pattern.tempoAndSignature)if(std::abs(meta.positionBeats)<1.0e-9&&meta.message.isTempoMetaEvent()){const double seconds=meta.message.getTempoSecondsPerQuarterNote();if(seconds>0.0)tempoEditor.setText(juce::String(juce::roundToInt(60.0/seconds)),juce::dontSendNotification);break;}
    sequenceEvents=pattern.events;sequenceEventsInitialised=true;importedMidiBaseline=currentSequencePattern();hasImportedMidiBaseline=true;reviewMidiButton.setEnabled(true);editMidiEventsButton.setEnabled(true);
    savePluginEditorState();
    auto importMessage=magdrum::ui::localisedText("Imported {hits} hits from {file}. {unmapped} unmapped notes; {ambiguous} ambiguous notes; {outside} hits outside the imported pattern length.{unsupported}{meterChanges}");importMessage=importMessage.replace("{hits}",juce::String(imported)).replace("{file}",file.getFileName()).replace("{unmapped}",juce::String(report.unmappedNotes.size())).replace("{ambiguous}",juce::String(report.ambiguousNotes.size())).replace("{outside}",juce::String(outside)).replace("{unsupported}",unsupportedMeter?" "+magdrum::ui::localisedText("Unsupported meter; used 4/4."):juce::String()).replace("{meterChanges}",ignoredMeterChanges?" "+magdrum::ui::localisedText("Later meter events are preserved for MIDI export; the grid shows the opening meter."):juce::String());status.setText(importMessage,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::updateLayerSummary()
{
    const auto layers=processor.getSampleLayersForNote(selectedNote);juce::String text;
    if(layers.empty())text=magdrum::ui::localisedText("No sample loaded")+"\n"+magdrum::ui::localisedText("Load WAV");
    else for(size_t i=0;i<layers.size();++i){if(i>0)text+="\n";text+=magdrum::ui::localisedText("Velocity")+" "+juce::String(layers[i].minimumVelocity)+"–"+juce::String(layers[i].maximumVelocity)+"\n"+juce::File(layers[i].path).getFileName();}
    layerSummary.setText(text,juce::dontSendNotification);
    sampleDetails.setText(magdrum::ui::localisedText(layers.empty()?"PAD SAMPLE\nNo sample loaded":"PAD SAMPLE")+(layers.empty()?juce::String():"\n"+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path),juce::dontSendNotification);
    waveform->setFile(layers.empty()?juce::File():juce::File(layers.front().path));
}

void MagDrumAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if(button==&libraryTab){setPage(0);return;}if(button==&machineTab){setPage(1);return;}if(button==&sequencerTab){setPage(2);return;}if(button==&mapsTab){setPage(3);return;}
    for(size_t lane=0;lane<steps.size();++lane)for(size_t step=0;step<steps[lane].size();++step)if(button==&steps[lane][step]){syncSequenceEventForCell(static_cast<int>(lane),static_cast<int>(step));savePluginEditorState();return;}
    if(button==&scanButton){chooseSampleFolder();return;}if(button==&scanDefaultsButton){scanDefaultLibraryFolders();return;}if(button==&instrumentScannerButton){launchInstrumentScanner();return;}if(button==&openSourceKitButton){openSourceKitForSelectedSample();return;}
    if(button==&searchButton){refreshSamples();return;}if(button==&libraryToolsButton){showLibraryTools();return;}if(button==&moreSamplesButton){if(hasMoreSamples)loadSamplePage(false);return;}
    if(button==&previewButton){const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample to preview."),juce::dontSendNotification);return;}status.setText(processor.previewSample(file)?magdrum::ui::localisedText("Previewing ")+file.getFileName():magdrum::ui::localisedText("Could not read this audio file."),juce::dontSendNotification);return;}
    if(button==&assignButton){const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}if(processor.loadSampleForVelocityRange(selectedNote,velocityMinimum(velocityRange.getText()),velocityMaximum(velocityRange.getText()),file)){updateLayerSummary();const auto padIndex=static_cast<size_t>(std::max(0,static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin())));status.setText(magdrum::ui::localisedText("Assigned ")+file.getFileName()+magdrum::ui::localisedText(" to ")+localizedPadName(padIndex)+".",juce::dontSendNotification);}else status.setText(magdrum::ui::localisedText("Assignment failed. Check velocity ranges and the eight-layer limit."),juce::dontSendNotification);return;}
    for(size_t i=0;i<pads.size();++i)if(button==&pads[i]){setNote(mappedPadNotes[i]);processor.triggerNote(selectedNote);status.setText(processor.getSampleLayersForNote(selectedNote).empty()?magdrum::ui::localisedText("Pad selected · no sample assigned yet."):magdrum::ui::localisedText("Played ")+localizedPadName(i),juce::dontSendNotification);return;}
    if(button==&clearPadButton){status.setText(processor.clearSamplesForNote(selectedNote)?magdrum::ui::localisedText("Pad cleared."):magdrum::ui::localisedText("Pad is already empty."),juce::dontSendNotification);updateLayerSummary();return;}
    if(button==&loadButton){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose a drum sample"),juce::File(),"*.wav;*.aif;*.aiff;*.flac",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr)return;const auto file=fc.getResult();if(!file.existsAsFile())return;if(safe->processor.loadSampleForVelocityRange(safe->selectedNote,velocityMinimum(safe->velocityRange.getText()),velocityMaximum(safe->velocityRange.getText()),file)){safe->updateLayerSummary();safe->status.setText(magdrum::ui::localisedText("Loaded ")+file.getFileName(),juce::dontSendNotification);}else safe->status.setText(magdrum::ui::localisedText("Could not assign sample. Check range, format and layer limit."),juce::dontSendNotification);});return;}
    if(button==&loadProfileButton){const auto name=profileSelect.getText();if(name=="General MIDI"){mappingProfile=MidiMappingProfile::generalMidi();}else if(const auto loaded=database.loadMappingProfile(name))mappingProfile=*loaded;else{status.setText(magdrum::ui::localisedText("Could not load mapping profile."),juce::dontSendNotification);return;}profileName.setText(name,juce::dontSendNotification);updateDrumNotes();refreshMappingSummary();status.setText(magdrum::ui::localisedText("Loaded MIDI profile: ")+name,juce::dontSendNotification);return;}
    if(button==&saveProfileButton){const auto name=profileName.getText().trim();if(name.isEmpty()||!database.saveMappingProfile(name,mappingProfile)){status.setText(magdrum::ui::localisedText("Could not save mapping profile."),juce::dontSendNotification);return;}int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);savePluginEditorState();status.setText(magdrum::ui::localisedText("Saved MIDI profile: ")+name,juce::dontSendNotification);return;}
    if(button==&setMappingButton||button==&removeMappingButton){const int instrument=mappingInstrument.getSelectedId()-1,articulation=mappingArticulation.getSelectedId()-1,note=mappingNote.getText().getIntValue();if(instrument<0||articulation<0||(button==&setMappingButton&&(note<0||note>127))){status.setText(magdrum::ui::localisedText("Choose a drum, articulation and MIDI note from 0 to 127."),juce::dontSendNotification);return;}const bool changed=button==&setMappingButton?mappingProfile.setMapping({static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation),static_cast<std::uint8_t>(note)}):mappingProfile.removeMapping(static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation));if(changed){database.saveMappingProfile(profileName.getText().trim().isEmpty()?"General MIDI":profileName.getText().trim(),mappingProfile);updateDrumNotes();refreshMappingSummary();savePluginEditorState();status.setText(magdrum::ui::localisedText("MIDI mapping updated."),juce::dontSendNotification);}else status.setText(magdrum::ui::localisedText("No matching mapping was changed."),juce::dontSendNotification);return;}
    if(button==&importCsvButton){chooseMappingCsv(false);return;}if(button==&exportCsvButton){chooseMappingCsv(true);return;}if(button==&importMidiButton){importMidiPattern();return;}if(button==&browseMidiButton){browseDefaultMidiPatterns();return;}if(button==&editMidiEventsButton){openMidiEventEditor();return;}if(button==&reviewMidiButton){reviewMidiChanges();return;}
    if(button==&setMetadataButton){editSequenceMetadata(false);return;}if(button==&removeMetadataButton){editSequenceMetadata(true);return;}
    if(button==&playSequenceButton){if(sequencerPlaying){sequencerPlaying=false;stopTimer();playSequenceButton.setButtonText(magdrum::ui::localisedText("Play"));}else{const int bpm=sequenceTempoAtBeat(0.0);if(bpm<20||bpm>300){status.setText(magdrum::ui::localisedText("Tempo must be between 20 and 300 BPM."),juce::dontSendNotification);return;}savePluginEditorState();sequenceTempo=bpm;sequenceStep=0;sequencerPlaying=true;playSequenceButton.setButtonText(magdrum::ui::localisedText("Stop"));startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*4.0))));}return;}
    if(button==&clearSequenceButton){for(size_t lane=0;lane<steps.size();++lane)for(size_t step=0;step<steps[lane].size();++step){steps[lane][step].setToggleState(false,juce::dontSendNotification);stepVelocities[lane][step]=100;steps[lane][step].setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[lane]).withAlpha(100.0f/127.0f));}syncSequenceEventsFromGrid();savePluginEditorState();status.setText(magdrum::ui::localisedText("Pattern cleared."),juce::dontSendNotification);return;}
    if(button==&exportSequenceButton)
    {
        const int bpm=tempoEditor.getText().getIntValue();
        if(bpm<20||bpm>300){status.setText(magdrum::ui::localisedText("Tempo must be between 20 and 300 BPM."),juce::dontSendNotification);return;}
        savePluginEditorState();
        auto pattern=currentSequencePattern();
        chooser=std::make_unique<juce::FileChooser>("Export drum pattern",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Pattern.mid"),"*.mid",true,false,this);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe,pattern](const juce::FileChooser& fc)
        {
            if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=file.withFileExtension(".mid");
            std::vector<DrumEvent> unsupported;std::vector<MidiFallbackUse> fallbacks;
            const bool ok=MidiPatternIO{}.write(file,pattern,safe->mappingProfile,true,&unsupported,&fallbacks);
            auto message=magdrum::ui::localisedText(ok?"Exported MIDI pattern: {file}. {fallbacks} mapping fallbacks.":"Could not write MIDI file.");message=message.replace("{file}",file.getFileName()).replace("{fallbacks}",juce::String(fallbacks.size()));safe->status.setText(message,juce::dontSendNotification);
        });
        return;
    }
}

void MagDrumAudioProcessorEditor::timerCallback()
{
    if(!sequencerPlaying)return;const int step=sequenceStep;for(int row=0;row<8;++row)if(steps[static_cast<size_t>(row)][static_cast<size_t>(step)].getToggleState())processor.triggerNote(mappedLaneNotes[static_cast<size_t>(row)],stepVelocities[static_cast<size_t>(row)][static_cast<size_t>(step)]);sequenceStep=(sequenceStep+1)%patternStepCount();sequenceTempo=sequenceTempoAtBeat(sequenceStep*0.25);startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*4.0))));
}
