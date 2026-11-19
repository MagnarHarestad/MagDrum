#include "PluginEditor.h"
#include "DrumPadLayout.h"
#include "../core/MagDrumPaths.h"
#include "../core/PluginSourceKitRequest.h"
#include "../core/PluginNoteScanner.h"
#include "../core/PluginScanProcess.h"
#include "../core/PluginVelocityLayerAnalyzer.h"
#include "../core/PluginRoundRobinAnalyzer.h"
#include "../core/DefaultLibraryDiscovery.h"
#include "../core/ScopedFileAccess.h"
#include "../core/WavScanner.h"
#include "../core/AudioClassifier.h"
#include "../core/SampleSimilarityProjection.h"
#include "../ui/MagDrumLocalization.h"
#include "../ui/PortableMetadataConflictDialog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if JUCE_MAC || JUCE_LINUX
#include <dlfcn.h>
#endif

namespace
{
using magdrum::ui::Palette;
using magdrum::plugin::defaultPadNotes;
using magdrum::plugin::padArticulations;
using magdrum::plugin::padInstruments;
using magdrum::plugin::padNames;
#if defined(MAGDRUM_MOBILE_PLUGIN_UI)
constexpr bool hasPluginScannerPage = false;
constexpr int highestEditorPage = 3;
#else
constexpr bool hasPluginScannerPage = true;
constexpr int highestEditorPage = 4;
#endif
constexpr std::array<const char*, 16> laneNames{"KICK","SNARE","CLOSED HAT","OPEN HAT","TOM","RIDE","CRASH","CLAP","BELL","CRASH","TOM","TOM","TOM","RIMSHOT","PEDAL HI-HAT","PERCUSSION"};
constexpr std::array<int, 16> defaultSequencerMidiNotes{36,38,42,46,43,51,49,39,53,57,50,47,45,37,44,56};
using I=DrumInstrument;using A=DrumArticulation;
constexpr std::array<I,16> laneInstruments{I::Kick,I::Snare,I::HiHat,I::HiHat,I::Tom,I::Ride,I::Crash,I::Clap,I::Ride,I::Crash,I::Tom,I::Tom,I::Tom,I::Rimshot,I::HiHat,I::Percussion};
constexpr std::array<A,16> laneArticulations{A::Default,A::Default,A::Closed,A::Open,A::Default,A::Bow,A::Default,A::Default,A::Bell,A::Edge,A::High,A::Mid,A::Low,A::SideStick,A::Pedal,A::Bell};
juce::Colour categoryColour(const juce::String& value) { return Palette::forDrumCategory(value); }
int sequencerLaneForEvent(const DrumEvent& event, const std::array<int, 16>& mappedNotes)
{
    if (event.midiNote >= 0)
        for (size_t lane = 0; lane < mappedNotes.size(); ++lane)
            if (mappedNotes[lane] == event.midiNote) return static_cast<int>(lane);
    switch(event.instrument)
    {
        case DrumInstrument::Kick: return 0;
        case DrumInstrument::Snare:
            return event.articulation == DrumArticulation::SideStick
                || event.articulation == DrumArticulation::Rimshot ? 13 : 1;
        case DrumInstrument::Rimshot: return event.articulation == DrumArticulation::SideStick ? 13 : 1;
        case DrumInstrument::HiHat: return event.articulation == DrumArticulation::Open ? 3 : event.articulation == DrumArticulation::Pedal ? 14 : 2;
        case DrumInstrument::Tom:
            return event.articulation == DrumArticulation::High ? 10
                : event.articulation == DrumArticulation::Mid ? 11
                : event.articulation == DrumArticulation::Low ? 12 : 4;
        case DrumInstrument::Ride: return event.articulation == DrumArticulation::Bell ? 8 : 5;
        case DrumInstrument::Crash: return event.articulation == DrumArticulation::Edge ? 9 : 6;
        case DrumInstrument::Clap: return 7;
        case DrumInstrument::Percussion: return 15;
        default: return -1;
    }
}
juce::String noteName(int note)
{
    return juce::String::fromUTF8(formatMidiNoteName(note).c_str());
}
magdrum::sequencer::BarMap makeSequencerBarMap(int numerator,int denominator,int bars,
                                                const std::vector<MidiMetaEvent>& metadata,
                                                int subdivisionsPerBeat = 4)
{
    std::vector<magdrum::sequencer::MeterChange> changes;
    changes.reserve(metadata.size());
    for(const auto& meta:metadata)
        if(meta.message.isTimeSignatureMetaEvent())
        {
            int eventNumerator=4,eventDenominator=4;
            meta.message.getTimeSignatureInfo(eventNumerator,eventDenominator);
            changes.push_back({meta.positionBeats,eventNumerator,eventDenominator});
        }
    return magdrum::sequencer::buildBarMap(numerator,denominator,bars,changes,subdivisionsPerBeat);
}
juce::String localizedPadName(size_t index)
{
    const auto source = juce::String(padNames[index]);
    if (source.startsWith("Crash ")) return magdrum::ui::localisedText("Crash") + " " + source.fromFirstOccurrenceOf(" ", false, false);
    if (source.startsWith("Tom ")) return magdrum::ui::localisedText("Tom") + " " + source.fromFirstOccurrenceOf(" ", false, false);
    if (source == "Side Stick") return magdrum::ui::localisedText("Side stick");
    if (source == "Closed HH") return magdrum::ui::localisedText("Closed hi-hat");
    if (source == "Open HH") return magdrum::ui::localisedText("Open hi-hat");
    if (source == "Pedal HH") return magdrum::ui::localisedText("Pedal hi-hat");
    return magdrum::ui::localisedText(padNames[index]);
}
juce::String localizedSequencerLaneName(size_t row)
{
    static constexpr std::array<size_t, 16> padIndexForLane{12,13,10,11,7,3,0,9,2,1,4,5,6,8,14,15};
    return localizedPadName(padIndexForLane[row]);
}
juce::String describeMidiMapping(DrumInstrument instrument, DrumArticulation articulation)
{
    static constexpr const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
    static constexpr const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
    const auto instrumentIndex=static_cast<size_t>(instrument);
    const auto articulationIndex=static_cast<size_t>(articulation);
    if(instrumentIndex>=std::size(instruments)||articulationIndex>=std::size(articulations))return {};
    return magdrum::ui::localisedText(instruments[instrumentIndex])+" / "+magdrum::ui::localisedText(articulations[articulationIndex]);
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
#elif JUCE_MAC || JUCE_LINUX
    static int moduleAnchor = 0;
    Dl_info moduleInfo{};
    if (dladdr(&moduleAnchor, &moduleInfo) == 0 || moduleInfo.dli_fname == nullptr)
        return {};
    const auto moduleFile = juce::File(juce::String::fromUTF8(moduleInfo.dli_fname));
    const auto bundleContents = moduleFile.getParentDirectory().getParentDirectory();
    return bundleContents.getChildFile("Resources").getChildFile("MagDrumScanner");
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

class PluginMidiEventEditor final : public juce::Component, private juce::ListBoxModel, private juce::Button::Listener, private juce::ChangeListener
{
public:
    PluginMidiEventEditor(MidiPattern& value,std::function<void()> callback):pattern(value),onChange(std::move(callback)),events(magdrum::ui::localisedText("Drum events"),this)
    {
        events.setRowHeight(27);addAndMakeVisible(events);
        const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
        for(int i=0;i<9;++i)instrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);
        for(int i=0;i<13;++i)articulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);
        instrument.setSelectedId(1,juce::dontSendNotification);articulation.setSelectedId(1,juce::dontSendNotification);velocity.setText(magdrum::ui::localisedText("100"),juce::dontSendNotification);beat.setText(magdrum::ui::localisedText("0"),juce::dontSendNotification);duration.setText(magdrum::ui::localisedText("0.12"),juce::dontSendNotification);
        for(auto* component:{static_cast<juce::Component*>(&instrument),static_cast<juce::Component*>(&articulation),static_cast<juce::Component*>(&velocity),static_cast<juce::Component*>(&beat),static_cast<juce::Component*>(&duration),static_cast<juce::Component*>(&apply),static_cast<juce::Component*>(&add),static_cast<juce::Component*>(&remove)})addAndMakeVisible(*component);
        velocity.setInputRestrictions(3,"0123456789");beat.setInputRestrictions(12,"0123456789.");duration.setInputRestrictions(12,"0123456789.");
        velocity.setJustification(juce::Justification::centred);beat.setJustification(juce::Justification::centred);duration.setJustification(juce::Justification::centred);
        velocity.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity"),Palette::Muted);beat.setTextToShowWhenEmpty(magdrum::ui::localisedText("Beat"),Palette::Muted);duration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Length in beats"),Palette::Muted);
        apply.setButtonText(magdrum::ui::localisedText("Apply edit"));add.setButtonText(magdrum::ui::localisedText("Add hit"));remove.setButtonText(magdrum::ui::localisedText("Delete hit"));
        for(auto* button:{&apply,&add,&remove})button->addListener(this);
        message.setColour(juce::Label::textColourId,Palette::Muted);addAndMakeVisible(message);
        message.setText(magdrum::ui::localisedText("Select a hit to edit its drum, articulation, velocity, beat and duration."),juce::dontSendNotification);
        magdrum::ui::languageChangeBroadcaster().addChangeListener(this);
        applyLanguage();
        refresh();
    }
    ~PluginMidiEventEditor() override{magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);}
    void resized() override
    {
        auto area=getLocalBounds().reduced(10);events.setBounds(area.removeFromTop(juce::jmax(150,area.getHeight()-78)));area.removeFromTop(6);
        auto row=area.removeFromTop(32);instrument.setBounds(row.removeFromLeft(125));row.removeFromLeft(5);articulation.setBounds(row.removeFromLeft(120));row.removeFromLeft(5);velocity.setBounds(row.removeFromLeft(78));row.removeFromLeft(5);beat.setBounds(row.removeFromLeft(110));row.removeFromLeft(5);duration.setBounds(row.removeFromLeft(115));row.removeFromLeft(5);apply.setBounds(row.removeFromLeft(90));row.removeFromLeft(5);add.setBounds(row.removeFromLeft(76));row.removeFromLeft(5);remove.setBounds(row.removeFromLeft(92));
        area.removeFromTop(4);message.setBounds(area.removeFromTop(26));
    }
private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if(source==&magdrum::ui::languageChangeBroadcaster())applyLanguage();
    }
    void applyLanguage()
    {
        const int instrumentId=instrument.getSelectedId(),articulationId=articulation.getSelectedId();
        static constexpr const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        static constexpr const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};
        events.setName(magdrum::ui::localisedText("Drum events"));
        instrument.clear(juce::dontSendNotification);
        for(int i=0;i<9;++i)instrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);
        articulation.clear(juce::dontSendNotification);
        for(int i=0;i<13;++i)articulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);
        instrument.setSelectedId(instrumentId,juce::dontSendNotification);articulation.setSelectedId(articulationId,juce::dontSendNotification);
        velocity.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity"),Palette::Muted);beat.setTextToShowWhenEmpty(magdrum::ui::localisedText("Beat"),Palette::Muted);duration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Length in beats"),Palette::Muted);
        apply.setButtonText(magdrum::ui::localisedText("Apply edit"));add.setButtonText(magdrum::ui::localisedText("Add hit"));remove.setButtonText(magdrum::ui::localisedText("Delete hit"));
        message.setText(magdrum::ui::localisedText("Select a hit to edit its drum, articulation, velocity, beat and duration."),juce::dontSendNotification);
        events.updateContent();repaint();
    }
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
        g.setColour(Palette::Text);g.drawText(juce::String(row+1)+".  "+instrumentName+" / "+articulationName+"    "+magdrum::ui::localisedText("Velocity")+" "+juce::String(event.velocity)+"    "+magdrum::ui::localisedText("Beat")+" "+juce::String(event.positionBeats,3)+"    "+magdrum::ui::localisedText("Length in beats")+" "+juce::String(event.durationBeats,3),8,0,width-16,height,juce::Justification::centredLeft);
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

class MagDrumSampleMap final : public juce::Component, public juce::SettableTooltipClient
{
    class ProjectionJob final : public juce::ThreadPoolJob
    {
    public:
        ProjectionJob(MagDrumSampleMap& owner, std::uint64_t generation,
                      std::vector<juce::String> fingerprints)
            : ThreadPoolJob("MagDrum acoustic sample map projection"), safeOwner(&owner),
              requestGeneration(generation), fingerprintTexts(std::move(fingerprints)) {}
        JobStatus runJob() override
        {
            auto result = magdrum::projectAcousticFingerprints(fingerprintTexts);
            juce::MessageManager::callAsync([owner = safeOwner, generation = requestGeneration,
                                              points = std::move(result)]() mutable
            {
                if (owner != nullptr) owner->applyProjection(generation, std::move(points));
            });
            return jobHasFinished;
        }
    private:
        juce::Component::SafePointer<MagDrumSampleMap> safeOwner;
        std::uint64_t requestGeneration;
        std::vector<juce::String> fingerprintTexts;
    };
public:
    std::function<void(int)> onSelect;
    std::function<void(int)> onPreview;
    std::function<void()> onStopPreview;
    ~MagDrumSampleMap() override { projectionWorkers.removeAllJobs(true, -1); }
    void setSamples(const juce::Array<SampleBrowseRecord>& value, int selected)
    {
        samples=value;selectedIndex=selected;++projectionGeneration;
        pointForSampleIndex.assign(static_cast<size_t>(samples.size()), -1);
        acousticPoints.clear();
        const auto representativeIndices = magdrum::representativeSampleIndices(static_cast<size_t>(samples.size()));
        acousticPoints.reserve(representativeIndices.size());
        for (const int sampleIndex : representativeIndices)
        {
            const auto fallback = samplePosition(samples.getReference(sampleIndex));
            magdrum::SampleSimilarityPoint point;
            point.sampleIndex = sampleIndex;point.x = fallback.x;point.y = 1.0f - fallback.y;
            pointForSampleIndex[static_cast<size_t>(sampleIndex)] = static_cast<int>(acousticPoints.size());
            acousticPoints.push_back(point);
        }
        std::vector<juce::String> fingerprints;
        fingerprints.reserve(static_cast<size_t>(samples.size()));
        for (const auto& sample : samples) fingerprints.push_back(sample.acousticFingerprint);
        if (!fingerprints.empty())
            projectionWorkers.addJob(new ProjectionJob(*this, projectionGeneration, std::move(fingerprints)), true);
        repaint();
    }
    void setSelected(int selected) { selectedIndex=selected;repaint(); }
    juce::Point<float> pointForSampleForTest(int sampleIndex) const
    {
        const auto found = std::find_if(acousticPoints.begin(), acousticPoints.end(),
            [sampleIndex](const auto& point) { return point.sampleIndex == sampleIndex; });
        return found == acousticPoints.end() ? juce::Point<float>{} : samplePosition(*found, plotBounds());
    }
    std::array<float, 3> viewportForTest() const { return {viewport.zoom, viewport.panX, viewport.panY}; }
    void mouseMove(const juce::MouseEvent& event) override
    {
        const int index=sampleAt(event.position);
        if (isPreviewing && index != previewedIndex)
        {
            isPreviewing = false;
            previewedIndex = -1;
            if (onStopPreview) onStopPreview();
        }
        if(!juce::isPositiveAndBelow(index,samples.size())){setTooltip(juce::String{});return;}
        const auto& sample=samples.getReference(index);
        auto details=sample.file.getFileName()+"\n"+magdrum::ui::localisedText(sample.category)
            +" - "+juce::String(sample.durationSeconds,2)+" s";
        if(sample.brightnessHz>0.0)
            details+=" - "+juce::String(sample.brightnessHz,0)+" Hz";
        if (!hasAcousticFingerprint(index))
            details += "\n" + magdrum::ui::localisedText("Position estimated from duration and brightness");
        if(sample.pluginName.isNotEmpty())
        {
            details+="\n"+sample.pluginName;
            if(sample.kitName.isNotEmpty()) details+=" - "+sample.kitName;
        }
        setTooltip(details);
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        setTooltip(juce::String{});
        if (isPreviewing) { isPreviewing = false; previewedIndex = -1; if (onStopPreview) onStopPreview(); }
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const int closest = sampleAt(event.position);
        if (!juce::isPositiveAndBelow(closest, samples.size())) return;
        selectedIndex = closest; previewedIndex = closest; isPreviewing = true;
        repaint(); if (onSelect) onSelect(closest); if (onPreview) onPreview(closest);
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        const auto plot = plotBounds();
        if (!plot.contains(event.position)) return;
        const float nx = (event.position.x - plot.getX()) / plot.getWidth();
        const float ny = (event.position.y - plot.getY()) / plot.getHeight();
        viewport.zoomAt(wheel.deltaY, nx, ny);
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!isPanning) return;
        const auto plot = plotBounds();
        if (plot.isEmpty()) return;
        viewport.panFrom(panStartX, panStartY, event.position.x - panStart.x,
                         event.position.y - panStart.y, plot.getWidth(), plot.getHeight());
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto bounds=getLocalBounds().toFloat().reduced(14.0f);
        juce::ColourGradient panel(Palette::Surface, bounds.getX(), bounds.getY(),
                                   Palette::Panel, bounds.getRight(), bounds.getBottom(), false);
        g.setGradientFill(panel);g.fillRoundedRectangle(bounds,9.0f);
        const auto plot=plotBounds();
        g.setColour(Palette::Background.withAlpha(0.8f));g.fillRoundedRectangle(plot,5.0f);
        g.setColour(Palette::Border);g.drawRoundedRectangle(plot,5.0f,1.0f);
        for(int i=1;i<4;++i){const float x=plot.getX()+plot.getWidth()*i/4.0f;const float y=plot.getY()+plot.getHeight()*i/4.0f;g.setColour(Palette::Accent.withAlpha(0.10f));g.drawVerticalLine(static_cast<int>(x),plot.getY(),plot.getBottom());g.drawHorizontalLine(static_cast<int>(y),plot.getX(),plot.getRight());}
        g.setColour(Palette::Muted);g.setFont(11.0f);
        auto heading=bounds.toNearestInt().removeFromTop(24).reduced(12,0);
        const int legendWidth=juce::jmin(150,heading.getWidth()/2);
        auto legendArea=heading.removeFromRight(legendWidth);
        heading.removeFromRight(8);
        g.drawText(magdrum::ui::localisedText("ACOUSTIC SIMILARITY MAP"),heading,juce::Justification::centredLeft,true);
        g.setColour(Palette::Accent.withAlpha(0.9f));
        g.drawText(magdrum::ui::localisedText("Similar sounds cluster together"),
                   legendArea,juce::Justification::centredRight,true);
        const auto footer = samples.size() > static_cast<int>(acousticPoints.size())
            ? magdrum::ui::localisedText("Showing a representative subset")
            : magdrum::ui::localisedText("Double-click to preview; scroll to zoom; middle-drag to pan");
        g.drawText(footer,
                   static_cast<int>(plot.getX()),static_cast<int>(plot.getBottom()+4),
                   static_cast<int>(plot.getWidth()),14,juce::Justification::centred);
        // Keep the complete selected-point halo inside the plot when zooming
        // or panning. Clipping only to the border lets the glow spill outside
        // the graph and makes points appear detached from their coordinates.
        constexpr int pointHaloInset = 16;
        g.saveState();
        g.reduceClipRegion(plot.toNearestInt().reduced(pointHaloInset));
        for(const auto& projected : acousticPoints)
        {
            const int i=projected.sampleIndex;
            if(!juce::isPositiveAndBelow(i,samples.size()))continue;
            if(!viewport.contains(projected.x, projected.y))continue;
            const auto& sample=samples.getReference(i);
            const auto position=samplePosition(projected, plot);
            const auto x=position.x, y=position.y;
            const bool selected=i==selectedIndex;const float radius=selected?7.0f:4.2f;
            if(selected){g.setColour(categoryColour(sample.category).withAlpha(0.18f));g.fillEllipse(x-radius-7.0f,y-radius-7.0f,(radius+7.0f)*2,(radius+7.0f)*2);}
            g.setColour(categoryColour(sample.category).withAlpha(selected?1.0f:0.78f));g.fillEllipse(x-radius,y-radius,radius*2,radius*2);
            if(selected){g.setColour(Palette::Text);g.drawEllipse(x-radius-2,y-radius-2,(radius+2)*2,(radius+2)*2,1.4f);}
        }
        if(samples.isEmpty()){g.setColour(Palette::Muted);g.drawFittedText(magdrum::ui::localisedText("Scan installed libraries or add a sample folder to populate the library."),plot.toNearestInt().reduced(22),juce::Justification::centred,3);}
        g.restoreState();
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isMiddleButtonDown())
        {
            isPanning = false;
            if (!plotBounds().contains(event.position)) return;
            isPanning = true; panStart = event.position; panStartX = viewport.panX; panStartY = viewport.panY;
            return;
        }
        const int closest=sampleAt(event.position);
        if(juce::isPositiveAndBelow(closest,samples.size()))
        {
            selectedIndex=closest;
            repaint();
            if(onSelect)onSelect(closest);
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        isPanning = false;
    }
private:
    void applyProjection(std::uint64_t generation, std::vector<magdrum::SampleSimilarityPoint> points)
    {
        if (generation != projectionGeneration) return;
        acousticPoints = std::move(points);
        pointForSampleIndex.assign(static_cast<size_t>(samples.size()), -1);
        for (size_t pointIndex = 0; pointIndex < acousticPoints.size(); ++pointIndex)
        {
            auto& point = acousticPoints[pointIndex];
            if (!juce::isPositiveAndBelow(point.sampleIndex, samples.size())) continue;
            pointForSampleIndex[static_cast<size_t>(point.sampleIndex)] = static_cast<int>(pointIndex);
            if (!point.hasAcousticFingerprint)
            {
                const auto fallback = samplePosition(samples.getReference(point.sampleIndex));
                point.x = fallback.x;point.y = 1.0f - fallback.y;
            }
        }
        repaint();
    }
    bool hasAcousticFingerprint(int index) const
    {
        if (!juce::isPositiveAndBelow(index, static_cast<int>(pointForSampleIndex.size()))) return false;
        const int pointIndex = pointForSampleIndex[static_cast<size_t>(index)];
        return juce::isPositiveAndBelow(pointIndex, static_cast<int>(acousticPoints.size()))
            && acousticPoints[static_cast<size_t>(pointIndex)].hasAcousticFingerprint;
    }
    juce::Rectangle<float> plotBounds() const
    {
        auto plot=getLocalBounds().toFloat().reduced(14.0f).reduced(24.0f,24.0f);
        plot.removeFromTop(16.0f);
        plot.removeFromBottom(20.0f);
        return plot;
    }
    static juce::Point<float> samplePosition(const SampleBrowseRecord& sample)
    {
        const double duration=juce::jlimit(0.0,1.0,
            std::log10(juce::jmax(0.01,sample.durationSeconds)+0.01)/2.0);
        const double brightness=juce::jlimit(0.0,1.0,
            std::log10(juce::jmax(100.0,sample.brightnessHz)/100.0)/2.5);
        return {static_cast<float>(duration), static_cast<float>(1.0 - brightness)};
    }
    juce::Point<float> samplePosition(const magdrum::SampleSimilarityPoint& point,
                                      juce::Rectangle<float> plot) const
    {
        constexpr float pointHaloInset = 16.0f;
        plot = plot.reduced(pointHaloInset);
        const auto normalized = viewport.transform(point.x, point.y);
        return {plot.getX() + 6.0f + normalized.x * (plot.getWidth() - 12.0f),
                plot.getY() + 6.0f + normalized.y * (plot.getHeight() - 12.0f)};
    }
    int sampleAt(juce::Point<float> point) const
    {
        const auto plot=plotBounds();
        if(acousticPoints.empty()||!plot.contains(point))return -1;
        int closest=-1;
        float distance=24.0f;
        for(const auto& projected : acousticPoints)
        {
            if (!viewport.contains(projected.x, projected.y)) continue;
            const float current=point.getDistanceFrom(samplePosition(projected, plot));
            if(current<distance){distance=current;closest=projected.sampleIndex;}
        }
        return closest;
    }
    juce::Array<SampleBrowseRecord> samples;std::vector<magdrum::SampleSimilarityPoint> acousticPoints;
    std::vector<int> pointForSampleIndex;juce::ThreadPool projectionWorkers{1};
    std::uint64_t projectionGeneration=0;int selectedIndex=-1, previewedIndex=-1;
    magdrum::SampleSimilarityViewport viewport;
    float panStartX=0.0f, panStartY=0.0f;
    juce::Point<float> panStart;
    bool isPreviewing=false, isPanning=false;
};

void MagDrumAudioProcessorEditor::dispatchSampleMapDoubleClickForTest(juce::Point<float> position)
{
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::MouseEvent event(source, position, juce::ModifierKeys::leftButtonModifier,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, sampleMap.get(), sampleMap.get(), now, position, now, 2, false);
    sampleMap->mouseDoubleClick(event);
}

void MagDrumAudioProcessorEditor::dispatchSampleMapMoveForTest(juce::Point<float> position)
{
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::MouseEvent event(source, position, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        sampleMap.get(), sampleMap.get(), now, position, now, 0, false);
    sampleMap->mouseMove(event);
}

void MagDrumAudioProcessorEditor::dispatchSampleMapExitForTest()
{
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::Point<float> position;
    const juce::MouseEvent event(source, position, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        sampleMap.get(), sampleMap.get(), now, position, now, 0, false);
    sampleMap->mouseExit(event);
}

void MagDrumAudioProcessorEditor::dispatchSampleMapWheelForTest(juce::Point<float> position, float deltaY)
{
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::MouseEvent event(source, position, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        sampleMap.get(), sampleMap.get(), now, position, now, 0, false);
    sampleMap->mouseWheelMove(event, {0.0f, deltaY, false, false, false});
}

void MagDrumAudioProcessorEditor::dispatchSampleMapMiddleDragForTest(
    juce::Point<float> start, juce::Point<float> end)
{
    const auto now = juce::Time::getCurrentTime();
    const auto source = juce::Desktop::getInstance().getMainMouseSource();
    const juce::MouseEvent down(source, start, juce::ModifierKeys::middleButtonModifier,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, sampleMap.get(), sampleMap.get(), now, start, now, 1, false);
    const juce::MouseEvent drag(source, end, juce::ModifierKeys::middleButtonModifier,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, sampleMap.get(), sampleMap.get(), now, start, now, 1, true);
    const juce::MouseEvent up(source, end, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        sampleMap.get(), sampleMap.get(), now, start, now, 1, true);
    sampleMap->mouseDown(down);
    sampleMap->mouseDrag(drag);
    sampleMap->mouseUp(up);
}

juce::Point<float> MagDrumAudioProcessorEditor::sampleMapPointForTest(int sampleIndex) const
{
    return sampleMap->pointForSampleForTest(sampleIndex);
}

std::array<float, 3> MagDrumAudioProcessorEditor::sampleMapViewportForTest() const
{
    return sampleMap->viewportForTest();
}

class MagDrumWaveform final : public juce::Component, public juce::TooltipClient, private juce::ChangeListener
{
public:
    std::function<void(float, float)> onTrimRangeChanged;
    std::function<void(int, bool)> onTrimGestureChanged;
    juce::String getTooltip() override { return tooltipText; }
    MagDrumWaveform() : thumbnail(512, formats, cache)
    {
        formats.registerBasicFormats();thumbnail.addChangeListener(this);magdrum::ui::languageChangeBroadcaster().addChangeListener(this);
    }
    ~MagDrumWaveform() override { thumbnail.removeChangeListener(this);magdrum::ui::languageChangeBroadcaster().removeChangeListener(this); }
    void setFile(const juce::File& file)
    {
        currentFile=file;thumbnail.setSource(file.existsAsFile()?new juce::FileInputSource(file):nullptr);updateTooltip();repaint();
    }
    void setTrimRange(float start, float end)
    {
        trimStart = juce::jlimit(0.0f, 0.99f, start);
        trimEnd = juce::jlimit(trimStart + 0.01f, 1.0f, end);
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto area=getLocalBounds().toFloat().reduced(2.0f);g.setColour(Palette::Panel);g.fillRoundedRectangle(area,7.0f);g.setColour(Palette::Border.withAlpha(0.7f));g.drawRoundedRectangle(area,7.0f,1.0f);
        g.setColour(Palette::Muted);g.setFont(11.0f);g.drawText(magdrum::ui::localisedText("SAMPLE WAVEFORM"),getLocalBounds().reduced(12,7).removeFromTop(18),juce::Justification::centredLeft);
        auto wave=getLocalBounds().reduced(12,28).toFloat();
        const float startX=wave.getX()+trimStart*wave.getWidth(),endX=wave.getX()+trimEnd*wave.getWidth();
        g.setColour(Palette::Background.withAlpha(0.64f));g.fillRect(juce::Rectangle<float>(wave.getX(),wave.getY(),startX-wave.getX(),wave.getHeight()));g.fillRect(juce::Rectangle<float>(endX,wave.getY(),wave.getRight()-endX,wave.getHeight()));
        g.setColour(Palette::Accent.withAlpha(0.18f));g.drawHorizontalLine(static_cast<int>(wave.getCentreY()),wave.getX(),wave.getRight());
        if(thumbnail.getNumChannels()>0){g.setColour(Palette::Accent);thumbnail.drawChannels(g,wave.toNearestInt(),0.0,thumbnail.getTotalLength(),1.0f);}
        else{g.setColour(Palette::Muted);g.drawFittedText(magdrum::ui::localisedText(currentFile.existsAsFile()?"Reading waveform…":"No sample loaded"),wave.toNearestInt(),juce::Justification::centred,2);}
        g.setColour(Palette::Accent.withAlpha(0.30f));g.fillRect(juce::Rectangle<float>(wave.getX(),wave.getY(),startX-wave.getX(),wave.getHeight()));g.fillRect(juce::Rectangle<float>(endX,wave.getY(),wave.getRight()-endX,wave.getHeight()));
        g.setColour(Palette::Accent);g.drawVerticalLine(juce::roundToInt(startX),wave.getY(),wave.getBottom());g.drawVerticalLine(juce::roundToInt(endX),wave.getY(),wave.getBottom());
        g.fillRoundedRectangle(juce::Rectangle<float>(startX-4.0f,wave.getY(),8.0f,12.0f),3.0f);g.fillRoundedRectangle(juce::Rectangle<float>(endX-4.0f,wave.getY(),8.0f,12.0f),3.0f);
        g.setColour(Palette::Muted);g.setFont(10.0f);g.drawText(juce::String(juce::roundToInt(trimStart*100.0f))+"% — "+juce::String(juce::roundToInt(trimEnd*100.0f))+"%",getLocalBounds().reduced(12,7).removeFromTop(18).withTrimmedLeft(130),juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        const auto wave=getLocalBounds().reduced(12,28).toFloat();if(!wave.contains(event.position))return;
        const float x=wave.getX()+trimStart*wave.getWidth(),endX=wave.getX()+trimEnd*wave.getWidth();draggedTrimHandle=std::abs(event.position.x-x)<=std::abs(event.position.x-endX)?0:1;
        if(onTrimGestureChanged)onTrimGestureChanged(draggedTrimHandle==0?13:14,true);
    }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if(onTrimGestureChanged){onTrimGestureChanged(13,true);onTrimGestureChanged(14,true);}
        setTrimRange(0.0f,1.0f);if(onTrimRangeChanged)onTrimRangeChanged(trimStart,trimEnd);
        if(onTrimGestureChanged){onTrimGestureChanged(14,false);onTrimGestureChanged(13,false);}
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if(draggedTrimHandle<0)return;const auto wave=getLocalBounds().reduced(12,28).toFloat();const float normalized=juce::jlimit(0.0f,1.0f,(event.position.x-wave.getX())/juce::jmax(1.0f,wave.getWidth()));
        if(draggedTrimHandle==0)trimStart=juce::jmin(normalized,trimEnd-0.01f);else trimEnd=juce::jmax(normalized,trimStart+0.01f);repaint();if(onTrimRangeChanged)onTrimRangeChanged(trimStart,trimEnd);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if(draggedTrimHandle>=0&&onTrimGestureChanged)onTrimGestureChanged(draggedTrimHandle==0?13:14,false);
        draggedTrimHandle=-1;
    }
private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if(source==&magdrum::ui::languageChangeBroadcaster())updateTooltip();
        repaint();
    }
    void updateTooltip() { tooltipText=magdrum::ui::localisedText("Drag waveform markers to set the sample playback range. Double-click to reset."); }
    juce::AudioFormatManager formats;juce::AudioThumbnailCache cache{8};juce::AudioThumbnail thumbnail;juce::File currentFile;juce::String tooltipText;float trimStart=0.0f,trimEnd=1.0f;int draggedTrimHandle=-1;
};

class MagDrumSampleComparison final : public juce::Component
                                      , private juce::ChangeListener
{
public:
    using PreviewCallback = std::function<void(const juce::File&)>;
    using StopCallback = std::function<void()>;

    MagDrumSampleComparison(const juce::File& first,const juce::File& second, PreviewCallback preview,
                            StopCallback stop)
        : firstSample(first), secondSample(second), previewSample(std::move(preview)), stopSample(std::move(stop))
    {
        for(auto* label:{&leftTitle,&rightTitle}){label->setJustificationType(juce::Justification::centredLeft);label->setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(*label);}
        leftTitle.setText(first.getFileName(),juce::dontSendNotification);rightTitle.setText(second.getFileName(),juce::dontSendNotification);leftWave.setFile(first);rightWave.setFile(second);addAndMakeVisible(leftWave);addAndMakeVisible(rightWave);
        leftPreview.setButtonText(magdrum::ui::localisedText("Preview"));rightPreview.setButtonText(magdrum::ui::localisedText("Preview"));
        leftStop.setButtonText(magdrum::ui::localisedText("Stop"));rightStop.setButtonText(magdrum::ui::localisedText("Stop"));
        leftPreview.onClick=[this]{if(previewSample)previewSample(firstSample);};rightPreview.onClick=[this]{if(previewSample)previewSample(secondSample);};
        leftStop.onClick=[this]{if(stopSample)stopSample();};rightStop.onClick=[this]{if(stopSample)stopSample();};
        addAndMakeVisible(leftPreview);addAndMakeVisible(rightPreview);addAndMakeVisible(leftStop);addAndMakeVisible(rightStop);
        magdrum::ui::languageChangeBroadcaster().addChangeListener(this);
        applyLanguage();
    }
    ~MagDrumSampleComparison() override { magdrum::ui::languageChangeBroadcaster().removeChangeListener(this); }
    void resized() override
    {
        auto area=getLocalBounds().reduced(12);const int column=area.getWidth()/2;auto a=area.removeFromLeft(column).reduced(6);auto b=area.reduced(6);leftTitle.setBounds(a.removeFromTop(30));auto leftControls=a.removeFromTop(30);leftPreview.setBounds(leftControls.removeFromLeft(leftControls.getWidth()/2-3));leftControls.removeFromLeft(6);leftStop.setBounds(leftControls);a.removeFromTop(6);leftWave.setBounds(a);rightTitle.setBounds(b.removeFromTop(30));auto rightControls=b.removeFromTop(30);rightPreview.setBounds(rightControls.removeFromLeft(rightControls.getWidth()/2-3));rightControls.removeFromLeft(6);rightStop.setBounds(rightControls);b.removeFromTop(6);rightWave.setBounds(b);
    }
private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if(source==&magdrum::ui::languageChangeBroadcaster())applyLanguage();
    }
    void applyLanguage()
    {
        leftPreview.setButtonText(magdrum::ui::localisedText("Preview"));rightPreview.setButtonText(magdrum::ui::localisedText("Preview"));
        leftStop.setButtonText(magdrum::ui::localisedText("Stop"));rightStop.setButtonText(magdrum::ui::localisedText("Stop"));
        leftWave.repaint();rightWave.repaint();repaint();
    }
    juce::File firstSample,secondSample;PreviewCallback previewSample;StopCallback stopSample;juce::Label leftTitle,rightTitle;juce::TextButton leftPreview,rightPreview,leftStop,rightStop;MagDrumWaveform leftWave,rightWave;
};

class MagDrumSampleComparisonWindow final : public juce::DocumentWindow
{
public:
    MagDrumSampleComparisonWindow(const juce::File& first,const juce::File& second,
                                  MagDrumSampleComparison::PreviewCallback preview,
                                  MagDrumSampleComparison::StopCallback stop)
        :juce::DocumentWindow(magdrum::ui::localisedText("Compare drum samples"),Palette::Panel,juce::DocumentWindow::closeButton)
    {setUsingNativeTitleBar(true);setContentOwned(new MagDrumSampleComparison(first,second,std::move(preview),std::move(stop)),true);setResizable(true,true);centreWithSize(900,420);setVisible(true);}
    void closeButtonPressed() override {setVisible(false);}
};

class PluginSampleScanJob final : public juce::ThreadPoolJob
{
public:
    using Completion=std::function<void(const WavScanner::Stats&,std::int64_t,const juce::String&)>;
    using Progress=std::function<void(const juce::File&,int,int)>;
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
            auto examples=scanDatabase.getManualClassificationExamples();
            std::shared_ptr<const DrumClassificationModel> learnedModel;
            if(!examples.empty())learnedModel=std::make_shared<ManualExampleDrumClassificationModel>(std::move(examples));
            WavScanner scanner;
            (void)scanner.scan(rootFolder,[this]{return shouldExit()||(cancelFlag&&cancelFlag->load());},
                [&scanDatabase](const juce::File& file,std::int64_t size,std::int64_t modified){return scanDatabase.isSampleUnchanged(file,size,modified);},
                &stats,[&](const ScannedSample& sample){if(!scanDatabase.insertSample(sample,*sourceId))return false;++indexed;return true;},resumeAfter,
                [this,&scanDatabase,scanId](const juce::File& file){return scanDatabase.updateSampleScanCheckpoint(scanId,file);},std::move(learnedModel),
                [this](const juce::File& file,int completed,int total){if(onProgress)onProgress(file,completed,total);});
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

class PluginSampleBrowseJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(bool, SampleBrowsePage)>;

    PluginSampleBrowseJob(juce::File databaseFile, SampleBrowseFilter filter, int limit, int offset,
                          Completion completion, bool evenlySpacedRepresentative = false)
        : ThreadPoolJob("MagDrum VST sample library query"), dbPath(std::move(databaseFile)),
          queryFilter(std::move(filter)), pageLimit(limit), pageOffset(offset),
          representative(evenlySpacedRepresentative), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        SampleDatabase queryDatabase;
        const bool opened = queryDatabase.open(dbPath);
        auto page = opened ? (representative
            ? queryDatabase.searchSampleMapRecords(queryFilter, pageLimit)
            : queryDatabase.searchSampleBrowseRecords(queryFilter, pageLimit, pageOffset))
                           : SampleBrowsePage{};
        juce::MessageManager::callAsync([completion = std::move(onComplete), opened,
                                         page = std::move(page)]() mutable
        {
            if (completion)
                completion(opened, std::move(page));
        });
        return jobHasFinished;
    }

private:
    juce::File dbPath;
    SampleBrowseFilter queryFilter;
    int pageLimit = 250, pageOffset = 0;
    bool representative = false;
    Completion onComplete;
};

class PluginSampleLookupJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(int, bool, juce::Array<juce::File>,
                                          std::vector<PerceptualDuplicateMatch>)>;

    PluginSampleLookupJob(juce::File databaseFile, juce::File sampleFile, int lookupAction,
                          Completion completion)
        : ThreadPoolJob("MagDrum VST sample lookup"), dbPath(std::move(databaseFile)),
          selectedFile(std::move(sampleFile)), action(lookupAction), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        SampleDatabase queryDatabase;
        const bool opened = queryDatabase.open(dbPath);
        juce::Array<juce::File> files;
        std::vector<PerceptualDuplicateMatch> perceptualMatches;
        if (opened)
        {
            if (action == 2)
                files = queryDatabase.findSimilar(selectedFile, 20);
            else if (action == 3)
                files = queryDatabase.findExactDuplicates(selectedFile, 100);
            else if (action == 4)
                perceptualMatches = queryDatabase.findPerceptualDuplicates(selectedFile);
        }

        juce::MessageManager::callAsync([completion = std::move(onComplete), lookupAction = action,
                                         opened, files = std::move(files),
                                         perceptualMatches = std::move(perceptualMatches)]() mutable
        {
            if (completion)
                completion(lookupAction, opened, std::move(files), std::move(perceptualMatches));
        });
        return jobHasFinished;
    }

private:
    juce::File dbPath, selectedFile;
    int action = 0;
    Completion onComplete;
};

class PluginSamplePreviewJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(bool)>;

    PluginSamplePreviewJob(MagDrumAudioProcessor& audioProcessor, juce::File sampleFile,
                           const std::atomic<std::uint64_t>& currentGeneration,
                           std::uint64_t requestGeneration, Completion completion)
        : ThreadPoolJob("MagDrum VST sample preview"), processor(audioProcessor),
          file(std::move(sampleFile)), activeGeneration(currentGeneration),
          generation(requestGeneration), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        const auto cancelled = [this]
        {
            return shouldExit() || activeGeneration.load(std::memory_order_acquire) != generation;
        };
        if (cancelled()) return jobHasFinished;
        const bool previewed = processor.previewSample(file, cancelled);
        if (cancelled()) return jobHasFinished;
        juce::MessageManager::callAsync([completion = std::move(onComplete), previewed]() mutable
        {
            if (completion) completion(previewed);
        });
        return jobHasFinished;
    }

private:
    MagDrumAudioProcessor& processor;
    juce::File file;
    const std::atomic<std::uint64_t>& activeGeneration;
    std::uint64_t generation = 0;
    Completion onComplete;
};

class PluginSampleLoadJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(bool)>;

    PluginSampleLoadJob(MagDrumAudioProcessor& audioProcessor, juce::File sampleFile, int targetNote,
                        int minimum, int maximum, const std::atomic<bool>& cancellationFlag,
                        Completion completion)
        : ThreadPoolJob("MagDrum VST sample assignment"), processor(audioProcessor),
          file(std::move(sampleFile)), note(targetNote), minimumVelocity(minimum),
          maximumVelocity(maximum), cancelledFlag(cancellationFlag), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        const auto cancelled = [this]
        {
            return shouldExit() || cancelledFlag.load(std::memory_order_acquire);
        };
        if (cancelled()) return jobHasFinished;
        const bool loaded = processor.loadSampleForVelocityRange(note, minimumVelocity,
                                                                  maximumVelocity, file, cancelled);
        if (cancelled()) return jobHasFinished;
        juce::MessageManager::callAsync([completion = std::move(onComplete), loaded]() mutable
        {
            if (completion) completion(loaded);
        });
        return jobHasFinished;
    }

private:
    MagDrumAudioProcessor& processor;
    juce::File file;
    int note = 36, minimumVelocity = 1, maximumVelocity = 127;
    const std::atomic<bool>& cancelledFlag;
    Completion onComplete;
};

class PluginSampleCandidateReviewJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(int, bool, SampleBrowseRecord,
        std::vector<PluginVelocityLayerBoundary>,
        std::vector<PluginConfirmedVelocityLayerRange>,
        std::vector<PluginRoundRobinCandidate>,
        std::vector<PluginConfirmedRoundRobinGroup>)>;

    PluginSampleCandidateReviewJob(juce::File databaseFile, SampleBrowseRecord sampleRecord,
                                   int reviewAction, Completion completion)
        : ThreadPoolJob("MagDrum VST sample candidate review"), dbPath(std::move(databaseFile)),
          sample(std::move(sampleRecord)), action(reviewAction), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        SampleDatabase queryDatabase;
        const bool opened = queryDatabase.open(dbPath);
        std::vector<PluginVelocityLayerBoundary> layerCandidates;
        std::vector<PluginConfirmedVelocityLayerRange> confirmedLayers;
        std::vector<PluginRoundRobinCandidate> roundRobinCandidates;
        std::vector<PluginConfirmedRoundRobinGroup> confirmedGroups;
        if (opened && action == 10)
        {
            layerCandidates = queryDatabase.getPluginVelocityLayerCandidates(sample.pluginIdentifier, sample.kitName);
            confirmedLayers = queryDatabase.getConfirmedPluginVelocityLayers(sample.pluginIdentifier, sample.kitName);
        }
        else if (opened && action == 11)
        {
            roundRobinCandidates = queryDatabase.getPluginRoundRobinCandidates(sample.pluginIdentifier, sample.kitName);
            confirmedGroups = queryDatabase.getConfirmedPluginRoundRobinGroups(sample.pluginIdentifier, sample.kitName);
        }

        juce::MessageManager::callAsync(
            [completion = std::move(onComplete), reviewAction = action, opened,
             sampleRecord = std::move(sample), layerCandidates = std::move(layerCandidates),
             confirmedLayers = std::move(confirmedLayers),
             roundRobinCandidates = std::move(roundRobinCandidates),
             confirmedGroups = std::move(confirmedGroups)]() mutable
            {
                if (completion)
                    completion(reviewAction, opened, std::move(sampleRecord), std::move(layerCandidates),
                               std::move(confirmedLayers), std::move(roundRobinCandidates),
                               std::move(confirmedGroups));
            });
        return jobHasFinished;
    }

private:
    juce::File dbPath;
    SampleBrowseRecord sample;
    int action = 0;
    Completion onComplete;
};

class PluginMetadataFolderSyncJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(PortableMetadataSyncResult)>;
    PluginMetadataFolderSyncJob(juce::File databaseFile, juce::File folder, Completion completion)
        : ThreadPoolJob("MagDrum metadata folder sync"), dbPath(std::move(databaseFile)),
          syncFolder(std::move(folder)), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        SampleDatabase database;
        PortableMetadataSyncResult result;
        if (!database.open(dbPath))
            result.error = "Could not open the shared MagDrum sample database.";
        else
            result = database.syncPortableMetadataFolder(syncFolder);
        juce::MessageManager::callAsync([completion = std::move(onComplete), result = std::move(result)]() mutable
        {
            if (completion) completion(std::move(result));
        });
        return jobHasFinished;
    }

private:
    juce::File dbPath, syncFolder;
    Completion onComplete;
};

class PluginMetadataArchiveJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(bool, bool, bool, bool, juce::File,
                                          PortableMetadataMergeResult)>;

    PluginMetadataArchiveJob(juce::File databaseFile, juce::File archiveFile, bool isExport,
                             Completion completion)
        : ThreadPoolJob("MagDrum portable metadata archive"), dbPath(std::move(databaseFile)),
          file(std::move(archiveFile)), exporting(isExport), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        SampleDatabase archiveDatabase;
        const bool databaseOpened = archiveDatabase.open(dbPath);
        bool archiveValid = true;
        bool fileSaved = false;
        PortableMetadataMergeResult mergeResult;

        if (databaseOpened && exporting)
        {
            const auto json = juce::JSON::toString(archiveDatabase.exportPortableMetadata(), true);
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension(".json");
            fileSaved = file.replaceWithText(json);
        }
        else if (databaseOpened)
        {
            const auto archive = juce::JSON::parse(file.loadFileAsString());
            archiveValid = !archive.isVoid();
            if (archiveValid)
                mergeResult = archiveDatabase.mergePortableMetadata(archive);
        }

        juce::MessageManager::callAsync(
            [completion = std::move(onComplete), isExport = exporting, databaseOpened,
             archiveValid, fileSaved, archiveFile = std::move(file),
             result = std::move(mergeResult)]() mutable
            {
                if (completion)
                    completion(isExport, databaseOpened, archiveValid, fileSaved,
                               std::move(archiveFile), std::move(result));
            });
        return jobHasFinished;
    }

private:
    juce::File dbPath, file;
    bool exporting = false;
    Completion onComplete;
};

class VstPluginNoteScanJob final : public juce::ThreadPoolJob
{
public:
    VstPluginNoteScanJob(MagDrumAudioProcessorEditor& owner, juce::PluginDescription pluginDescription,
                         juce::MemoryBlock savedState, PluginNoteScanSettings scanSettings,
                         juce::File databaseFile, juce::String identifier, juce::String savedKit,
                         std::int64_t kit, std::atomic<bool>& cancel)
        : ThreadPoolJob("MagDrum VST isolated instrument scan"), editor(&owner), description(std::move(pluginDescription)),
          state(std::move(savedState)), settings(std::move(scanSettings)), dbPath(std::move(databaseFile)),
          pluginIdentifier(std::move(identifier)), kitName(std::move(savedKit)), kitId(kit), cancellation(cancel) {}

    JobStatus runJob() override
    {
        SampleDatabase db;
        if (! db.open(dbPath)) { finish(0, 0, 0, 1, false, "Could not open the shared MagDrum sample database.", {}); return jobHasFinished; }
        juce::String pluginName = description.name;
        const auto sourceId = db.ensureSampleSource("plugin", pluginIdentifier, pluginName);
        if (! sourceId) { finish(0, 0, 0, 1, false, "Could not create the plugin source record.", {}); return jobHasFinished; }

        const auto previewRoot = getMagDrumDataDirectory().getChildFile("plugin-previews")
            .getChildFile(juce::String(pluginIdentifier.hashCode64())).getChildFile(juce::String(static_cast<juce::int64>(kitId)));
        if (! previewRoot.isDirectory() && ! previewRoot.createDirectory().wasOk())
        { finish(0, 0, 0, 1, false, "Could not create the plugin preview folder.", {}); return jobHasFinished; }

        const auto requestDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrum")
            .getChildFile("vst-plugin-scan-" + juce::Uuid().toString());
        const auto outputDir = requestDir.getChildFile("captures");
        if (! requestDir.createDirectory().wasOk() || ! outputDir.createDirectory().wasOk())
        { finish(0, 0, 0, 1, false, "Could not prepare isolated scan files.", {}); return jobHasFinished; }

        const auto descriptionFile = requestDir.getChildFile("plugin.xml");
        const auto stateFile = requestDir.getChildFile("kit-state.bin");
        const auto xml = description.createXml();
        if (xml == nullptr || ! xml->writeTo(descriptionFile) || ! stateFile.replaceWithData(state.getData(), state.getSize()))
        { requestDir.deleteRecursively(); finish(0, 0, 0, 1, false, "Could not prepare the saved instrument state.", {}); return jobHasFinished; }

        const auto cancelFile = requestDir.getChildFile("cancel"), progressFile = requestDir.getChildFile("progress.json");
        const auto captureLog = requestDir.getChildFile("captures.jsonl"), resultFile = requestDir.getChildFile("result.json");
        const auto requestFile = requestDir.getChildFile("request.json");
        auto* request = new juce::DynamicObject();
        request->setProperty("descriptionFile", descriptionFile.getFullPathName());
        request->setProperty("stateFile", stateFile.getFullPathName());
        request->setProperty("outputDirectory", outputDir.getFullPathName());
        request->setProperty("cancellationFile", cancelFile.getFullPathName());
        request->setProperty("progressFile", progressFile.getFullPathName());
        request->setProperty("captureLogFile", captureLog.getFullPathName());
        request->setProperty("resultFile", resultFile.getFullPathName());
        request->setProperty("sampleRate", settings.sampleRate);
        request->setProperty("blockSize", settings.blockSize);
        request->setProperty("repeatCount", settings.repeatCount);
        request->setProperty("noteDurationSeconds", settings.noteDurationSeconds);
        request->setProperty("releaseSeconds", settings.releaseSeconds);
        request->setProperty("captureLengthSeconds", settings.captureLengthSeconds);
        request->setProperty("realtimePaced", settings.renderMode == PluginNoteRenderMode::realtimePaced);
        request->setProperty("audioDeviceRealtime", settings.renderMode == PluginNoteRenderMode::audioDevice);
        juce::Array<juce::var> velocities;
        for (const auto velocity : settings.velocityLevels) velocities.add(velocity);
        request->setProperty("velocityLevels", velocities);
        if (! requestFile.replaceWithText(juce::JSON::toString(juce::var(request), true)))
        { requestDir.deleteRecursively(); finish(0, 0, 0, 1, false, "Could not write the isolated scan request.", {}); return jobHasFinished; }

        const auto helper = bundledScannerApplication();
        if (! helper.existsAsFile())
        { requestDir.deleteRecursively(); finish(0, 0, 0, 1, false, "The bundled MagDrum scanner was not found beside the VST3.", {}); return jobHasFinished; }
        juce::StringArray command{helper.getFullPathName(), "--magdrum-plugin-scan-worker", requestFile.getFullPathName()};
        const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(editor);
        const auto execution = PluginScanProcess::monitorWorker(command, progressFile, resultFile, cancelFile,
            [this] { return shouldExit() || cancellation.load(); },
            [safe](int done, int total, int note, int velocity)
            {
                juce::MessageManager::callAsync([safe, done, total, note, velocity]
                { if (safe != nullptr) safe->updatePluginScanProgress(done, total, note, velocity); });
            }, 10000, captureLog);
        if (! execution.started || ! execution.manifestRead)
        {
            const auto message = execution.manifest.error.isNotEmpty() ? execution.manifest.error
                : execution.terminatedForCancellation ? juce::String("Scan cancelled after the instrument stopped responding.")
                : juce::String("The isolated instrument scanner exited without a complete result.");
            requestDir.deleteRecursively();
            finish(0, 0, 0, execution.terminatedForCancellation ? 0 : 1, execution.terminatedForCancellation, message, {});
            return jobHasFinished;
        }

        const auto& captures = execution.manifest.captures;
        for (const auto& capture : captures)
            if (! capture.silent)
            {
                const auto source = outputDir.getChildFile(capture.fileName);
                const auto destination = previewRoot.getChildFile(capture.fileName);
                if (destination.existsAsFile()) destination.deleteFile();
                if (source.existsAsFile()) (void) source.copyFileTo(destination);
            }

        auto examples=db.getManualClassificationExamples();
        std::shared_ptr<const DrumClassificationModel> learnedModel;
        if(!examples.empty())learnedModel=std::make_shared<ManualExampleDrumClassificationModel>(std::move(examples));
        const auto analyzed = WavScanner{}.scan(outputDir, {}, {}, nullptr, {}, {}, {}, std::move(learnedModel));
        std::unordered_map<std::string, const ScannedSample*> byPath;
        for (const auto& sample : analyzed) byPath.emplace(sample.file.getFullPathName().toStdString(), &sample);
        int saved = 0, sounding = 0, silent = 0, failures = 0;
        std::vector<PluginSoundRecord> sounds;
        std::vector<PluginRoundRobinTake> takes;
        for (const auto& capture : captures)
        {
            PluginSoundRecord sound;
            sound.midiNote = capture.midiNote; sound.velocity = capture.velocity; sound.silent = capture.silent;
            sound.duration = static_cast<double>(capture.sampleCount) / juce::jmax(1.0, capture.sampleRate);
            sound.sampleRate = capture.sampleRate; sound.channels = capture.channels; sound.peak = capture.peak; sound.rms = capture.rms;
            PluginRoundRobinTake take;
            take.midiNote = capture.midiNote; take.velocity = capture.velocity; take.takeIndex = capture.takeIndex; take.silent = capture.silent;
            if (capture.silent) ++silent;
            else
            {
                ++sounding;
                const auto temporary = outputDir.getChildFile(capture.fileName);
                const auto permanent = previewRoot.getChildFile(capture.fileName);
                sound.previewPath = permanent.getFullPathName(); take.previewPath = sound.previewPath;
                const auto found = byPath.find(temporary.getFullPathName().toStdString());
                if (found != byPath.end())
                {
                    auto sample = *found->second; sample.file = permanent; sample.fileSizeBytes = permanent.getSize();
                    sample.modifiedTimeMillis = permanent.getLastModificationTime().toMilliseconds();
                    sound.duration = sample.durationSeconds; sound.sampleRate = sample.sampleRate; sound.channels = sample.channels;
                    sound.peak = sample.peak; sound.rms = sample.rms; sound.spectralCentroid = sample.spectralCentroidHz;
                    sound.spectralFlatness = sample.spectralFlatness; sound.transientStrength = sample.transientStrength;
                    sound.transientCount = sample.transientCount; sound.fundamentalHz = sample.fundamentalHz;
                    sound.pitchConfidence = sample.pitchConfidence; sound.fingerprint = sample.fingerprint;
                    sound.acousticFingerprint = sample.acousticFingerprint; sound.category = sample.category;
                    sound.confidence = sample.classificationConfidence; take.acousticFingerprint = sample.acousticFingerprint;
                    if (! db.insertSample(sample, *sourceId)) ++failures;
                }
            }
            takes.push_back(take);
            if (capture.takeIndex == 1)
            {
                if (db.upsertPluginSound(kitId, sound)) { ++saved; sounds.push_back(sound); }
                else ++failures;
            }
        }

        const auto layers = PluginVelocityLayerAnalyzer{}.analyze(sounds);
        const auto roundRobins = PluginRoundRobinAnalyzer{}.analyze(takes);
        if (! db.replacePluginVelocityLayerCandidates(kitId, layers)) ++failures;
        if (! db.replacePluginRoundRobinData(kitId, takes, roundRobins)) ++failures;
        juce::String summary = magdrum::ui::localisedText(
            "{layers} velocity-layer candidates · {roundRobins} round-robin candidates")
            .replace("{layers}", juce::String(layers.size()))
            .replace("{roundRobins}", juce::String(static_cast<int>(roundRobins.size())));
        requestDir.deleteRecursively();
        finish(saved, sounding, silent, failures, execution.manifest.cancelled, execution.manifest.error, summary);
        return jobHasFinished;
    }

private:
    void finish(int saved, int sounding, int silent, int failures, bool cancelled, const juce::String& error, const juce::String& summary)
    {
        const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(editor);
        juce::MessageManager::callAsync([safe, saved, sounding, silent, failures, cancelled, error, summary]
        { if (safe != nullptr) safe->finishPluginNoteScan(saved, sounding, silent, failures, cancelled, error, summary); });
    }
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> editor;
    juce::PluginDescription description;
    juce::MemoryBlock state;
    PluginNoteScanSettings settings;
    juce::File dbPath;
    juce::String pluginIdentifier, kitName;
    std::int64_t kitId;
    std::atomic<bool>& cancellation;
};

class PluginInventoryScanJob final : public juce::ThreadPoolJob
{
public:
    PluginInventoryScanJob(MagDrumAudioProcessorEditor& owner, std::shared_ptr<std::atomic<bool>> cancel,
                           juce::File progress, juce::File folder)
        : ThreadPoolJob("MagDrum isolated VST3 discovery"), editor(&owner), cancellation(std::move(cancel)),
          progressFile(std::move(progress)), additionalFolder(std::move(folder)) {}
    JobStatus runJob() override
    {
        const auto helper = bundledScannerApplication();
        if (! helper.existsAsFile()) { finish(-1, false); return jobHasFinished; }
        constexpr int maximumPluginCrashRecoveries = 64;
        int recoveredCrashes = 0;
        while (! shouldExit() && ! cancellation->load())
        {
            juce::ChildProcess child;
            juce::StringArray command{helper.getFullPathName(), "--magdrum-vst3-discovery-worker",
                                      progressFile.getFullPathName()};
            if (additionalFolder.isDirectory()) command.add(additionalFolder.getFullPathName());
            if (! child.start(command, 0))
            { finish(-1, false); return jobHasFinished; }
            while (child.isRunning() && ! shouldExit() && ! cancellation->load())
                child.waitForProcessToFinish(100);
            const bool cancelled = shouldExit() || cancellation->load();
            if (cancelled && child.isRunning()) { child.kill(); child.waitForProcessToFinish(3000); }
            if (cancelled)
            { finish(-1, true); return jobHasFinished; }

            const int exitCode = static_cast<int>(child.getExitCode());
            if (exitCode == 0)
            { finish(0, false); return jobHasFinished; }

            // Some VST3 modules crash MagDrum itself before JUCE can update its
            // dead-man's-pedal file. Persist the module path from the last
            // in-flight progress record and continue with the remaining modules.
            if (exitCode < 0 && recoveredCrashes < maximumPluginCrashRecoveries)
            {
                magdrum::plugins::Vst3DiscoveryProgress progress;
                if (magdrum::plugins::readVst3DiscoveryProgress(progressFile, progress)
                    && !progress.currentPath.isEmpty()
                    && magdrum::plugins::recordVst3DiscoveryModuleFailure(progressFile,
                        progress.currentPath))
                {
                    ++recoveredCrashes;
                    continue;
                }
            }

            finish(exitCode, false);
            return jobHasFinished;
        }
        finish(-1, true);
        return jobHasFinished;
    }
private:
    void finish(int exitCode, bool cancelled)
    {
        magdrum::plugins::Vst3DiscoveryProgress progress;
        const int skippedAfterCrash = magdrum::plugins::readVst3DiscoveryProgress(progressFile, progress)
            ? progress.failedModules.size() : 0;
        const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(editor);
        juce::MessageManager::callAsync([safe, exitCode, cancelled, skippedAfterCrash]
        { if (safe != nullptr) safe->finishPluginInventoryScan(exitCode, cancelled, skippedAfterCrash); });
    }
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> editor;
    std::shared_ptr<std::atomic<bool>> cancellation;
    juce::File progressFile;
    juce::File additionalFolder;
};

class PluginPresetImportJob final : public juce::ThreadPoolJob
{
public:
    PluginPresetImportJob(MagDrumAudioProcessorEditor& owner, juce::PluginDescription plugin,
                          juce::File preset, juce::String identifier, juce::String name,
                          std::shared_ptr<std::atomic<bool>> cancel, int programIndex = -1)
        : ThreadPoolJob("MagDrum isolated VST3 preset import"), editor(&owner),
          description(std::move(plugin)), presetFile(std::move(preset)),
          pluginIdentifier(std::move(identifier)), kitName(std::move(name)),
          cancellation(std::move(cancel)), selectedProgramIndex(programIndex) {}

    JobStatus runJob() override
    {
        const auto helper = bundledScannerApplication();
        if (! helper.existsAsFile())
        {
            finish(-1, false, {}, magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3."));
            return jobHasFinished;
        }

        const auto work = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("MagDrum").getChildFile("preset-import-" + juce::Uuid().toString());
        if (! work.createDirectory().wasOk())
        {
            finish(-1, false, {}, magdrum::ui::localisedText("Could not prepare the isolated preset import."));
            return jobHasFinished;
        }
        const auto descriptionFile = work.getChildFile("plugin.xml");
        const auto stateFile = work.getChildFile("kit-state.bin");
        const auto resultFile = work.getChildFile("result.json");
        const auto requestFile = work.getChildFile("request.json");
        const auto xml = description.createXml();
        auto* request = new juce::DynamicObject();
        request->setProperty("descriptionFile", descriptionFile.getFullPathName());
        request->setProperty("operation", selectedProgramIndex >= 0 ? "loadProgram" : "loadPreset");
        if (selectedProgramIndex >= 0)
            request->setProperty("programIndex", selectedProgramIndex);
        else
            request->setProperty("presetFile", presetFile.getFullPathName());
        request->setProperty("stateFile", stateFile.getFullPathName());
        request->setProperty("resultFile", resultFile.getFullPathName());
        if (xml == nullptr || ! xml->writeTo(descriptionFile)
            || ! requestFile.replaceWithText(juce::JSON::toString(juce::var(request), true)))
        {
            work.deleteRecursively();
            finish(-1, false, {}, magdrum::ui::localisedText("Could not prepare the isolated preset import."));
            return jobHasFinished;
        }

        juce::StringArray command;
        command.add(helper.getFullPathName());
        command.add("--magdrum-vst3-preset-worker");
        command.add(requestFile.getFullPathName());
        juce::ChildProcess child;
        if (! child.start(command, 0))
        {
            work.deleteRecursively();
            finish(-1, false, {}, magdrum::ui::localisedText("The isolated MagDrum preset helper could not start."));
            return jobHasFinished;
        }

        while (child.isRunning() && ! shouldExit() && ! cancellation->load())
            child.waitForProcessToFinish(100);
        const bool cancelled = shouldExit() || cancellation->load();
        if (cancelled && child.isRunning())
        {
            child.kill();
            child.waitForProcessToFinish(3000);
        }

        const int exitCode = cancelled ? -1 : static_cast<int>(child.getExitCode());
        juce::MemoryBlock state;
        juce::String message;
        if (exitCode == 0)
        {
            const auto resultValue = juce::JSON::parse(resultFile.loadFileAsString());
            if (resultValue.isObject())
                message = resultValue.getDynamicObject()->getProperty("message").toString();
            if (message.isEmpty() && (! stateFile.loadFileAsData(state) || state.isEmpty()))
                message = magdrum::ui::localisedText("The isolated helper did not return a saved preset state.");
            if (message.isNotEmpty())
                state.reset();
        }
        else if (resultFile.existsAsFile())
        {
            const auto resultValue = juce::JSON::parse(resultFile.loadFileAsString());
            if (resultValue.isObject())
                message = resultValue.getDynamicObject()->getProperty("message").toString();
        }
        work.deleteRecursively();
        finish(exitCode, cancelled, std::move(state), message);
        return jobHasFinished;
    }

private:
    void finish(int exitCode, bool cancelled, juce::MemoryBlock state, juce::String message)
    {
        const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(editor);
        juce::MessageManager::callAsync([safe, exitCode, cancelled, state = std::move(state),
                                         message = std::move(message), identifier = pluginIdentifier,
                                         name = kitName]() mutable
        {
            if (safe != nullptr)
                safe->finishPluginPresetImport(exitCode, cancelled, std::move(state), message, identifier, name);
        });
    }

    juce::Component::SafePointer<MagDrumAudioProcessorEditor> editor;
    juce::PluginDescription description;
    juce::File presetFile;
    juce::String pluginIdentifier, kitName;
    std::shared_ptr<std::atomic<bool>> cancellation;
    int selectedProgramIndex = -1;
};

class PluginProgramDiscoveryJob final : public juce::ThreadPoolJob
{
public:
    PluginProgramDiscoveryJob(MagDrumAudioProcessorEditor& owner, juce::PluginDescription plugin,
                              std::shared_ptr<std::atomic<bool>> cancel)
        : ThreadPoolJob("MagDrum isolated VST3 program discovery"), editor(&owner),
          description(std::move(plugin)), cancellation(std::move(cancel)) {}

    JobStatus runJob() override
    {
        const auto helper = bundledScannerApplication();
        if (! helper.existsAsFile())
        {
            finish(-1, false, {}, magdrum::ui::localisedText("The isolated MagDrum preset helper could not start."));
            return jobHasFinished;
        }
        const auto work = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("MagDrum").getChildFile("program-discovery-" + juce::Uuid().toString());
        if (! work.createDirectory().wasOk())
        {
            finish(-1, false, {}, magdrum::ui::localisedText("Could not prepare the isolated preset import."));
            return jobHasFinished;
        }
        const auto descriptionFile = work.getChildFile("plugin.xml");
        const auto stateFile = work.getChildFile("unused-state.bin");
        const auto resultFile = work.getChildFile("result.json");
        const auto requestFile = work.getChildFile("request.json");
        const auto xml = description.createXml();
        auto* request = new juce::DynamicObject();
        request->setProperty("descriptionFile", descriptionFile.getFullPathName());
        request->setProperty("operation", "enumeratePrograms");
        request->setProperty("stateFile", stateFile.getFullPathName());
        request->setProperty("resultFile", resultFile.getFullPathName());
        if (xml == nullptr || ! xml->writeTo(descriptionFile)
            || ! requestFile.replaceWithText(juce::JSON::toString(juce::var(request), true)))
        {
            work.deleteRecursively();
            finish(-1, false, {}, magdrum::ui::localisedText("Could not prepare the isolated preset import."));
            return jobHasFinished;
        }

        juce::ChildProcess child;
        if (! child.start({helper.getFullPathName(), "--magdrum-vst3-preset-worker", requestFile.getFullPathName()}, 0))
        {
            work.deleteRecursively();
            finish(-1, false, {}, magdrum::ui::localisedText("The isolated MagDrum preset helper could not start."));
            return jobHasFinished;
        }
        while (child.isRunning() && ! shouldExit() && ! cancellation->load())
            child.waitForProcessToFinish(100);
        const bool cancelled = shouldExit() || cancellation->load();
        if (cancelled && child.isRunning())
        {
            child.kill();
            child.waitForProcessToFinish(3000);
        }

        juce::StringArray programNames;
        juce::String message;
        const int exitCode = cancelled ? -1 : static_cast<int>(child.getExitCode());
        if (! cancelled && resultFile.existsAsFile())
        {
            const auto resultValue = juce::JSON::parse(resultFile.loadFileAsString());
            if (resultValue.isObject())
            {
                const auto* result = resultValue.getDynamicObject();
                message = result->getProperty("message").toString();
                const auto value = result->getProperty("programs");
                if (const auto* programs = value.getArray())
                    for (const auto& program : *programs)
                        if (program.isObject())
                            programNames.add(program.getDynamicObject()->getProperty("name").toString());
            }
        }
        work.deleteRecursively();
        finish(exitCode, cancelled, std::move(programNames), message);
        return jobHasFinished;
    }

private:
    void finish(int exitCode, bool cancelled, juce::StringArray programs, juce::String message)
    {
        const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(editor);
        juce::MessageManager::callAsync([safe, exitCode, cancelled, programs = std::move(programs),
                                         message = std::move(message)]() mutable
        {
            if (safe != nullptr)
                safe->finishPluginProgramDiscovery(exitCode, cancelled, std::move(programs), message);
        });
    }

    juce::Component::SafePointer<MagDrumAudioProcessorEditor> editor;
    juce::PluginDescription description;
    std::shared_ptr<std::atomic<bool>> cancellation;
};

class PluginMidiPatternDiscoveryJob final : public juce::ThreadPoolJob
{
public:
    using Completion = std::function<void(std::vector<juce::File>, bool)>;
    PluginMidiPatternDiscoveryJob(std::vector<juce::File> roots,
                                  std::shared_ptr<std::atomic<bool>> cancellation,
                                  Completion completion)
        : ThreadPoolJob("MagDrum VST3 MIDI pattern discovery"), searchRoots(std::move(roots)),
          cancelFlag(std::move(cancellation)), onComplete(std::move(completion)) {}

    JobStatus runJob() override
    {
        auto matches=magdrum::library::findDrumMidiPatternFiles(searchRoots,200,500000,[this]
        {
            return shouldExit() || (cancelFlag && cancelFlag->load(std::memory_order_acquire));
        });
        const bool cancelled = shouldExit() || (cancelFlag && cancelFlag->load(std::memory_order_acquire));
        if (onComplete)
            onComplete(std::move(matches), cancelled);
        return jobHasFinished;
    }

private:
    std::vector<juce::File> searchRoots;
    std::shared_ptr<std::atomic<bool>> cancelFlag;
    Completion onComplete;
};

MagDrumAudioProcessorEditor::MagDrumAudioProcessorEditor(MagDrumAudioProcessor& p, juce::File databaseFileOverride)
    : AudioProcessorEditor(p), tooltipWindow(this, 500), processor(p), indexedList("Samples",this), sampleMap(std::make_unique<MagDrumSampleMap>()), waveform(std::make_unique<MagDrumWaveform>())
{
    activePadParameterGestureNotes.fill(-1);
    magdrum::ui::initialiseMagDrumLocalization();magdrum::ui::languageChangeBroadcaster().addChangeListener(this);processor.addSampleStateListener(this);setLookAndFeel(&theme);setSize(1120,760);setResizable(true,true);setResizeLimits(1000,620,1800,1200);
    padActivityTimer.onTick=[this]{updatePadActivity();};padActivityTimer.start();
    languageButton.setTooltip(magdrum::ui::localisedText("Menu"));languageButton.onClick=[this]{magdrum::ui::showApplicationMenu(languageButton);};addAndMakeVisible(languageButton);
    libraryTab.setButtonText(magdrum::ui::localisedText("SAMPLE LIBRARY"));machineTab.setButtonText(magdrum::ui::localisedText("DRUM MACHINE"));sequencerTab.setButtonText(magdrum::ui::localisedText("SEQUENCER"));mapsTab.setButtonText(magdrum::ui::localisedText("MIDI MAPS"));pluginScannerTab.setButtonText(magdrum::ui::localisedText("PLUGIN SCANNER"));
    for(auto* tab:{&libraryTab,&machineTab,&sequencerTab,&mapsTab,&pluginScannerTab}){tab->addListener(this);addAndMakeVisible(*tab);}
    pluginScannerTab.setVisible(hasPluginScannerPage);
    sampleSearch.setTextToShowWhenEmpty(magdrum::ui::localisedText("Search indexed samples"),Palette::Muted);sampleSearch.setIndents(8,0);sampleSearch.setJustification(juce::Justification::centredLeft);sampleSearch.onReturnKey=[this]{refreshSamples();};addAndMakeVisible(sampleSearch);
    categoryFilter.addItem(magdrum::ui::localisedText("All sounds"),1);categoryFilter.addItem(magdrum::ui::localisedText("Kick"),2);categoryFilter.addItem(magdrum::ui::localisedText("Snare"),3);categoryFilter.addItem(magdrum::ui::localisedText("Closed hi-hat"),4);categoryFilter.addItem(magdrum::ui::localisedText("Open hi-hat"),5);categoryFilter.addItem(magdrum::ui::localisedText("Pedal hi-hat"),6);categoryFilter.addItem(magdrum::ui::localisedText("Tom"),7);categoryFilter.addItem(magdrum::ui::localisedText("Ride"),8);categoryFilter.addItem(magdrum::ui::localisedText("Crash"),9);categoryFilter.addItem(magdrum::ui::localisedText("Clap"),10);categoryFilter.addItem(magdrum::ui::localisedText("Percussion"),11);categoryFilter.addItem(magdrum::ui::localisedText("Unknown"),12);categoryFilter.setSelectedId(1);addAndMakeVisible(categoryFilter);
    contentTypeFilter.addItem(magdrum::ui::localisedText("All sounds"),1);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Drum one-shots"),2);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Drum loops & beats"),3);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Bass one-shots"),4);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Bass loops & beats"),5);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Synth one-shots"),6);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Synth loops"),7);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Other loops"),8);
    contentTypeFilter.setSelectedId(2);
    addAndMakeVisible(contentTypeFilter);
    const char* features[]{"All sound features","Very short","Short","Medium","Long","Dark","Mid brightness","Bright"};for(int i=0;i<8;++i)featureFilter.addItem(magdrum::ui::localisedText(features[i]),i+1);featureFilter.setSelectedId(1);addAndMakeVisible(featureFilter);
    const char* sorts[]{"Name A-Z","Duration shortest","Duration longest","Brightest first","Darkest first"};for(int i=0;i<5;++i)sortFilter.addItem(magdrum::ui::localisedText(sorts[i]),i+1);sortFilter.setSelectedId(1);addAndMakeVisible(sortFilter);
    confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review"),2);confidenceFilter.setSelectedId(1);addAndMakeVisible(confidenceFilter);
    sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.setSelectedId(1);addAndMakeVisible(sourceFilter);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);kitFilter.setSelectedId(1);addAndMakeVisible(kitFilter);
    auto refreshOnChange=[this]{refreshSamples();};categoryFilter.onChange=refreshOnChange;contentTypeFilter.onChange=refreshOnChange;featureFilter.onChange=refreshOnChange;sortFilter.onChange=refreshOnChange;confidenceFilter.onChange=refreshOnChange;sourceFilter.onChange=refreshOnChange;kitFilter.onChange=refreshOnChange;
    scanButton.setButtonText(magdrum::ui::localisedText("Add sample folder"));scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Scan installed libraries"));instrumentScannerButton.setButtonText(magdrum::ui::localisedText("Plugin instruments"));openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));openSourceKitButton.setTooltip(magdrum::ui::localisedText("Open the selected sample's source instrument and recall its saved kit in a separate MagDrum window."));openSourceKitButton.setEnabled(false);searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));stopPreviewButton.setButtonText(magdrum::ui::localisedText("Stop"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));
    scanButton.addListener(this);scanDefaultsButton.addListener(this);instrumentScannerButton.addListener(this);openSourceKitButton.addListener(this);searchButton.addListener(this);previewButton.addListener(this);stopPreviewButton.addListener(this);assignButton.addListener(this);libraryToolsButton.addListener(this);moreSamplesButton.addListener(this);addAndMakeVisible(scanButton);addAndMakeVisible(scanDefaultsButton);addAndMakeVisible(instrumentScannerButton);addAndMakeVisible(openSourceKitButton);addAndMakeVisible(searchButton);addAndMakeVisible(previewButton);addAndMakeVisible(stopPreviewButton);addAndMakeVisible(assignButton);addAndMakeVisible(libraryToolsButton);addAndMakeVisible(moreSamplesButton);
    refreshPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Refresh instruments"));
    scanPluginInventoryButton.setButtonText(magdrum::ui::localisedText(pluginInventoryScanRunning ? "Cancel instrument scan" : "Scan plugin folders"));
    choosePluginFolderButton.setButtonText(magdrum::ui::localisedText("Choose VST3 folder"));
    openPluginManagerButton.setButtonText(magdrum::ui::localisedText("Open instrument manager"));
    scanPluginNotesButton.setButtonText(magdrum::ui::localisedText("Scan kit notes"));
    cancelPluginNotesButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));
    pluginVelocities.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity levels"),Palette::Muted);
    pluginNoteDuration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Note seconds"),Palette::Muted);
    pluginRelease.setTextToShowWhenEmpty(magdrum::ui::localisedText("Release seconds"),Palette::Muted);
    pluginCaptureLength.setTextToShowWhenEmpty(magdrum::ui::localisedText("Capture seconds"),Palette::Muted);
    pluginRepeatCount.setTextToShowWhenEmpty(magdrum::ui::localisedText("Repeats"),Palette::Muted);
    pluginVelocityLabel.setText(magdrum::ui::localisedText("Velocity levels"),juce::dontSendNotification);pluginNoteDurationLabel.setText(magdrum::ui::localisedText("Note seconds"),juce::dontSendNotification);pluginReleaseLabel.setText(magdrum::ui::localisedText("Release seconds"),juce::dontSendNotification);pluginCaptureLabel.setText(magdrum::ui::localisedText("Capture seconds"),juce::dontSendNotification);pluginRepeatLabel.setText(magdrum::ui::localisedText("Repeats"),juce::dontSendNotification);
    const int renderMode=pluginRenderMode.getSelectedId();pluginRenderMode.clear(juce::dontSendNotification);pluginRenderMode.addItem(magdrum::ui::localisedText("Offline"),1);pluginRenderMode.addItem(magdrum::ui::localisedText("Real-time paced"),2);pluginRenderMode.addItem(magdrum::ui::localisedText("Audio device"),3);pluginRenderMode.setSelectedId(renderMode>0?renderMode:1,juce::dontSendNotification);
    instrumentScannerButton.setTooltip(magdrum::ui::localisedText("Open MagDrum's isolated instrument browser to load VST instruments, choose kits and capture MIDI notes."));
    scanDefaultsButton.setTooltip(magdrum::ui::localisedText("Scan audio files in common Native Instruments, Spectrasonics and Steinberg library locations."));
    indexedList.setModel(this);indexedList.setRowHeight(43);indexedList.setMultipleSelectionEnabled(true);addAndMakeVisible(indexedList);
    sampleMap->onSelect=[this](int index) { selectSampleMapSample(index); };
    sampleMap->onPreview=[this](int index)
    {
        if (juce::isPositiveAndBelow(index, sampleMapSamples.size()))
            previewSampleAsync(sampleMapSamples.getReference(index).file);
    };
    sampleMap->onStopPreview=[this]{stopSamplePreview();};addAndMakeVisible(*sampleMap);
    sampleScanProgress.setTextToDisplay(juce::String{});sampleScanProgress.setPercentageDisplay(true);sampleScanProgress.setColour(juce::ProgressBar::foregroundColourId,Palette::Accent);sampleScanProgress.setColour(juce::ProgressBar::backgroundColourId,Palette::Panel);addAndMakeVisible(sampleScanProgress);
    sampleScanProgressLabel.setJustificationType(juce::Justification::centredLeft);sampleScanProgressLabel.setColour(juce::Label::textColourId,Palette::Muted);sampleScanProgressLabel.setFont(juce::Font(juce::FontOptions(11.0f)));addAndMakeVisible(sampleScanProgressLabel);
    for(size_t i=0;i<pads.size();++i){auto& pad=pads[i];pad.setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));pad.setColour(juce::TextButton::buttonColourId,Palette::Surface);pad.setColour(juce::TextButton::buttonOnColourId,categoryColour(padNames[i]));pad.setPadGlowColour(categoryColour(padNames[i]));pad.setClickingTogglesState(false);pad.addListener(this);addAndMakeVisible(pad);}
    selectedPadTitle.setFont(juce::Font(juce::FontOptions(20.0f,juce::Font::bold)));selectedPadTitle.setColour(juce::Label::textColourId,Palette::Accent);addAndMakeVisible(selectedPadTitle);
    chokeGroupLabel.setJustificationType(juce::Justification::centredRight);
    chokeGroupLabel.setColour(juce::Label::textColourId, Palette::Muted);
    chokeGroupLabel.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    addAndMakeVisible(chokeGroupLabel);
    chokeGroupSelect.setTooltip(magdrum::ui::localisedText(
        "Pads in the same choke group stop one another when triggered."));
    addAndMakeVisible(chokeGroupSelect);
    manageLayersButton.setTooltip(magdrum::ui::localisedText(
        "Manage velocity layers for the selected pad."));
    manageLayersButton.addListener(this);
    addAndMakeVisible(manageLayersButton);
    chokeGroupSelect.onChange = [this]
    {
        if (updatingPadControls || chokeGroupSelect.getSelectedId() < 1) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.chokeGroup = chokeGroupSelect.getSelectedId() - 1;
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    layerSummary.setColour(juce::Label::textColourId,Palette::Text);layerSummary.setJustificationType(juce::Justification::topLeft);layerSummary.setMinimumHorizontalScale(0.8f);layerSummary.setTooltip(magdrum::ui::localisedText("Samples assigned to the same velocity range play in round-robin order."));addAndMakeVisible(layerSummary);
    sampleDetails.setColour(juce::Label::textColourId,Palette::Muted);sampleDetails.setJustificationType(juce::Justification::topLeft);sampleDetails.setMinimumHorizontalScale(0.8f);addAndMakeVisible(sampleDetails);
    const auto configurePadControl = [this](juce::Slider& slider, juce::Label& label, const juce::String& name,
                                            double minimum, double maximum, double interval, const juce::String& suffix)
    {
        label.setText(magdrum::ui::localisedText(name), juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, Palette::Muted);
        label.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 82, 20);
        slider.setRange(minimum, maximum, interval);
        slider.setTextValueSuffix(suffix);
        slider.setNumDecimalPlacesToDisplay(1);
        slider.setScrollWheelEnabled(false);
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
    };
    configurePadControl(gainSlider, gainLabel, "PAD GAIN", -60.0, 6.0, 0.1, " dB");
    configurePadControl(panSlider, panLabel, "PAD PAN", -1.0, 1.0, 0.01, {});
    configurePadControl(tuneSlider, tuneLabel, "PAD TUNE", -24.0, 24.0, 0.1, " st");
    configurePadControl(driveSlider, driveLabel, "PAD DRIVE", 0.0, 24.0, 0.1, " dB");
    configurePadControl(attackSlider, attackLabel, "PAD ATTACK", 0.0, 1000.0, 1.0, " ms");
    configurePadControl(decaySlider, decayLabel, "PAD DECAY", 0.0, 5000.0, 1.0, " ms");
    configurePadControl(sustainSlider, sustainLabel, "PAD SUSTAIN", 0.0, 100.0, 1.0, "%");
    configurePadControl(releaseSlider, releaseLabel, "PAD RELEASE", 0.0, 5000.0, 1.0, " ms");
    configurePadControl(filterCutoffSlider, filterCutoffLabel, "FILTER CUTOFF", 20.0, 20000.0, 1.0, " Hz");
    configurePadControl(filterResonanceSlider, filterResonanceLabel, "FILTER Q", 0.5, 10.0, 0.01, {});
    filterResonanceSlider.setTooltip(magdrum::ui::localisedText(
        "Filter Q controls resonance. Higher Q emphasizes frequencies near the cutoff."));
    filterResonanceSlider.setSkewFactorFromMidPoint(2.0);
    configurePadControl(eqLowSlider, eqLowLabel, "EQ LOW", -18.0, 18.0, 0.1, " dB");
    configurePadControl(eqMidSlider, eqMidLabel, "EQ MID", -18.0, 18.0, 0.1, " dB");
    configurePadControl(eqHighSlider, eqHighLabel, "EQ HIGH", -18.0, 18.0, 0.1, " dB");
    configurePadControl(compressorThresholdSlider, compressorThresholdLabel, "COMP THRESHOLD", -36.0, 0.0, 0.1, " dB");
    configurePadControl(compressorRatioSlider, compressorRatioLabel, "COMP RATIO", 1.0, 12.0, 0.1, ":1");
    configurePadControl(compressorAttackSlider, compressorAttackLabel, "COMP ATTACK", 0.1, 100.0, 0.1, " ms");
    configurePadControl(compressorReleaseSlider, compressorReleaseLabel, "COMP RELEASE", 5.0, 500.0, 1.0, " ms");
    configurePadControl(compressorMakeupSlider, compressorMakeupLabel, "COMP MAKEUP", 0.0, 12.0, 0.1, " dB");
    configurePadControl(phaserRateSlider, phaserRateLabel, "PHASER RATE", 0.0, 8.0, 0.01, " Hz");
    configurePadControl(phaserDepthSlider, phaserDepthLabel, "PHASER DEPTH", 0.0, 100.0, 1.0, "%");
    configurePadControl(phaserFeedbackSlider, phaserFeedbackLabel, "PHASER FEEDBACK", -0.8, 0.8, 0.01, {});
    configurePadControl(phaserMixSlider, phaserMixLabel, "PHASER MIX", 0.0, 100.0, 1.0, "%");
    const std::array<std::pair<juce::Slider*, size_t>, 22> hostAutomatedSliders{{
        {&gainSlider, 0}, {&panSlider, 1}, {&tuneSlider, 2}, {&attackSlider, 3},
        {&decaySlider, 4}, {&sustainSlider, 5}, {&releaseSlider, 6},
        {&filterCutoffSlider, 7}, {&filterResonanceSlider, 8}, {&driveSlider, 9},
        {&eqLowSlider, 10}, {&eqMidSlider, 11}, {&eqHighSlider, 12},
        {&compressorThresholdSlider, 15}, {&compressorRatioSlider, 16},
        {&compressorAttackSlider, 17}, {&compressorReleaseSlider, 18},
        {&compressorMakeupSlider, 19}, {&phaserRateSlider, 20},
        {&phaserDepthSlider, 21}, {&phaserFeedbackSlider, 22}, {&phaserMixSlider, 23}
    }};
    for (const auto& [slider, parameterIndex] : hostAutomatedSliders)
    {
        slider->onDragStart = [this, parameterIndex] { beginPadParameterGesture(parameterIndex); };
        slider->onDragEnd = [this, parameterIndex] { endPadParameterGesture(parameterIndex); };
    }
    compressorRatioSlider.setNumDecimalPlacesToDisplay(1);
    compressorAttackSlider.setNumDecimalPlacesToDisplay(1);
    compressorReleaseSlider.setNumDecimalPlacesToDisplay(0);
    compressorThresholdSlider.setNumDecimalPlacesToDisplay(1);
    phaserRateSlider.setNumDecimalPlacesToDisplay(2);
    phaserDepthSlider.setNumDecimalPlacesToDisplay(0);
    phaserFeedbackSlider.setNumDecimalPlacesToDisplay(2);
    phaserMixSlider.setNumDecimalPlacesToDisplay(0);
    phaserRateSlider.setSkewFactorFromMidPoint(0.5);
    attackSlider.setNumDecimalPlacesToDisplay(0);
    decaySlider.setNumDecimalPlacesToDisplay(0);
    sustainSlider.setNumDecimalPlacesToDisplay(0);
    releaseSlider.setNumDecimalPlacesToDisplay(0);
    filterCutoffSlider.setNumDecimalPlacesToDisplay(0);
    filterCutoffSlider.setSkewFactorFromMidPoint(4000.0);
    for (auto* heading : {&padMixHeading, &padShapeHeading, &padEqHeading, &padFxHeading})
    {
        heading->setJustificationType(juce::Justification::centredLeft);
        heading->setColour(juce::Label::textColourId, Palette::Accent);
        heading->setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        addAndMakeVisible(*heading);
    }
    padMixHeading.setText(magdrum::ui::localisedText("PAD MIX"), juce::dontSendNotification);
    padShapeHeading.setText(magdrum::ui::localisedText("ENVELOPE / FILTER"), juce::dontSendNotification);
    padEqHeading.setText(magdrum::ui::localisedText("PAD EQUALIZER"), juce::dontSendNotification);
    padFxHeading.setText(magdrum::ui::localisedText("COMPRESSOR / PHASER"), juce::dontSendNotification);
    compressorTab.setButtonText(magdrum::ui::localisedText("COMPRESSOR"));
    phaserTab.setButtonText(magdrum::ui::localisedText("PHASER"));
    compressorTab.setClickingTogglesState(false);
    phaserTab.setClickingTogglesState(false);
    compressorTab.addListener(this);
    phaserTab.addListener(this);
    addAndMakeVisible(compressorTab);
    addAndMakeVisible(phaserTab);
    gainSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.gainDb = static_cast<float>(gainSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    panSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.pan = static_cast<float>(panSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    tuneSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.tuneSemitones = static_cast<float>(tuneSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    driveSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.driveDb = static_cast<float>(driveSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    const auto updateEq = [this](int band, float value)
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        if (band == 0) parameters.eqLowDb = value;
        else if (band == 1) parameters.eqMidDb = value;
        else parameters.eqHighDb = value;
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
        repaint(padEqPanelBounds);
    };
    eqLowSlider.onValueChange = [this, updateEq] { updateEq(0, static_cast<float>(eqLowSlider.getValue())); };
    eqMidSlider.onValueChange = [this, updateEq] { updateEq(1, static_cast<float>(eqMidSlider.getValue())); };
    eqHighSlider.onValueChange = [this, updateEq] { updateEq(2, static_cast<float>(eqHighSlider.getValue())); };
    const auto updateDynamics = [this](int control, float value)
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        if (control == 0) parameters.compressorThresholdDb = value;
        else if (control == 1) parameters.compressorRatio = value;
        else if (control == 2) parameters.compressorAttackMs = value;
        else if (control == 3) parameters.compressorReleaseMs = value;
        else parameters.compressorMakeupDb = value;
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    compressorThresholdSlider.onValueChange = [this, updateDynamics] { updateDynamics(0, static_cast<float>(compressorThresholdSlider.getValue())); };
    compressorRatioSlider.onValueChange = [this, updateDynamics] { updateDynamics(1, static_cast<float>(compressorRatioSlider.getValue())); };
    compressorAttackSlider.onValueChange = [this, updateDynamics] { updateDynamics(2, static_cast<float>(compressorAttackSlider.getValue())); };
    compressorReleaseSlider.onValueChange = [this, updateDynamics] { updateDynamics(3, static_cast<float>(compressorReleaseSlider.getValue())); };
    compressorMakeupSlider.onValueChange = [this, updateDynamics] { updateDynamics(4, static_cast<float>(compressorMakeupSlider.getValue())); };
    const auto updatePhaser = [this](int control, float value)
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        if (control == 0) parameters.phaserRateHz = value;
        else if (control == 1) parameters.phaserDepth = value / 100.0f;
        else if (control == 2) parameters.phaserFeedback = value;
        else parameters.phaserMix = value / 100.0f;
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    phaserRateSlider.onValueChange = [this, updatePhaser] { updatePhaser(0, static_cast<float>(phaserRateSlider.getValue())); };
    phaserDepthSlider.onValueChange = [this, updatePhaser] { updatePhaser(1, static_cast<float>(phaserDepthSlider.getValue())); };
    phaserFeedbackSlider.onValueChange = [this, updatePhaser] { updatePhaser(2, static_cast<float>(phaserFeedbackSlider.getValue())); };
    phaserMixSlider.onValueChange = [this, updatePhaser] { updatePhaser(3, static_cast<float>(phaserMixSlider.getValue())); };
    attackSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.attackMs = static_cast<float>(attackSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    releaseSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.releaseMs = static_cast<float>(releaseSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    decaySlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.decayMs = static_cast<float>(decaySlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    sustainSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.sustainLevel = static_cast<float>(sustainSlider.getValue() / 100.0);
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    filterCutoffSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.filterCutoffHz = static_cast<float>(filterCutoffSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
        repaint(padShapePanelBounds);
    };
    filterResonanceSlider.onValueChange = [this]
    {
        if (updatingPadControls) return;
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.filterResonance = static_cast<float>(filterResonanceSlider.getValue());
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
        repaint(padShapePanelBounds);
    };
    waveform->onTrimRangeChanged = [this](float start, float end)
    {
        auto parameters = processor.getNoteParameters(selectedNote);
        parameters.sampleStart = start;
        parameters.sampleEnd = end;
        processor.setNoteParameters(selectedNote, parameters);
        processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    };
    waveform->onTrimGestureChanged = [this](int parameterIndex, bool starting)
    {
        if (starting)
            beginPadParameterGesture(static_cast<size_t>(parameterIndex));
        else
            endPadParameterGesture(static_cast<size_t>(parameterIndex));
    };
    addAndMakeVisible(*waveform);
    velocityRange.setText(magdrum::ui::localisedText("1-127"));velocityRange.setInputRestrictions(7,"0123456789-");velocityRange.setJustification(juce::Justification::centred);velocityRange.setTooltip(magdrum::ui::localisedText("Inclusive velocity range for this sample layer (1–127)."));addAndMakeVisible(velocityRange);
    clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load sample"));for(auto* button:{&clearPadButton,&loadButton}){button->addListener(this);addAndMakeVisible(*button);}
    for(auto& row:stepVelocities)row.fill(100);
    for(size_t row=0;row<laneLabels.size();++row)
    {
        laneLabels[row].setText(localizedSequencerLaneName(row),juce::dontSendNotification);laneLabels[row].setColour(juce::Label::textColourId,categoryColour(laneNames[row]));laneLabels[row].setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));addAndMakeVisible(laneLabels[row]);
        for(size_t localStep=0;localStep<stepButtonCount;++localStep)
        {
            auto& cell=steps[row][localStep];cell.setClickingTogglesState(true);cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(100.0f/127.0f));
            cell.onRightClick=[this,row,localStep]
            {
                const int globalStep=visibleStepIndex(static_cast<int>(localStep));
                if(globalStep<0)return;
                juce::PopupMenu menu;for(int velocity:{50,70,85,100,115,127})menu.addItem(velocity,magdrum::ui::localisedText("Velocity")+" "+juce::String(velocity));juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&steps[row][localStep]),[safe,row,localStep,globalStep](int value)
                {
                    if(safe==nullptr||value<1)return;safe->stepVelocities[row][static_cast<size_t>(globalStep)]=value;auto& target=safe->steps[row][localStep];target.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[row]).withAlpha(static_cast<float>(value)/127.0f));safe->refreshStepTooltips();if(safe->syncSequenceEventForCell(static_cast<int>(row),globalStep))safe->status.setText(magdrum::ui::localisedText("Off-grid hits in this step remain; use Edit hits to review them."),juce::dontSendNotification);safe->savePluginEditorState();
                });
            };
            cell.addListener(this);addAndMakeVisible(cell);
        }
    }
    barSelect.setSelectedId(1);
    barSelect.onChange=[this]{updateSequencerGrid();};
    barSelect.setTooltip(magdrum::ui::localisedText("Select a bar in the current pattern."));
    addAndMakeVisible(barSelect);
    for(int bars=1;bars<=magdrum::sequencer::maximumBars;++bars)
        lengthSelect.addItem(juce::String(bars)+" "+magdrum::ui::localisedText(bars==1?"bar":"bars"),bars);
    lengthSelect.setSelectedId(magdrum::sequencer::maximumBars,juce::dontSendNotification);
    lengthSelect.setTooltip(magdrum::ui::localisedText("Pattern length in bars (1–16)."));
    lengthSelect.onChange=[this]{
        const int requested=lengthSelect.getSelectedId();
        const int newStepCount=patternStepCount();
        for(const auto& row:sequenceStepActive)for(size_t index=static_cast<size_t>(newStepCount);index<row.size();++index)
            if(row[index]){
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
    meterSelect.setTooltip(magdrum::ui::localisedText("Time signature"));
    meterSelect.onChange=[this]{
        const int selectedId=meterSelect.getSelectedId();
        MidiPattern edited;edited.tempoAndSignature=sequenceMetadata;
        if(!setPatternTimeSignature(edited,timeSignatureNumerator(),timeSignatureDenominator()))return;
        const auto map=makeSequencerBarMap(timeSignatureNumerator(),timeSignatureDenominator(),previousPatternBars,edited.tempoAndSignature,gridResolutionSubdivisions());
        if(!map.valid){meterSelect.setSelectedId(previousMeterId,juce::dontSendNotification);status.setText(magdrum::ui::localisedText("That opening time signature is not available in the sequencer grid."),juce::dontSendNotification);return;}
        for(const auto& row:sequenceStepActive)for(size_t index=static_cast<size_t>(map.totalSteps);index<row.size();++index)
            if(row[index]){
                meterSelect.setSelectedId(previousMeterId,juce::dontSendNotification);
                status.setText(magdrum::ui::localisedText("That meter would hide existing hits in the selected pattern length. Move or clear those hits first."),juce::dontSendNotification);
                return;
            }
        sequenceMetadata=std::move(edited.tempoAndSignature);
        previousMeterId=selectedId;
        updateSequencerGrid();
        savePluginEditorState();
    };
    addAndMakeVisible(meterSelect);
    gridResolutionLabel.setText(magdrum::ui::localisedText("Step grid"),juce::dontSendNotification);
    gridResolutionLabel.setColour(juce::Label::textColourId,Palette::Muted);
    addAndMakeVisible(gridResolutionLabel);
    gridResolutionSelect.addItem("1/8",2);gridResolutionSelect.addItem("1/16",4);gridResolutionSelect.addItem("1/32",8);
    gridResolutionSelect.setSelectedId(4,juce::dontSendNotification);
    gridResolutionSelect.setTooltip(magdrum::ui::localisedText("Step grid"));
    gridResolutionSelect.onChange=[this]
    {
        const int selected=gridResolutionSelect.getSelectedId();
        const auto map=makeSequencerBarMap(timeSignatureNumerator(),timeSignatureDenominator(),previousPatternBars,sequenceMetadata,selected);
        if(!map.valid)
        {
            gridResolutionSelect.setSelectedId(previousGridResolution,juce::dontSendNotification);
            status.setText(magdrum::ui::localisedText("This step grid cannot represent the current pattern."),juce::dontSendNotification);
            return;
        }
        previousGridResolution=selected;
        syncSequencerGridFromEvents();
        updateSequencerGrid();
        savePluginEditorState();
    };
    playSequenceButton.setButtonText(magdrum::ui::localisedText("Play"));playSequenceButton.setTooltip(magdrum::ui::localisedText("While the host transport is playing, the sequencer follows its tempo and position."));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));playSequenceButton.addListener(this);clearSequenceButton.addListener(this);exportSequenceButton.addListener(this);addAndMakeVisible(playSequenceButton);addAndMakeVisible(clearSequenceButton);addAndMakeVisible(exportSequenceButton);
    importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));browseMidiButton.setButtonText(magdrum::ui::localisedText("Find drum MIDI"));editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));importMidiButton.addListener(this);browseMidiButton.addListener(this);editMidiEventsButton.addListener(this);reviewMidiButton.addListener(this);addAndMakeVisible(importMidiButton);addAndMakeVisible(browseMidiButton);addAndMakeVisible(editMidiEventsButton);addAndMakeVisible(reviewMidiButton);reviewMidiButton.setEnabled(false);browseMidiButton.setTooltip(magdrum::ui::localisedText("Search the detected default libraries for drum-related MIDI files."));editMidiEventsButton.setTooltip(magdrum::ui::localisedText("Edit individual hits, including articulation, off-grid beat position and duration."));reviewMidiButton.setTooltip(magdrum::ui::localisedText("Compare the imported MIDI pattern with your current sequencer edits."));
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);
    midiFallbackPolicy.setSelectedId(1,juce::dontSendNotification);
    midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));
    midiFallbackPolicy.onChange=[this]{savePluginEditorState();};addAndMakeVisible(midiFallbackPolicy);
    tempoEditor.setText(magdrum::ui::localisedText("120"));tempoEditor.setInputRestrictions(3,"0123456789");tempoEditor.setJustification(juce::Justification::centred);tempoEditor.setIndents(0,0);tempoEditor.setTooltip(magdrum::ui::localisedText("Pattern preview and MIDI export tempo."));tempoEditor.onTextChange=[this]{savePluginEditorState();};addAndMakeVisible(tempoEditor);
    metadataType.addItem(magdrum::ui::localisedText("Tempo event"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(1,juce::dontSendNotification);metadataType.setTooltip(magdrum::ui::localisedText("Choose the MIDI metadata event to edit."));
    metadataBeat.setText(magdrum::ui::localisedText("0"),juce::dontSendNotification);metadataBeat.setInputRestrictions(12,"0123456789.");metadataBeat.setJustification(juce::Justification::centred);metadataBeat.setIndents(0,0);metadataBeat.setTooltip(magdrum::ui::localisedText("Event position in quarter-note beats from the start of the MIDI pattern."));
    metadataValue.setText(magdrum::ui::localisedText("120"),juce::dontSendNotification);metadataValue.setInputRestrictions(8,"0123456789/");metadataValue.setJustification(juce::Justification::centred);metadataValue.setIndents(0,0);metadataValue.setTooltip(magdrum::ui::localisedText("Tempo in BPM, or a time signature such as 4/4."));
    metadataType.onChange=[this]{metadataValue.setText(metadataType.getSelectedId()==1?"120":"4/4",juce::dontSendNotification);};
    for(auto* control:{static_cast<juce::Component*>(&metadataType),static_cast<juce::Component*>(&metadataBeat),static_cast<juce::Component*>(&metadataValue),static_cast<juce::Component*>(&metadataSummary)})addAndMakeVisible(*control);
    for(auto* button:{&setMetadataButton,&removeMetadataButton}){button->addListener(this);addAndMakeVisible(*button);}
    metadataSummary.setColour(juce::Label::textColourId,Palette::Muted);metadataSummary.setMinimumHorizontalScale(0.7f);metadataSummary.setTooltip(magdrum::ui::localisedText("Tempo and time-signature events stored with this sequencer pattern."));refreshSequenceMetadata();
    profileSelect.addItem(magdrum::ui::localisedText("General MIDI"),1);profileSelect.setSelectedId(1);profileSelect.onChange=[this]{const auto name=profileSelect.getText();if(name=="General MIDI")mappingProfile=MidiMappingProfile::generalMidi();else if(const auto loaded=database.loadMappingProfile(name))mappingProfile=*loaded;else return;profileName.setText(name,juce::dontSendNotification);updateDrumNotes();refreshMappingSummary();savePluginEditorState();};addAndMakeVisible(profileSelect);
    profileName.setText(magdrum::ui::localisedText("General MIDI"));profileName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Mapping profile name"),Palette::Muted);profileName.setJustification(juce::Justification::centredLeft);addAndMakeVisible(profileName);
    const char* instrumentNames[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<9;++i)mappingInstrument.addItem(instrumentNames[i],i+1);mappingInstrument.setSelectedId(1);addAndMakeVisible(mappingInstrument);
    const char* articulationNames[]{"Default","Center","Rimshot","SideStick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};for(int i=0;i<13;++i)mappingArticulation.addItem(articulationNames[i],i+1);mappingArticulation.setSelectedId(1);addAndMakeVisible(mappingArticulation);
    mappingNote.setTextToShowWhenEmpty(magdrum::ui::localisedText("MIDI 0-127"),Palette::Muted);mappingNote.setTooltip(magdrum::ui::localisedText("Enter a MIDI note from 0 to 127."));mappingNote.setInputRestrictions(3,"0123456789");mappingNote.setJustification(juce::Justification::centred);mappingNote.setIndents(0,0);addAndMakeVisible(mappingNote);
    for(auto* button:{&loadProfileButton,&saveProfileButton,&importCsvButton,&exportCsvButton,&setMappingButton,&removeMappingButton}){button->addListener(this);addAndMakeVisible(*button);}
    mappingSummary.setColour(juce::Label::textColourId,Palette::Text);mappingSummary.setJustificationType(juce::Justification::topLeft);addAndMakeVisible(mappingSummary);
    pluginInstrumentSelect.onChange=[this]{selectedPluginPresetFile=juce::File();pluginProgramSelect.clear(juce::dontSendNotification);importPluginPresetButton.setEnabled(false);importPluginProgramButton.setEnabled(false);if(pluginProgramScanCancellation)pluginProgramScanCancellation->store(true);refreshPluginKits();};addAndMakeVisible(pluginInstrumentSelect);
    pluginKitSelect.onChange=[this]{scanPluginNotesButton.setEnabled(!pluginScanRunning&&pluginKitSelect.getSelectedId()>0);};addAndMakeVisible(pluginKitSelect);
    pluginProgramSelect.setTextWhenNothingSelected(magdrum::ui::localisedText("Instrument programs"));
    pluginProgramSelect.onChange=[this]{if(pluginProgramSelect.getSelectedId()>0)pluginPresetKitName.setText(pluginProgramSelect.getText(),juce::dontSendNotification);importPluginProgramButton.setEnabled(pluginProgramSelect.getSelectedId()>0&&!pluginPresetImportRunning);};addAndMakeVisible(pluginProgramSelect);
    refreshPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Refresh instruments"));refreshPluginInventoryButton.addListener(this);addAndMakeVisible(refreshPluginInventoryButton);
    scanPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Scan plugin folders"));scanPluginInventoryButton.addListener(this);addAndMakeVisible(scanPluginInventoryButton);
    pluginInventoryProgress.setTextToDisplay(juce::String{});pluginInventoryProgress.setPercentageDisplay(true);pluginInventoryProgress.setColour(juce::ProgressBar::foregroundColourId,Palette::Accent);pluginInventoryProgress.setColour(juce::ProgressBar::backgroundColourId,Palette::Panel);addAndMakeVisible(pluginInventoryProgress);
    choosePluginFolderButton.setButtonText(magdrum::ui::localisedText("Choose VST3 folder"));choosePluginFolderButton.addListener(this);addAndMakeVisible(choosePluginFolderButton);
    pluginPresetKitName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Preset kit name"),Palette::Muted);pluginPresetKitName.setJustification(juce::Justification::centredLeft);addAndMakeVisible(pluginPresetKitName);
    choosePluginPresetButton.setButtonText(magdrum::ui::localisedText("Choose VST3 preset"));choosePluginPresetButton.addListener(this);addAndMakeVisible(choosePluginPresetButton);
    importPluginPresetButton.setButtonText(magdrum::ui::localisedText("Import preset as kit"));importPluginPresetButton.addListener(this);importPluginPresetButton.setEnabled(false);addAndMakeVisible(importPluginPresetButton);
    scanPluginProgramsButton.setButtonText(magdrum::ui::localisedText("Scan programs"));scanPluginProgramsButton.addListener(this);scanPluginProgramsButton.setEnabled(false);addAndMakeVisible(scanPluginProgramsButton);
    importPluginProgramButton.setButtonText(magdrum::ui::localisedText("Import program as kit"));importPluginProgramButton.addListener(this);importPluginProgramButton.setEnabled(false);addAndMakeVisible(importPluginProgramButton);
    openPluginManagerButton.setButtonText(magdrum::ui::localisedText("Open instrument manager"));openPluginManagerButton.addListener(this);addAndMakeVisible(openPluginManagerButton);
    scanPluginNotesButton.setButtonText(magdrum::ui::localisedText("Scan kit notes"));scanPluginNotesButton.addListener(this);scanPluginNotesButton.setEnabled(false);addAndMakeVisible(scanPluginNotesButton);
    cancelPluginNotesButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));cancelPluginNotesButton.addListener(this);cancelPluginNotesButton.setEnabled(false);addAndMakeVisible(cancelPluginNotesButton);
    pluginVelocities.setText(magdrum::ui::localisedText("50, 75, 100, 127"));pluginVelocities.setInputRestrictions(63,"0123456789,; ");pluginVelocities.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity levels"),Palette::Muted);pluginVelocities.setJustification(juce::Justification::centredLeft);addAndMakeVisible(pluginVelocities);
    pluginNoteDuration.setText(magdrum::ui::localisedText("0.25"));pluginNoteDuration.setInputRestrictions(8,"0123456789.");pluginNoteDuration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Note seconds"),Palette::Muted);pluginNoteDuration.setJustification(juce::Justification::centred);addAndMakeVisible(pluginNoteDuration);
    pluginRelease.setText(magdrum::ui::localisedText("0.5"));pluginRelease.setInputRestrictions(8,"0123456789.");pluginRelease.setTextToShowWhenEmpty(magdrum::ui::localisedText("Release seconds"),Palette::Muted);pluginRelease.setJustification(juce::Justification::centred);addAndMakeVisible(pluginRelease);
    pluginCaptureLength.setText(magdrum::ui::localisedText("0.75"));pluginCaptureLength.setInputRestrictions(8,"0123456789.");pluginCaptureLength.setTextToShowWhenEmpty(magdrum::ui::localisedText("Capture seconds"),Palette::Muted);pluginCaptureLength.setJustification(juce::Justification::centred);addAndMakeVisible(pluginCaptureLength);
    pluginRepeatCount.setText(magdrum::ui::localisedText("1"));pluginRepeatCount.setInputRestrictions(2,"0123456789");pluginRepeatCount.setTextToShowWhenEmpty(magdrum::ui::localisedText("Repeats"),Palette::Muted);pluginRepeatCount.setJustification(juce::Justification::centred);addAndMakeVisible(pluginRepeatCount);
    for(auto* label:{&pluginVelocityLabel,&pluginNoteDurationLabel,&pluginReleaseLabel,&pluginCaptureLabel,&pluginRepeatLabel}){label->setColour(juce::Label::textColourId,Palette::Muted);label->setFont(juce::Font(juce::FontOptions(12.0f)));addAndMakeVisible(*label);}
    pluginRenderMode.addItem(magdrum::ui::localisedText("Offline"),1);pluginRenderMode.addItem(magdrum::ui::localisedText("Real-time paced"),2);pluginRenderMode.addItem(magdrum::ui::localisedText("Audio device"),3);pluginRenderMode.setSelectedId(1);addAndMakeVisible(pluginRenderMode);
    pluginScanProgress.setColour(juce::Label::textColourId,Palette::Accent);pluginScanProgress.setJustificationType(juce::Justification::centredLeft);addAndMakeVisible(pluginScanProgress);
    pluginScanInfo.setColour(juce::Label::textColourId,Palette::Muted);pluginScanInfo.setJustificationType(juce::Justification::topLeft);pluginScanInfo.setText(magdrum::ui::localisedText("Select a discovered instrument, import a preset as a kit, then scan its MIDI notes."),juce::dontSendNotification);addAndMakeVisible(pluginScanInfo);
    status.setColour(juce::Label::textColourId,Palette::Muted);status.setJustificationType(juce::Justification::centredLeft);addAndMakeVisible(status);
    const auto dataDirectory=databaseFileOverride!=juce::File()?databaseFileOverride.getParentDirectory():getMagDrumDataDirectory();
    if(dataDirectory!=juce::File()&&(dataDirectory.createDirectory().wasOk()||dataDirectory.isDirectory())){databaseFile=databaseFileOverride!=juce::File()?databaseFileOverride:dataDirectory.getChildFile("samples.sqlite3");databaseReady=database.open(databaseFile);}
    if(!databaseReady)status.setText(magdrum::ui::localisedText("Sample index unavailable. Choose an audio file or scan a folder here."),juce::dontSendNotification);
    else{refreshSourceKitFilters();refreshSamples();for(const auto& name:database.getMappingProfileNames())if(name!="General MIDI")profileSelect.addItem(name,profileSelect.getNumItems()+1);if(const auto stored=database.loadMappingProfile("General MIDI"))mappingProfile=*stored;}
    for(auto* field:{&sampleSearch,&velocityRange,&profileName,&mappingNote,&metadataBeat,&metadataValue,&pluginVelocities,&pluginNoteDuration,&pluginRelease,&pluginCaptureLength,&pluginRepeatCount,&pluginPresetKitName})field->setIndents(8,0);
    tempoEditor.setIndents(0,0);
    refreshPluginInventory();restorePluginEditorState();if(!sequenceEventsInitialised)syncSequenceEventsFromGrid();updateSequencerGrid();updateDrumNotes();setNote(mappedPadNotes[0]);refreshMappingSummary();setPage(0);applyLanguage();
}

MagDrumAudioProcessorEditor::~MagDrumAudioProcessorEditor(){for(size_t index=0;index<activePadParameterGestureNotes.size();++index)endPadParameterGesture(index);padActivityTimer.stop();magdrum::ui::languageChangeBroadcaster().removeChangeListener(this);processor.removeSampleStateListener(this);midiEventWindow.reset();comparisonWindow.reset();stopTimer();++samplePreviewGeneration;samplePreviewWorkers.removeAllJobs(true,-1);sampleLoadCancellation.store(true,std::memory_order_release);sampleLoadWorkers.removeAllJobs(true,-1);if(scanCancellation)scanCancellation->store(true);if(pluginInventoryScanCancellation)pluginInventoryScanCancellation->store(true);if(pluginPresetImportCancellation)pluginPresetImportCancellation->store(true);if(pluginProgramScanCancellation)pluginProgramScanCancellation->store(true);pluginScanCancellation.store(true);sampleBrowseWorkers.removeAllJobs(true,-1);sampleMapWorkers.removeAllJobs(true,-1);sampleLookupWorkers.removeAllJobs(true,-1);sampleReviewWorkers.removeAllJobs(true,-1);metadataArchiveWorkers.removeAllJobs(true,-1);scanWorkers.removeAllJobs(true,-1);metadataSyncWorkers.removeAllJobs(true,-1);pluginInventoryWorkers.removeAllJobs(true,-1);pluginPresetImportWorkers.removeAllJobs(true,-1);pluginProgramScanWorkers.removeAllJobs(true,-1);pluginScanWorkers.removeAllJobs(true,-1);setLookAndFeel(nullptr);}

void MagDrumAudioProcessorEditor::paint(juce::Graphics& g)
{
    magdrum::ui::paintTexturedBackground(g,getLocalBounds(),0.42f);g.setColour(Palette::Accent);g.setFont(juce::Font(juce::FontOptions(30.0f,juce::Font::bold)));g.drawText("MagDrum",24,14,260,40,juce::Justification::centredLeft);
    if (page == 1)
    {
        const auto drawInstrumentPanel = [&g](juce::Rectangle<int> area, juce::Colour accent)
        {
            if (area.isEmpty()) return;
            area = area.toFloat().getSmallestIntegerContainer();
            juce::DropShadow(juce::Colours::black.withAlpha(0.34f), 7, { 0, 3 })
                .drawForRectangle(g, area);
            juce::ColourGradient surface(Palette::Panel.brighter(0.055f),
                                         static_cast<float>(area.getX()), static_cast<float>(area.getY()),
                                         Palette::Panel.darker(0.18f),
                                         static_cast<float>(area.getX()), static_cast<float>(area.getBottom()), false);
            g.setGradientFill(surface);
            g.fillRoundedRectangle(area.toFloat(), 8.0f);
            g.setColour(Palette::Border.withAlpha(0.82f));
            g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);
            g.setColour(accent.withAlpha(0.68f));
            g.drawHorizontalLine(area.getY() + 1, static_cast<float>(area.getX() + 10),
                                 static_cast<float>(area.getRight() - 10));
        };
        drawInstrumentPanel(drumPadTrayBounds, Palette::Accent);
        drawInstrumentPanel(drumEditorPanelBounds, Palette::Accent);
        drawInstrumentPanel(drumWaveformPanelBounds, Palette::Accent.brighter(0.15f));
        drawInstrumentPanel(padMixPanelBounds, Palette::Accent);
        drawInstrumentPanel(padShapePanelBounds, Palette::Accent.brighter(0.08f));
        drawInstrumentPanel(padEqPanelBounds, Palette::Accent.brighter(0.15f));
        drawInstrumentPanel(padFxPanelBounds, Palette::Accent.brighter(0.22f));

        const auto parameters = processor.getNoteParameters(selectedNote);
        const double sampleRate = processor.getSampleRate() > 1000.0 ? processor.getSampleRate() : 48000.0;
        const auto drawResponse = [&g](juce::Rectangle<int> panel, juce::Colour accent,
                                      float minDb, float maxDb, const auto& responseAt)
        {
            if (panel.isEmpty()) return;
            auto graph = panel.reduced(8, 0).withHeight(16).removeFromRight(142);
            graph.setY(panel.getY() + 1);
            if (graph.getWidth() < 48) return;
            g.setColour(juce::Colour(0xff10151b).withAlpha(0.96f));
            g.fillRoundedRectangle(graph.toFloat(), 2.5f);
            g.setColour(Palette::Border.withAlpha(0.8f));
            g.drawRoundedRectangle(graph.toFloat().reduced(0.5f), 2.5f, 0.7f);
            g.setColour(Palette::Border.withAlpha(0.42f));
            for (int division = 1; division < 4; ++division)
            {
                const float x = static_cast<float>(graph.getX())
                    + static_cast<float>(graph.getWidth()) * division / 4.0f;
                g.drawVerticalLine(juce::roundToInt(x), static_cast<float>(graph.getY() + 2),
                                   static_cast<float>(graph.getBottom() - 2));
            }
            const float zeroY = juce::jmap(0.0f, minDb, maxDb,
                                           static_cast<float>(graph.getBottom() - 1),
                                           static_cast<float>(graph.getY() + 1));
            g.setColour(Palette::Muted.withAlpha(0.58f));
            g.drawHorizontalLine(juce::roundToInt(zeroY), static_cast<float>(graph.getX() + 2),
                                 static_cast<float>(graph.getRight() - 2));
            juce::Path curve;
            for (int x = 0; x < graph.getWidth(); ++x)
            {
                const double proportion = static_cast<double>(x) / juce::jmax(1, graph.getWidth() - 1);
                const double frequency = 20.0 * std::pow(1000.0, proportion);
                const float db = juce::jlimit(minDb, maxDb, responseAt(frequency));
                const float y = juce::jmap(db, minDb, maxDb,
                                           static_cast<float>(graph.getBottom() - 1),
                                           static_cast<float>(graph.getY() + 1));
                if (x == 0) curve.startNewSubPath(static_cast<float>(graph.getX() + x), y);
                else curve.lineTo(static_cast<float>(graph.getX() + x), y);
            }
            g.setColour(accent.withAlpha(0.18f));
            g.strokePath(curve, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
            g.setColour(accent.brighter(0.25f));
            g.strokePath(curve, juce::PathStrokeType(1.25f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        };
        const auto onePole = [](double frequency, double cutoff, double sampleRateHz)
        {
            const double coefficient = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * cutoff / sampleRateHz);
            const double angle = -2.0 * juce::MathConstants<double>::pi * frequency / sampleRateHz;
            const std::complex<double> denominator(1.0 - (1.0 - coefficient) * std::cos(angle),
                                                    -(1.0 - coefficient) * std::sin(angle));
            return std::complex<double>(coefficient, 0.0) / denominator;
        };
        const auto dbFromMagnitude = [](double magnitude)
        {
            return static_cast<float>(juce::Decibels::gainToDecibels(
                static_cast<float>(std::max(magnitude, 1.0e-5)), -100.0f));
        };
        drawResponse(padShapePanelBounds, Palette::Accent.brighter(0.08f), -36.0f, 18.0f,
                     [parameters, sampleRate, dbFromMagnitude](double frequency)
        {
            const double cutoff = std::min<double>(parameters.filterCutoffHz, sampleRate * 0.49);
            if (parameters.filterCutoffHz >= 19999.0f || cutoff >= sampleRate * 0.45) return 0.0f;
            const double omega = 2.0 * juce::MathConstants<double>::pi * cutoff / sampleRate;
            const double alpha = std::sin(omega) / (2.0 * juce::jlimit(0.5f, 10.0f, parameters.filterResonance));
            const double cosine = std::cos(omega);
            const double inverseA0 = 1.0 / (1.0 + alpha);
            const double b0 = (1.0 - cosine) * 0.5 * inverseA0;
            const double b1 = (1.0 - cosine) * inverseA0;
            const double b2 = b0;
            const double a1 = -2.0 * cosine * inverseA0;
            const double a2 = (1.0 - alpha) * inverseA0;
            const double angle = -2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
            const std::complex<double> z1(std::cos(angle), std::sin(angle));
            const auto z2 = z1 * z1;
            const auto numerator = b0 + b1 * z1 + b2 * z2;
            const auto denominator = 1.0 + a1 * z1 + a2 * z2;
            return dbFromMagnitude(std::abs(numerator / denominator));
        });
        drawResponse(padEqPanelBounds, Palette::Accent.brighter(0.15f), -18.0f, 18.0f,
                     [parameters, sampleRate, onePole, dbFromMagnitude](double frequency)
        {
            const auto low = onePole(frequency, 200.0, sampleRate);
            const auto broad = onePole(frequency, 4000.0, sampleRate);
            const auto response = low * static_cast<double>(juce::Decibels::decibelsToGain(parameters.eqLowDb))
                + (broad - low) * static_cast<double>(juce::Decibels::decibelsToGain(parameters.eqMidDb))
                + (std::complex<double>(1.0, 0.0) - broad)
                    * static_cast<double>(juce::Decibels::decibelsToGain(parameters.eqHighDb));
            return dbFromMagnitude(std::abs(response));
        });
    }
    auto breadcrumb=magdrum::ui::localisedText("SAMPLE LIBRARY")+"  /  "+magdrum::ui::localisedText("DRUM MACHINE")+"  /  "+magdrum::ui::localisedText("SEQUENCER")+"  /  "+magdrum::ui::localisedText("MIDI MAPS");
    if(hasPluginScannerPage)breadcrumb+="  /  "+magdrum::ui::localisedText("PLUGIN SCANNER");
    g.setColour(Palette::Muted);g.setFont(13.0f);g.drawText(breadcrumb,26,51,960,22,juce::Justification::centredLeft);
    g.setColour(Palette::Border);g.drawHorizontalLine(83,24.0f,static_cast<float>(getWidth()-24));
}

void MagDrumAudioProcessorEditor::resized()
{
    auto bounds=getLocalBounds().reduced(24);bounds.removeFromTop(67);auto tabs=bounds.removeFromTop(38);
    languageButton.setBounds(getWidth()-82, 91, 58, 34);
    libraryTab.setBounds(tabs.removeFromLeft(150));tabs.removeFromLeft(6);machineTab.setBounds(tabs.removeFromLeft(150));tabs.removeFromLeft(6);sequencerTab.setBounds(tabs.removeFromLeft(150));tabs.removeFromLeft(6);mapsTab.setBounds(tabs.removeFromLeft(150));tabs.removeFromLeft(6);
    if(hasPluginScannerPage)pluginScannerTab.setBounds(tabs.removeFromLeft(170));else pluginScannerTab.setBounds({});
    bounds.removeFromTop(12);
    auto footer=bounds.removeFromBottom(26);status.setBounds(footer);bounds.removeFromBottom(8);
    libraryTab.setToggleState(page==0,juce::dontSendNotification);machineTab.setToggleState(page==1,juce::dontSendNotification);sequencerTab.setToggleState(page==2,juce::dontSendNotification);mapsTab.setToggleState(page==3,juce::dontSendNotification);pluginScannerTab.setToggleState(page==4,juce::dontSendNotification);
    sampleSearch.setVisible(page==0);categoryFilter.setVisible(page==0);contentTypeFilter.setVisible(page==0);featureFilter.setVisible(page==0);sortFilter.setVisible(page==0);confidenceFilter.setVisible(page==0);sourceFilter.setVisible(page==0);kitFilter.setVisible(page==0);scanButton.setVisible(page==0);scanDefaultsButton.setVisible(page==0);instrumentScannerButton.setVisible(page==0);openSourceKitButton.setVisible(page==0);searchButton.setVisible(page==0);previewButton.setVisible(page==0);stopPreviewButton.setVisible(page==0);assignButton.setVisible(page==0);libraryToolsButton.setVisible(page==0);moreSamplesButton.setVisible(page==0);indexedList.setVisible(page==0);sampleMap->setVisible(page==0);
    for(auto& pad:pads)pad.setVisible(page==1);selectedPadTitle.setVisible(page==1);chokeGroupLabel.setVisible(page==1);chokeGroupSelect.setVisible(page==1);manageLayersButton.setVisible(page==1);layerSummary.setVisible(page==1);sampleDetails.setVisible(page==1);waveform->setVisible(page==1);padMixHeading.setVisible(page==1);padShapeHeading.setVisible(page==1);padEqHeading.setVisible(page==1);padFxHeading.setVisible(page==1);gainLabel.setVisible(page==1);panLabel.setVisible(page==1);tuneLabel.setVisible(page==1);driveLabel.setVisible(page==1);attackLabel.setVisible(page==1);decayLabel.setVisible(page==1);sustainLabel.setVisible(page==1);releaseLabel.setVisible(page==1);filterCutoffLabel.setVisible(page==1);eqLowLabel.setVisible(page==1);eqMidLabel.setVisible(page==1);eqHighLabel.setVisible(page==1);gainSlider.setVisible(page==1);panSlider.setVisible(page==1);tuneSlider.setVisible(page==1);driveSlider.setVisible(page==1);attackSlider.setVisible(page==1);decaySlider.setVisible(page==1);sustainSlider.setVisible(page==1);releaseSlider.setVisible(page==1);filterCutoffSlider.setVisible(page==1);eqLowSlider.setVisible(page==1);eqMidSlider.setVisible(page==1);eqHighSlider.setVisible(page==1);velocityRange.setVisible(page==1);clearPadButton.setVisible(page==1);loadButton.setVisible(page==1);
    filterResonanceLabel.setVisible(page==1);
    filterResonanceSlider.setVisible(page==1);
    for(auto* label:{&compressorThresholdLabel,&compressorRatioLabel,&compressorAttackLabel,&compressorReleaseLabel,&compressorMakeupLabel})label->setVisible(page==1&&!showPhaserControls);
    for(auto* label:{&phaserRateLabel,&phaserDepthLabel,&phaserFeedbackLabel,&phaserMixLabel})label->setVisible(page==1&&showPhaserControls);
    for(auto* slider:{&compressorThresholdSlider,&compressorRatioSlider,&compressorAttackSlider,&compressorReleaseSlider,&compressorMakeupSlider})slider->setVisible(page==1&&!showPhaserControls);
    for(auto* slider:{&phaserRateSlider,&phaserDepthSlider,&phaserFeedbackSlider,&phaserMixSlider})slider->setVisible(page==1&&showPhaserControls);
    compressorTab.setVisible(page==1);phaserTab.setVisible(page==1);
    compressorTab.setColour(juce::TextButton::buttonColourId,showPhaserControls?Palette::Surface:Palette::Accent.withAlpha(0.82f));
    phaserTab.setColour(juce::TextButton::buttonColourId,showPhaserControls?Palette::Accent.withAlpha(0.82f):Palette::Surface);
    const auto barMap=currentBarMap();
    const int selectedBar=juce::jlimit(1,barMap.barCount,barSelect.getSelectedId()>0?barSelect.getSelectedId():1);
    const auto& visibleBar=barMap.bars[static_cast<size_t>(selectedBar-1)];
    for(auto& label:laneLabels)label.setVisible(page==2);
    for(size_t rowIndex=0;rowIndex<steps.size();++rowIndex)
        for(size_t localStep=0;localStep<steps[rowIndex].size();++localStep)
        {
            const bool active=page==2&&localStep<static_cast<size_t>(visibleBar.stepCount);
            auto& cell=steps[rowIndex][localStep];
            cell.setVisible(active);
            if(!active)continue;
            const auto globalStep=static_cast<size_t>(visibleBar.startStep)+localStep;
            cell.setToggleState(sequenceStepActive[rowIndex][globalStep],juce::dontSendNotification);
            cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[rowIndex]).withAlpha(static_cast<float>(stepVelocities[rowIndex][globalStep])/127.0f));
        }
    playSequenceButton.setVisible(page==2);clearSequenceButton.setVisible(page==2);exportSequenceButton.setVisible(page==2);importMidiButton.setVisible(page==2);browseMidiButton.setVisible(page==2);editMidiEventsButton.setVisible(page==2);reviewMidiButton.setVisible(page==2);midiFallbackPolicy.setVisible(page==2);tempoEditor.setVisible(page==2);barSelect.setVisible(page==2);meterSelect.setVisible(page==2);lengthSelect.setVisible(page==2);gridResolutionLabel.setVisible(page==2);gridResolutionSelect.setVisible(page==2);metadataType.setVisible(page==2);metadataBeat.setVisible(page==2);metadataValue.setVisible(page==2);setMetadataButton.setVisible(page==2);removeMetadataButton.setVisible(page==2);metadataSummary.setVisible(page==2);
    profileSelect.setVisible(page==3);profileName.setVisible(page==3);mappingInstrument.setVisible(page==3);mappingArticulation.setVisible(page==3);mappingNote.setVisible(page==3);mappingSummary.setVisible(page==3);loadProfileButton.setVisible(page==3);saveProfileButton.setVisible(page==3);importCsvButton.setVisible(page==3);exportCsvButton.setVisible(page==3);setMappingButton.setVisible(page==3);removeMappingButton.setVisible(page==3);
    for(auto* control:{static_cast<juce::Component*>(&pluginInstrumentSelect),static_cast<juce::Component*>(&pluginKitSelect),static_cast<juce::Component*>(&pluginRenderMode),static_cast<juce::Component*>(&pluginPresetKitName),static_cast<juce::Component*>(&pluginProgramSelect),static_cast<juce::Component*>(&pluginVelocities),static_cast<juce::Component*>(&pluginNoteDuration),static_cast<juce::Component*>(&pluginRelease),static_cast<juce::Component*>(&pluginCaptureLength),static_cast<juce::Component*>(&pluginRepeatCount),static_cast<juce::Component*>(&pluginScanProgress),static_cast<juce::Component*>(&pluginScanInfo),static_cast<juce::Component*>(&pluginVelocityLabel),static_cast<juce::Component*>(&pluginNoteDurationLabel),static_cast<juce::Component*>(&pluginReleaseLabel),static_cast<juce::Component*>(&pluginCaptureLabel),static_cast<juce::Component*>(&pluginRepeatLabel)})control->setVisible(hasPluginScannerPage&&page==4);
    for(auto* button:{&refreshPluginInventoryButton,&scanPluginInventoryButton,&choosePluginFolderButton,&openPluginManagerButton,&choosePluginPresetButton,&importPluginPresetButton,&scanPluginProgramsButton,&importPluginProgramButton,&scanPluginNotesButton,&cancelPluginNotesButton})button->setVisible(hasPluginScannerPage&&page==4);
    sampleScanProgress.setVisible(page==0&&scanning);
    sampleScanProgressLabel.setVisible(page==0&&scanning);
    pluginInventoryProgress.setVisible(hasPluginScannerPage&&page==4&&pluginInventoryScanRunning);
    if(page==0)
    {
        auto controls=bounds.removeFromTop(34);sampleSearch.setBounds(controls.removeFromLeft(225));controls.removeFromLeft(6);categoryFilter.setBounds(controls.removeFromLeft(135));controls.removeFromLeft(6);featureFilter.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);sortFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);confidenceFilter.setBounds(controls.removeFromLeft(125));controls.removeFromLeft(6);searchButton.setBounds(controls.removeFromLeft(78));controls.removeFromLeft(6);previewButton.setBounds(controls.removeFromLeft(78));controls.removeFromLeft(6);stopPreviewButton.setBounds(controls.removeFromLeft(64));
        bounds.removeFromTop(6);controls=bounds.removeFromTop(34);sourceFilter.setBounds(controls.removeFromLeft(180));controls.removeFromLeft(6);contentTypeFilter.setBounds(controls.removeFromLeft(220));controls.removeFromLeft(6);kitFilter.setBounds(controls.removeFromLeft(190));
        bounds.removeFromTop(6);controls=bounds.removeFromTop(34);scanButton.setBounds(controls.removeFromLeft(145));controls.removeFromLeft(6);scanDefaultsButton.setBounds(controls.removeFromLeft(190));controls.removeFromLeft(8);instrumentScannerButton.setBounds(controls.removeFromLeft(140));controls.removeFromLeft(8);openSourceKitButton.setBounds(controls.removeFromLeft(140));controls.removeFromLeft(8);assignButton.setBounds(controls.removeFromLeft(118));controls.removeFromLeft(6);libraryToolsButton.setBounds(controls.removeFromLeft(115));controls.removeFromLeft(6);moreSamplesButton.setBounds(controls.removeFromLeft(110));moreSamplesButton.setEnabled(hasMoreSamples);
        bounds.removeFromTop(5);auto progressRow=bounds.removeFromTop(16);sampleScanProgress.setBounds(progressRow.removeFromLeft(180));progressRow.removeFromLeft(10);sampleScanProgressLabel.setBounds(progressRow);
        bounds.removeFromTop(5);auto content=bounds;const int left=juce::jlimit(320,440,content.getWidth()*2/5);indexedList.setBounds(content.removeFromLeft(left));content.removeFromLeft(12);sampleMap->setBounds(content);
    }
    else if(page==1)
    {
        drumPadTrayBounds = {};
        drumEditorPanelBounds = {};
        drumWaveformPanelBounds = {};
        padMixPanelBounds = {};
        padShapePanelBounds = {};
        padEqPanelBounds = {};
        padFxPanelBounds = {};
        const auto fitValueBox = [](juce::Slider& slider, int columnWidth)
        {
            const int valueBoxWidth = juce::jlimit(1, 82, columnWidth - 4);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, valueBoxWidth, 20);
        };
        auto grid=bounds.removeFromLeft(bounds.getWidth()*3/5);
        drumPadTrayBounds = grid.expanded(7, 7);
        const int gap=10;
        const int padW=(grid.getWidth()-3*gap)/4,padH=(grid.getHeight()-3*gap)/4;
        for(size_t i=0;i<pads.size();++i)
        {
            const int row=static_cast<int>(i/4),col=static_cast<int>(i%4);
            pads[i].setBounds(grid.getX()+col*(padW+gap),grid.getY()+row*(padH+gap),padW,padH);
        }
        bounds.removeFromLeft(18);
        drumEditorPanelBounds = bounds.expanded(8, 7);
        bounds.reduce(8, 4);
        const int controlGroupHeight = juce::jlimit(64, 94,
            64 + juce::jlimit(0, 120, bounds.getHeight() - 413) * 30 / 120);
        const int waveformHeight = juce::jmax(28,
            bounds.getHeight() - 128 - 4 * controlGroupHeight);
        auto selectedPadRow = bounds.removeFromTop(26);
        auto chokeArea = selectedPadRow.removeFromRight(158);
        chokeGroupSelect.setBounds(chokeArea.removeFromRight(98).reduced(0, 1));
        chokeArea.removeFromRight(5);
        chokeGroupLabel.setBounds(chokeArea.removeFromRight(50));
        selectedPadTitle.setBounds(selectedPadRow);
        bounds.removeFromTop(2);
        auto sampleDetailsRow = bounds.removeFromTop(36);
        manageLayersButton.setBounds(sampleDetailsRow.removeFromRight(92).reduced(0, 2));
        sampleDetails.setBounds(sampleDetailsRow);bounds.removeFromTop(3);
        drumWaveformPanelBounds = bounds.removeFromTop(waveformHeight).expanded(5, 3);
        waveform->setBounds(drumWaveformPanelBounds.reduced(5, 5));
        bounds.removeFromTop(5);
        padMixPanelBounds = bounds.removeFromTop(controlGroupHeight).expanded(5, 2);
        auto mixPanel = padMixPanelBounds.reduced(5, 3);
        padMixHeading.setBounds(mixPanel.removeFromTop(15));
        auto controls=mixPanel.removeFromTop(controlGroupHeight - 21);
        const int controlWidth=controls.getWidth()/4;
        const std::array<std::pair<juce::Label*,juce::Slider*>,4> padMixControls{{
            {&gainLabel,&gainSlider},{&panLabel,&panSlider},{&tuneLabel,&tuneSlider},{&driveLabel,&driveSlider}}};
        for(size_t index=0;index<padMixControls.size();++index)
        {
            auto column=controls.removeFromLeft(index+1==padMixControls.size()?controls.getWidth():controlWidth);
            padMixControls[index].first->setBounds(column.removeFromTop(15));
            fitValueBox(*padMixControls[index].second, column.getWidth());
            padMixControls[index].second->setBounds(column);
        }
        bounds.removeFromTop(4);
        padShapePanelBounds = bounds.removeFromTop(controlGroupHeight).expanded(5, 2);
        auto shapePanel = padShapePanelBounds.reduced(5, 3);
        padShapeHeading.setBounds(shapePanel.removeFromTop(15));
        controls=shapePanel.removeFromTop(controlGroupHeight - 21);
        const int shapeControlWidth=controls.getWidth()/6;
        const std::array<std::pair<juce::Label*,juce::Slider*>,6> padShapeControls{{
            {&attackLabel,&attackSlider},{&decayLabel,&decaySlider},{&sustainLabel,&sustainSlider},
            {&releaseLabel,&releaseSlider},{&filterCutoffLabel,&filterCutoffSlider},
            {&filterResonanceLabel,&filterResonanceSlider}}};
        for(size_t index=0;index<padShapeControls.size();++index)
        {
            auto column=controls.removeFromLeft(index+1==padShapeControls.size()?controls.getWidth():shapeControlWidth);
            padShapeControls[index].first->setBounds(column.removeFromTop(15));
            fitValueBox(*padShapeControls[index].second, column.getWidth());
            padShapeControls[index].second->setBounds(column);
        }
        bounds.removeFromTop(4);
        padEqPanelBounds = bounds.removeFromTop(controlGroupHeight).expanded(5, 2);
        auto eqPanel = padEqPanelBounds.reduced(5, 3);
        padEqHeading.setBounds(eqPanel.removeFromTop(15));
        controls=eqPanel.removeFromTop(controlGroupHeight - 21);
        const int eqControlWidth=controls.getWidth()/3;
        const std::array<std::pair<juce::Label*,juce::Slider*>,3> padEqControls{{
            {&eqLowLabel,&eqLowSlider},{&eqMidLabel,&eqMidSlider},{&eqHighLabel,&eqHighSlider}}};
        for(size_t index=0;index<padEqControls.size();++index)
        {
            auto column=controls.removeFromLeft(index+1==padEqControls.size()?controls.getWidth():eqControlWidth);
            padEqControls[index].first->setBounds(column.removeFromTop(15));
            fitValueBox(*padEqControls[index].second, column.getWidth());
            padEqControls[index].second->setBounds(column);
        }
        bounds.removeFromTop(4);
        padFxPanelBounds = bounds.removeFromTop(controlGroupHeight).expanded(5, 2);
        auto fxPanel = padFxPanelBounds.reduced(5, 3);
        auto fxHeader = fxPanel.removeFromTop(16);
        padFxHeading.setBounds(fxHeader.removeFromLeft(144));
        fxHeader.removeFromLeft(4);
        compressorTab.setBounds(fxHeader.removeFromLeft(86));
        fxHeader.removeFromLeft(4);
        phaserTab.setBounds(fxHeader.removeFromLeft(70));
        auto effectControls = fxPanel;
        const std::array<std::pair<juce::Label*,juce::Slider*>,5> compressorControls{{
            {&compressorThresholdLabel,&compressorThresholdSlider},{&compressorRatioLabel,&compressorRatioSlider},
            {&compressorAttackLabel,&compressorAttackSlider},{&compressorReleaseLabel,&compressorReleaseSlider},
            {&compressorMakeupLabel,&compressorMakeupSlider}}};
        for(size_t index=0;index<compressorControls.size();++index)
        {
            auto column=effectControls.removeFromLeft(index+1==compressorControls.size()?effectControls.getWidth():effectControls.getWidth()/(static_cast<int>(compressorControls.size()-index)));
            compressorControls[index].first->setBounds(column.removeFromTop(13));
            fitValueBox(*compressorControls[index].second, column.getWidth());
            compressorControls[index].second->setBounds(column);
        }
        effectControls = fxPanel;
        const std::array<std::pair<juce::Label*,juce::Slider*>,4> phaserControls{{
            {&phaserRateLabel,&phaserRateSlider},{&phaserDepthLabel,&phaserDepthSlider},
            {&phaserFeedbackLabel,&phaserFeedbackSlider},{&phaserMixLabel,&phaserMixSlider}}};
        for(size_t index=0;index<phaserControls.size();++index)
        {
            auto column=effectControls.removeFromLeft(index+1==phaserControls.size()?effectControls.getWidth():effectControls.getWidth()/(static_cast<int>(phaserControls.size()-index)));
            phaserControls[index].first->setBounds(column.removeFromTop(13));
            fitValueBox(*phaserControls[index].second, column.getWidth());
            phaserControls[index].second->setBounds(column);
        }
        bounds.removeFromTop(3);
        bounds.removeFromTop(4);
        auto row=bounds.removeFromBottom(34);velocityRange.setBounds(row.removeFromLeft(100));row.removeFromLeft(8);clearPadButton.setBounds(row.removeFromLeft(108));row.removeFromLeft(8);loadButton.setBounds(row);
        layerSummary.setBounds(bounds);
    }
    else if(page==2)
    {
        auto toolbar=bounds.removeFromTop(36);
        playSequenceButton.setBounds(toolbar.removeFromLeft(82));toolbar.removeFromLeft(6);
        clearSequenceButton.setBounds(toolbar.removeFromLeft(116));toolbar.removeFromLeft(6);
        importMidiButton.setBounds(toolbar.removeFromLeft(96));toolbar.removeFromLeft(6);
        browseMidiButton.setBounds(toolbar.removeFromLeft(142));toolbar.removeFromLeft(6);
        exportSequenceButton.setBounds(toolbar.removeFromLeft(110));toolbar.removeFromLeft(6);
        editMidiEventsButton.setBounds(toolbar.removeFromLeft(84));toolbar.removeFromLeft(6);
        reviewMidiButton.setBounds(toolbar.removeFromLeft(120));toolbar.removeFromLeft(6);
        tempoEditor.setBounds(toolbar.removeFromLeft(70));toolbar.removeFromLeft(8);
        meterSelect.setBounds(toolbar.removeFromLeft(74));toolbar.removeFromLeft(6);
        lengthSelect.setBounds(toolbar.removeFromLeft(78));toolbar.removeFromLeft(6);
        barSelect.setBounds(toolbar.removeFromLeft(82));
        auto metaRow=bounds.removeFromTop(32);
        gridResolutionLabel.setBounds(metaRow.removeFromLeft(76));gridResolutionSelect.setBounds(metaRow.removeFromLeft(72));metaRow.removeFromLeft(8);
        metadataType.setBounds(metaRow.removeFromLeft(132));metaRow.removeFromLeft(6);
        metadataBeat.setBounds(metaRow.removeFromLeft(74));metaRow.removeFromLeft(6);
        metadataValue.setBounds(metaRow.removeFromLeft(74));metaRow.removeFromLeft(6);
        setMetadataButton.setBounds(metaRow.removeFromLeft(86));metaRow.removeFromLeft(6);
        removeMetadataButton.setBounds(metaRow.removeFromLeft(100));metaRow.removeFromLeft(8);
        metadataSummary.setBounds(metaRow);bounds.removeFromTop(8);
        auto fallbackRow=bounds.removeFromTop(26);midiFallbackPolicy.setBounds(fallbackRow.removeFromLeft(320));bounds.removeFromTop(8);
        constexpr int labelWidth=122;
        const auto map=currentBarMap();
        const int selectedGridBar=juce::jlimit(1,map.barCount,barSelect.getSelectedId()>0?barSelect.getSelectedId():1);
        const auto& span=map.bars[static_cast<size_t>(selectedGridBar-1)];
        const int activeSteps=span.stepCount;
        const int cellWidth=juce::jmax(16,(bounds.getWidth()-labelWidth-(activeSteps-1)*4)/activeSteps);
        for(int row=0;row<16;++row)
        {
            auto line=bounds.removeFromTop(juce::jmax(20,bounds.getHeight()/16));
            laneLabels[static_cast<size_t>(row)].setBounds(line.removeFromLeft(labelWidth));line.removeFromLeft(6);
            for(int step=0;step<activeSteps;++step)
            {
                steps[static_cast<size_t>(row)][static_cast<size_t>(step)].setBounds(line.removeFromLeft(cellWidth));
                line.removeFromLeft(4);
            }
        }
    }
    else if(page==3){auto top=bounds.removeFromTop(36);profileSelect.setBounds(top.removeFromLeft(210));top.removeFromLeft(8);profileName.setBounds(top.removeFromLeft(190));top.removeFromLeft(8);loadProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);saveProfileButton.setBounds(top.removeFromLeft(105));top.removeFromLeft(6);importCsvButton.setBounds(top.removeFromLeft(95));top.removeFromLeft(6);exportCsvButton.setBounds(top.removeFromLeft(95));bounds.removeFromTop(12);auto edit=bounds.removeFromTop(36);mappingInstrument.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingArticulation.setBounds(edit.removeFromLeft(150));edit.removeFromLeft(8);mappingNote.setBounds(edit.removeFromLeft(110));edit.removeFromLeft(8);setMappingButton.setBounds(edit.removeFromLeft(120));edit.removeFromLeft(8);removeMappingButton.setBounds(edit.removeFromLeft(130));bounds.removeFromTop(14);mappingSummary.setBounds(bounds);}
    else
    {
        auto row = bounds.removeFromTop(36);
        const bool compactPluginHeader = row.getWidth() < 1000;
        pluginInstrumentSelect.setBounds(row.removeFromLeft(300));
        row.removeFromLeft(8);
        refreshPluginInventoryButton.setBounds(row.removeFromLeft(142));
        row.removeFromLeft(6);
        scanPluginInventoryButton.setBounds(row.removeFromLeft(155));
        pluginInventoryProgress.setBounds(row.reduced(8, 11));
        if (compactPluginHeader)
        {
            bounds.removeFromTop(6);
            row = bounds.removeFromTop(36);
        }
        row.removeFromLeft(6);
        choosePluginFolderButton.setBounds(row.removeFromLeft(170));
        row.removeFromLeft(6);
        openPluginManagerButton.setBounds(row.removeFromLeft(180));
        bounds.removeFromTop(8);row=bounds.removeFromTop(36);pluginKitSelect.setBounds(row.removeFromLeft(270));row.removeFromLeft(8);pluginPresetKitName.setBounds(row.removeFromLeft(235));row.removeFromLeft(8);choosePluginPresetButton.setBounds(row.removeFromLeft(165));row.removeFromLeft(6);importPluginPresetButton.setBounds(row.removeFromLeft(175));bounds.removeFromTop(8);row=bounds.removeFromTop(36);pluginProgramSelect.setBounds(row.removeFromLeft(270));row.removeFromLeft(8);scanPluginProgramsButton.setBounds(row.removeFromLeft(150));row.removeFromLeft(8);importPluginProgramButton.setBounds(row.removeFromLeft(190));row.removeFromLeft(12);pluginRenderMode.setBounds(row.removeFromLeft(200));bounds.removeFromTop(8);auto labels=bounds.removeFromTop(18);pluginVelocityLabel.setBounds(labels.removeFromLeft(250));labels.removeFromLeft(8);pluginNoteDurationLabel.setBounds(labels.removeFromLeft(118));labels.removeFromLeft(8);pluginReleaseLabel.setBounds(labels.removeFromLeft(118));labels.removeFromLeft(8);pluginCaptureLabel.setBounds(labels.removeFromLeft(118));labels.removeFromLeft(8);pluginRepeatLabel.setBounds(labels.removeFromLeft(90));row=bounds.removeFromTop(36);pluginVelocities.setBounds(row.removeFromLeft(250));row.removeFromLeft(8);pluginNoteDuration.setBounds(row.removeFromLeft(118));row.removeFromLeft(8);pluginRelease.setBounds(row.removeFromLeft(118));row.removeFromLeft(8);pluginCaptureLength.setBounds(row.removeFromLeft(118));row.removeFromLeft(8);pluginRepeatCount.setBounds(row.removeFromLeft(90));bounds.removeFromTop(10);row=bounds.removeFromTop(36);scanPluginNotesButton.setBounds(row.removeFromLeft(160));row.removeFromLeft(8);cancelPluginNotesButton.setBounds(row.removeFromLeft(140));row.removeFromLeft(12);pluginScanProgress.setBounds(row);bounds.removeFromTop(12);pluginScanInfo.setBounds(bounds);
    }
}

void MagDrumAudioProcessorEditor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &magdrum::ui::languageChangeBroadcaster())
        applyLanguage();
    else if (processor.isSampleStateChangeSource(source))
    {
        updateLayerSummary();
        refreshSampleAssignmentLabels();
    }
}

void MagDrumAudioProcessorEditor::applyLanguage()
{
    libraryTab.setButtonText(magdrum::ui::localisedText("SAMPLE LIBRARY"));machineTab.setButtonText(magdrum::ui::localisedText("DRUM MACHINE"));sequencerTab.setButtonText(magdrum::ui::localisedText("SEQUENCER"));mapsTab.setButtonText(magdrum::ui::localisedText("MIDI MAPS"));pluginScannerTab.setButtonText(magdrum::ui::localisedText("PLUGIN SCANNER"));manageLayersButton.setButtonText(magdrum::ui::localisedText("Layers"));manageLayersButton.setTooltip(magdrum::ui::localisedText("Manage velocity layers for the selected pad."));languageButton.setTooltip(magdrum::ui::localisedText("Menu"));
    sampleSearch.setTextToShowWhenEmpty(magdrum::ui::localisedText("Search indexed samples"),Palette::Muted);
    const int contentTypeId = juce::jlimit(1, 8, contentTypeFilter.getSelectedId());
    contentTypeFilter.clear(juce::dontSendNotification);
    contentTypeFilter.addItem(magdrum::ui::localisedText("All sounds"), 1);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Drum one-shots"), 2);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Drum loops & beats"), 3);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Bass one-shots"), 4);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Bass loops & beats"), 5);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Synth one-shots"), 6);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Synth loops"), 7);
    contentTypeFilter.addItem(magdrum::ui::localisedText("Other loops"), 8);
    contentTypeFilter.setSelectedId(contentTypeId, juce::dontSendNotification);
    const int fallbackPolicyId=juce::jlimit(1,3,midiFallbackPolicy.getSelectedId());
    midiFallbackPolicy.clear(juce::dontSendNotification);
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("None"),1);
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("Default articulation only"),2);
    midiFallbackPolicy.addItem(magdrum::ui::localisedText("Any same-instrument articulation"),3);
    midiFallbackPolicy.setSelectedId(fallbackPolicyId,juce::dontSendNotification);
    midiFallbackPolicy.setTooltip(magdrum::ui::localisedText("Choose how unsupported articulations map during MIDI export. Every substitution is listed after export."));
    refreshPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Refresh instruments"));
    scanPluginInventoryButton.setButtonText(magdrum::ui::localisedText(pluginInventoryScanRunning ? "Cancel instrument scan" : "Scan plugin folders"));
    choosePluginFolderButton.setButtonText(magdrum::ui::localisedText("Choose VST3 folder"));
    openPluginManagerButton.setButtonText(magdrum::ui::localisedText("Open instrument manager"));
    choosePluginPresetButton.setButtonText(magdrum::ui::localisedText("Choose VST3 preset"));
    importPluginPresetButton.setButtonText(magdrum::ui::localisedText(pluginPresetImportRunning && ! pluginPresetImportFromProgram ? "Cancel preset import" : "Import preset as kit"));
    scanPluginProgramsButton.setButtonText(magdrum::ui::localisedText(pluginProgramScanRunning ? "Cancel program scan" : "Scan programs"));
    importPluginProgramButton.setButtonText(magdrum::ui::localisedText(pluginPresetImportRunning && pluginPresetImportFromProgram ? "Cancel preset import" : "Import program as kit"));
    pluginProgramSelect.setTextWhenNothingSelected(magdrum::ui::localisedText("Instrument programs"));
    pluginPresetKitName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Preset kit name"),Palette::Muted);
    scanPluginNotesButton.setButtonText(magdrum::ui::localisedText("Scan kit notes"));
    cancelPluginNotesButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));
    pluginVelocityLabel.setText(magdrum::ui::localisedText("Velocity levels"),juce::dontSendNotification);
    pluginNoteDurationLabel.setText(magdrum::ui::localisedText("Note seconds"),juce::dontSendNotification);
    pluginReleaseLabel.setText(magdrum::ui::localisedText("Release seconds"),juce::dontSendNotification);
    pluginCaptureLabel.setText(magdrum::ui::localisedText("Capture seconds"),juce::dontSendNotification);
    pluginRepeatLabel.setText(magdrum::ui::localisedText("Repeats"),juce::dontSendNotification);
    pluginVelocities.setTextToShowWhenEmpty(magdrum::ui::localisedText("Velocity levels"),Palette::Muted);
    pluginNoteDuration.setTextToShowWhenEmpty(magdrum::ui::localisedText("Note seconds"),Palette::Muted);
    pluginRelease.setTextToShowWhenEmpty(magdrum::ui::localisedText("Release seconds"),Palette::Muted);
    pluginCaptureLength.setTextToShowWhenEmpty(magdrum::ui::localisedText("Capture seconds"),Palette::Muted);
    pluginRepeatCount.setTextToShowWhenEmpty(magdrum::ui::localisedText("Repeats"),Palette::Muted);
    const int renderModeId=pluginRenderMode.getSelectedId();
    pluginRenderMode.clear(juce::dontSendNotification);
    pluginRenderMode.addItem(magdrum::ui::localisedText("Offline"),1);
    pluginRenderMode.addItem(magdrum::ui::localisedText("Real-time paced"),2);
    pluginRenderMode.addItem(magdrum::ui::localisedText("Audio device"),3);
    pluginRenderMode.setSelectedId(renderModeId,juce::dontSendNotification);
    pluginScanInfo.setText(discoveredPluginInstruments.isEmpty()
        ? magdrum::ui::localisedText("No discovered VST3 instruments. Scan plugin folders above to find instruments.")
        : magdrum::ui::localisedText("Select a discovered instrument, import a preset as a kit, then scan its MIDI notes."),juce::dontSendNotification);
    searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));stopPreviewButton.setButtonText(magdrum::ui::localisedText("Stop"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));
    libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));
    clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load sample"));
    chokeGroupLabel.setText(magdrum::ui::localisedText("CHOKE"), juce::dontSendNotification);
    chokeGroupSelect.setTooltip(magdrum::ui::localisedText(
        "Pads in the same choke group stop one another when triggered."));
    const int selectedChokeGroup = chokeGroupSelect.getSelectedId();
    chokeGroupSelect.clear(juce::dontSendNotification);
    chokeGroupSelect.addItem(magdrum::ui::localisedText("None"), 1);
    for (int group = 1; group <= 16; ++group)
        chokeGroupSelect.addItem(juce::String(group), group + 1);
    chokeGroupSelect.setSelectedId(selectedChokeGroup, juce::dontSendNotification);
    gainLabel.setText(magdrum::ui::localisedText("PAD GAIN"),juce::dontSendNotification);
    panLabel.setText(magdrum::ui::localisedText("PAD PAN"),juce::dontSendNotification);
    tuneLabel.setText(magdrum::ui::localisedText("PAD TUNE"),juce::dontSendNotification);
    driveLabel.setText(magdrum::ui::localisedText("PAD DRIVE"),juce::dontSendNotification);
    attackLabel.setText(magdrum::ui::localisedText("PAD ATTACK"),juce::dontSendNotification);
    decayLabel.setText(magdrum::ui::localisedText("PAD DECAY"),juce::dontSendNotification);
    sustainLabel.setText(magdrum::ui::localisedText("PAD SUSTAIN"),juce::dontSendNotification);
    releaseLabel.setText(magdrum::ui::localisedText("PAD RELEASE"),juce::dontSendNotification);
    filterCutoffLabel.setText(magdrum::ui::localisedText("FILTER CUTOFF"),juce::dontSendNotification);
    filterResonanceLabel.setText(magdrum::ui::localisedText("FILTER Q"),juce::dontSendNotification);
    filterResonanceSlider.setTooltip(magdrum::ui::localisedText(
        "Filter Q controls resonance. Higher Q emphasizes frequencies near the cutoff."));
    eqLowLabel.setText(magdrum::ui::localisedText("EQ LOW"),juce::dontSendNotification);
    eqMidLabel.setText(magdrum::ui::localisedText("EQ MID"),juce::dontSendNotification);
    eqHighLabel.setText(magdrum::ui::localisedText("EQ HIGH"),juce::dontSendNotification);
    compressorThresholdLabel.setText(magdrum::ui::localisedText("COMP THRESHOLD"),juce::dontSendNotification);
    compressorRatioLabel.setText(magdrum::ui::localisedText("COMP RATIO"),juce::dontSendNotification);
    compressorAttackLabel.setText(magdrum::ui::localisedText("COMP ATTACK"),juce::dontSendNotification);
    compressorReleaseLabel.setText(magdrum::ui::localisedText("COMP RELEASE"),juce::dontSendNotification);
    compressorMakeupLabel.setText(magdrum::ui::localisedText("COMP MAKEUP"),juce::dontSendNotification);
    phaserRateLabel.setText(magdrum::ui::localisedText("PHASER RATE"),juce::dontSendNotification);
    phaserDepthLabel.setText(magdrum::ui::localisedText("PHASER DEPTH"),juce::dontSendNotification);
    phaserFeedbackLabel.setText(magdrum::ui::localisedText("PHASER FEEDBACK"),juce::dontSendNotification);
    phaserMixLabel.setText(magdrum::ui::localisedText("PHASER MIX"),juce::dontSendNotification);
    padMixHeading.setText(magdrum::ui::localisedText("PAD MIX"),juce::dontSendNotification);
    padShapeHeading.setText(magdrum::ui::localisedText("ENVELOPE / FILTER"),juce::dontSendNotification);
    padEqHeading.setText(magdrum::ui::localisedText("PAD EQUALIZER"),juce::dontSendNotification);
    padFxHeading.setText(magdrum::ui::localisedText("COMPRESSOR / PHASER"),juce::dontSendNotification);
    compressorTab.setButtonText(magdrum::ui::localisedText("COMPRESSOR"));
    phaserTab.setButtonText(magdrum::ui::localisedText("PHASER"));
    playSequenceButton.setButtonText(magdrum::ui::localisedText(sequencerPlaying ? "Stop" : "Play"));playSequenceButton.setTooltip(magdrum::ui::localisedText("While the host transport is playing, the sequencer follows its tempo and position."));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));
    importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));
    setMetadataButton.setButtonText(magdrum::ui::localisedText("Set event"));removeMetadataButton.setButtonText(magdrum::ui::localisedText("Remove event"));
    loadProfileButton.setButtonText(magdrum::ui::localisedText("Load profile"));saveProfileButton.setButtonText(magdrum::ui::localisedText("Save profile"));importCsvButton.setButtonText(magdrum::ui::localisedText("Import CSV"));exportCsvButton.setButtonText(magdrum::ui::localisedText("Export CSV"));setMappingButton.setButtonText(magdrum::ui::localisedText("Set mapping"));removeMappingButton.setButtonText(magdrum::ui::localisedText("Remove mapping"));
    instrumentScannerButton.setTooltip(magdrum::ui::localisedText("Scan plugin folders"));
    openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));
    openSourceKitButton.setTooltip(magdrum::ui::localisedText("Open the selected sample's source instrument and recall its saved kit in a separate MagDrum window."));
    scanDefaultsButton.setTooltip(magdrum::ui::localisedText("Scan audio files in common Native Instruments, Spectrasonics and Steinberg library locations."));
    velocityRange.setTooltip(magdrum::ui::localisedText("Inclusive velocity range for this sample layer (1–127)."));
    barSelect.setTooltip(magdrum::ui::localisedText("Select a bar in the current pattern."));
    lengthSelect.setTooltip(magdrum::ui::localisedText("Pattern length in bars (1–16)."));
    meterSelect.setTooltip(magdrum::ui::localisedText("Time signature"));
    gridResolutionLabel.setText(magdrum::ui::localisedText("Step grid"),juce::dontSendNotification);
    gridResolutionSelect.setTooltip(magdrum::ui::localisedText("Step grid"));
    browseMidiButton.setTooltip(magdrum::ui::localisedText("Search the detected default libraries for drum-related MIDI files."));
    editMidiEventsButton.setTooltip(magdrum::ui::localisedText("Edit individual hits, including articulation, off-grid beat position and duration."));
    reviewMidiButton.setTooltip(magdrum::ui::localisedText("Compare the imported MIDI pattern with your current sequencer edits."));
    tempoEditor.setTooltip(magdrum::ui::localisedText("Pattern preview and MIDI export tempo."));
    metadataType.setTooltip(magdrum::ui::localisedText("Choose the MIDI metadata event to edit."));
    metadataBeat.setTooltip(magdrum::ui::localisedText("Event position in quarter-note beats from the start of the MIDI pattern."));
    metadataValue.setTooltip(magdrum::ui::localisedText("Tempo in BPM, or a time signature such as 4/4."));
    metadataSummary.setTooltip(magdrum::ui::localisedText("Tempo and time-signature events stored with this sequencer pattern."));
    scanButton.setTooltip(magdrum::ui::localisedText("Add sample folder"));searchButton.setTooltip(magdrum::ui::localisedText("Search indexed samples"));previewButton.setTooltip(magdrum::ui::localisedText("Preview selected sample"));assignButton.setTooltip(magdrum::ui::localisedText("Assign selected sample to the selected pad"));
    libraryToolsButton.setTooltip(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setTooltip(magdrum::ui::localisedText("More samples"));clearPadButton.setTooltip(magdrum::ui::localisedText("Clear pad"));loadButton.setTooltip(magdrum::ui::localisedText("Load sample"));
    profileName.setTextToShowWhenEmpty(magdrum::ui::localisedText("Mapping profile name"),Palette::Muted);
    mappingNote.setTextToShowWhenEmpty(magdrum::ui::localisedText("MIDI 0-127"),Palette::Muted);
    const int selectedCategory=categoryFilter.getSelectedId(),selectedFeature=featureFilter.getSelectedId(),selectedSort=sortFilter.getSelectedId(),selectedConfidence=confidenceFilter.getSelectedId(),selectedLength=lengthSelect.getSelectedId();
    const char* categories[]{"All sounds","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Percussion","Unknown"};categoryFilter.clear(juce::dontSendNotification);for(int i=0;i<12;++i)categoryFilter.addItem(magdrum::ui::localisedText(categories[i]),i+1);categoryFilter.setSelectedId(selectedCategory,juce::dontSendNotification);
    const char* features[]{"All sound features","Very short","Short","Medium","Long","Dark","Mid brightness","Bright"};featureFilter.clear(juce::dontSendNotification);for(int i=0;i<8;++i)featureFilter.addItem(magdrum::ui::localisedText(features[i]),i+1);featureFilter.setSelectedId(selectedFeature,juce::dontSendNotification);
    const char* sorts[]{"Name A-Z","Duration shortest","Duration longest","Brightest first","Darkest first"};sortFilter.clear(juce::dontSendNotification);for(int i=0;i<5;++i)sortFilter.addItem(magdrum::ui::localisedText(sorts[i]),i+1);sortFilter.setSelectedId(selectedSort,juce::dontSendNotification);
    confidenceFilter.clear(juce::dontSendNotification);confidenceFilter.addItem(magdrum::ui::localisedText("All confidence levels"),1);confidenceFilter.addItem(magdrum::ui::localisedText("Needs review"),2);confidenceFilter.setSelectedId(selectedConfidence,juce::dontSendNotification);
    lengthSelect.clear(juce::dontSendNotification);for(int bars=1;bars<=magdrum::sequencer::maximumBars;++bars)lengthSelect.addItem(juce::String(bars)+" "+magdrum::ui::localisedText(bars==1?"bar":"bars"),bars);lengthSelect.setSelectedId(selectedLength,juce::dontSendNotification);
    const bool sampleScanCancellationRequested=scanCancellation&&scanCancellation->load(std::memory_order_acquire);
    const bool midiSearchCancellationRequested=defaultMidiSearchCancellation&&defaultMidiSearchCancellation->load(std::memory_order_acquire);
    const auto scanButtonLabel=scanningDefaultLibraries?"Add sample folder":sampleScanCancellationRequested?"Cancelling…":scanning?"Cancel scan":lastSampleScanFolder.isDirectory()?"Resume scan":"Add sample folder";
    const auto defaultScanLabel=scanningDefaultLibraries?(sampleScanCancellationRequested||midiSearchCancellationRequested?"Cancelling…":scanning||defaultMidiSearchCancellation?"Cancel scan":"Searching MIDI…"):(defaultLibraryScanCanResume?"Resume scan":"Scan installed libraries");
    scanButton.setButtonText(magdrum::ui::localisedText(scanButtonLabel));scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Scan installed libraries"));instrumentScannerButton.setButtonText(magdrum::ui::localisedText("Plugin instruments"));openSourceKitButton.setButtonText(magdrum::ui::localisedText("Open source kit"));searchButton.setButtonText(magdrum::ui::localisedText("Search"));previewButton.setButtonText(magdrum::ui::localisedText("Preview"));stopPreviewButton.setButtonText(magdrum::ui::localisedText("Stop"));assignButton.setButtonText(magdrum::ui::localisedText("Assign to pad"));libraryToolsButton.setButtonText(magdrum::ui::localisedText("Sample tools"));moreSamplesButton.setButtonText(magdrum::ui::localisedText("More samples"));clearPadButton.setButtonText(magdrum::ui::localisedText("Clear pad"));loadButton.setButtonText(magdrum::ui::localisedText("Load sample"));playSequenceButton.setButtonText(magdrum::ui::localisedText(sequencerPlaying?"Stop":"Play"));clearSequenceButton.setButtonText(magdrum::ui::localisedText("Clear pattern"));exportSequenceButton.setButtonText(magdrum::ui::localisedText("Export MIDI"));importMidiButton.setButtonText(magdrum::ui::localisedText("Import MIDI"));browseMidiButton.setButtonText(defaultMidiPatterns.empty()?magdrum::ui::localisedText("Find drum MIDI"):magdrum::ui::localisedText("Drum MIDI (")+juce::String(static_cast<int>(defaultMidiPatterns.size()))+")");editMidiEventsButton.setButtonText(magdrum::ui::localisedText("Edit hits"));reviewMidiButton.setButtonText(magdrum::ui::localisedText("Review changes"));
    scanDefaultsButton.setButtonText(magdrum::ui::localisedText(defaultScanLabel));
    for(size_t i=0;i<pads.size();++i)pads[i].setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));
    for(size_t row=0;row<laneLabels.size();++row)laneLabels[row].setText(localizedSequencerLaneName(row),juce::dontSendNotification);
    setMetadataButton.setButtonText(magdrum::ui::localisedText("Set event"));removeMetadataButton.setButtonText(magdrum::ui::localisedText("Remove event"));loadProfileButton.setButtonText(magdrum::ui::localisedText("Load profile"));saveProfileButton.setButtonText(magdrum::ui::localisedText("Save profile"));importCsvButton.setButtonText(magdrum::ui::localisedText("Import CSV"));exportCsvButton.setButtonText(magdrum::ui::localisedText("Export CSV"));setMappingButton.setButtonText(magdrum::ui::localisedText("Set mapping"));removeMappingButton.setButtonText(magdrum::ui::localisedText("Remove mapping"));
    const int selectedMetadata=metadataType.getSelectedId();metadataType.clear(juce::dontSendNotification);metadataType.addItem(magdrum::ui::localisedText("Tempo event"),1);metadataType.addItem(magdrum::ui::localisedText("Time signature"),2);metadataType.setSelectedId(selectedMetadata,juce::dontSendNotification);
    const int selectedInstrument=mappingInstrument.getSelectedId(),selectedArticulation=mappingArticulation.getSelectedId();const char* instruments[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};mappingInstrument.clear(juce::dontSendNotification);for(int i=0;i<9;++i)mappingInstrument.addItem(magdrum::ui::localisedText(instruments[i]),i+1);mappingInstrument.setSelectedId(selectedInstrument,juce::dontSendNotification);const char* articulations[]{"Default","Center","Rimshot","Side stick","Closed","Open","Pedal","Low","Mid","High","Bell","Bow","Edge"};mappingArticulation.clear(juce::dontSendNotification);for(int i=0;i<13;++i)mappingArticulation.addItem(magdrum::ui::localisedText(articulations[i]),i+1);mappingArticulation.setSelectedId(selectedArticulation,juce::dontSendNotification);
    if(comparisonWindow)comparisonWindow->setName(magdrum::ui::localisedText("Compare drum samples"));
    if(midiEventWindow)midiEventWindow->setName(magdrum::ui::localisedText("Edit MIDI events"));
    status.setText(juce::String(),juce::dontSendNotification);refreshSequenceMetadata();refreshMappingSummary();updateLayerSummary();setNote(selectedNote);
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

int MagDrumAudioProcessorEditor::gridResolutionSubdivisions() const
{
    const int selected=gridResolutionSelect.getSelectedId();
    return magdrum::sequencer::isSupportedSubdivision(selected)?selected:4;
}

int MagDrumAudioProcessorEditor::patternStepCount() const
{
    return currentBarMap().totalSteps;
}

magdrum::sequencer::BarMap MagDrumAudioProcessorEditor::currentBarMap() const
{
    const int bars=juce::jlimit(1,magdrum::sequencer::maximumBars,
                                lengthSelect.getSelectedId()>0?lengthSelect.getSelectedId():magdrum::sequencer::maximumBars);
    auto map=makeSequencerBarMap(timeSignatureNumerator(),timeSignatureDenominator(),bars,sequenceMetadata,gridResolutionSubdivisions());
    if(!map.valid)
        map=magdrum::sequencer::buildBarMap(timeSignatureNumerator(),timeSignatureDenominator(),bars,{},gridResolutionSubdivisions());
    return map;
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
    const bool signatureEvent=!tempoEvent;
    int openingNumerator=timeSignatureNumerator(),openingDenominator=timeSignatureDenominator();
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
            openingNumerator=numerator;openingDenominator=denominator;
        }
    }
    if(signatureEvent)
    {
        const auto proposedMap=makeSequencerBarMap(openingNumerator,openingDenominator,previousPatternBars,edited.tempoAndSignature,gridResolutionSubdivisions());
        if(!proposedMap.valid){status.setText(magdrum::ui::localisedText("This step grid cannot represent the current pattern."),juce::dontSendNotification);return false;}
        for(const auto& row:sequenceStepActive)for(size_t index=static_cast<size_t>(proposedMap.totalSteps);index<row.size();++index)
            if(row[index]){status.setText(magdrum::ui::localisedText("That meter would hide existing hits in the selected pattern length. Move or clear those hits first."),juce::dontSendNotification);return false;}
        for(const auto& event:sequenceEvents)
            if(event.positionBeats>=proposedMap.lengthBeats-magdrum::sequencer::gridBeatTolerance){status.setText(magdrum::ui::localisedText("That meter would hide existing hits in the selected pattern length. Move or clear those hits first."),juce::dontSendNotification);return false;}
        if(!remove&&std::abs(beat)<1.0e-9){
            const int id=openingNumerator*100+openingDenominator;
            meterSelect.setSelectedId(id,juce::dontSendNotification);previousMeterId=id;
        }
    }
    sequenceMetadata=std::move(edited.tempoAndSignature);
    if(signatureEvent)updateSequencerGrid();
    refreshSequenceMetadata();
    savePluginEditorState();
    if(remove)status.setText(magdrum::ui::localisedText("MIDI metadata removed at beat ")+juce::String(beat,3)+".",juce::dontSendNotification);
    else status.setText(magdrum::ui::localisedText(tempoEvent?"Tempo event saved at beat ":"Time-signature event saved at beat ")+juce::String(beat,3)+".",juce::dontSendNotification);
    return true;
}

MidiPattern MagDrumAudioProcessorEditor::currentSequencePattern() const
{
    MidiPattern pattern;
    pattern.lengthBeats=static_cast<double>(patternStepCount())/gridResolutionSubdivisions();
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
    for(int row=0;row<static_cast<int>(steps.size());++row)
        for(int step=0;step<patternStepCount();++step)
            if(sequenceStepActive[static_cast<size_t>(row)][static_cast<size_t>(step)])
                sequenceEvents.push_back({laneInstruments[static_cast<size_t>(row)],laneArticulations[static_cast<size_t>(row)],static_cast<std::uint8_t>(stepVelocities[static_cast<size_t>(row)][static_cast<size_t>(step)]),static_cast<double>(step)/gridResolutionSubdivisions(),0.12,mappedLaneNotes[static_cast<size_t>(row)]});
    sequenceEventsInitialised=true;
}

bool MagDrumAudioProcessorEditor::syncSequenceEventForCell(int lane,int step)
{
    if(!juce::isPositiveAndBelow(lane,static_cast<int>(steps.size()))||!juce::isPositiveAndBelow(step,patternStepCount()))return false;
    const int subdivisions=gridResolutionSubdivisions();
    const auto hasOffGridHit = [this,lane,step,subdivisions](const DrumEvent& event)
    {
        return std::isfinite(event.positionBeats)
            && sequencerLaneForEvent(event,mappedLaneNotes) == lane
            && juce::roundToInt(event.positionBeats * subdivisions) == step
            && !magdrum::sequencer::isPositionOnGridStep(event.positionBeats, step, subdivisions);
    };
    const bool preservesOffGridHit = std::any_of(sequenceEvents.begin(),sequenceEvents.end(),hasOffGridHit);
    const auto mapsToCell=[this,lane,step,subdivisions](const DrumEvent& event)
    {
        return sequencerLaneForEvent(event,mappedLaneNotes) == lane
            && magdrum::sequencer::isPositionOnGridStep(event.positionBeats, step, subdivisions);
    };
    sequenceEvents.erase(std::remove_if(sequenceEvents.begin(),sequenceEvents.end(),mapsToCell),sequenceEvents.end());
    if(sequenceStepActive[static_cast<size_t>(lane)][static_cast<size_t>(step)])
        sequenceEvents.push_back({laneInstruments[static_cast<size_t>(lane)],laneArticulations[static_cast<size_t>(lane)],static_cast<std::uint8_t>(stepVelocities[static_cast<size_t>(lane)][static_cast<size_t>(step)]),static_cast<double>(step)/subdivisions,0.12,mappedLaneNotes[static_cast<size_t>(lane)]});
    std::stable_sort(sequenceEvents.begin(),sequenceEvents.end(),[](const auto& left,const auto& right){return left.positionBeats<right.positionBeats;});
    sequenceEventsInitialised=true;
    return preservesOffGridHit;
}

void MagDrumAudioProcessorEditor::syncSequencerGridFromEvents()
{
    for(auto& row:sequenceStepActive)row.fill(false);
    for(auto& row:stepVelocities)row.fill(100);
    for(const auto& event:sequenceEvents)
    {
        const int lane = sequencerLaneForEvent(event,mappedLaneNotes);
        const int step=juce::roundToInt(event.positionBeats*gridResolutionSubdivisions());
        if(!juce::isPositiveAndBelow(lane,static_cast<int>(steps.size()))||!juce::isPositiveAndBelow(step,patternStepCount()))continue;
        const auto row=static_cast<size_t>(lane),column=static_cast<size_t>(step);sequenceStepActive[row][column]=true;stepVelocities[row][column]=event.velocity;
    }
    refreshStepTooltips();resized();repaint();
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

void MagDrumAudioProcessorEditor::openSampleComparisonWindow(const juce::File& first, const juce::File& second)
{
    if(first==juce::File()||second==juce::File())return;
    comparisonWindow=std::make_unique<MagDrumSampleComparisonWindow>(first,second,
        [this](const juce::File& file){previewSampleAsync(file);},[this]{stopSamplePreview();});
}

void MagDrumAudioProcessorEditor::reviewMidiChanges()
{
    if(!hasImportedMidiBaseline){status.setText(magdrum::ui::localisedText("Import a MIDI pattern before reviewing changes."),juce::dontSendNotification);return;}
    const auto diff=compareMidiPatterns(importedMidiBaseline,currentSequencePattern());
    juce::String details=magdrum::ui::localisedText("Compared with the imported pattern.")+"\n\n";
    details+=magdrum::ui::localisedText("Removed hits: ")+juce::String(static_cast<int>(diff.removedEvents.size()))+"\n"+magdrum::ui::localisedText("Added hits: ")+juce::String(static_cast<int>(diff.addedEvents.size()))
        +"\n"+magdrum::ui::localisedText("Removed metadata events: ")+juce::String(static_cast<int>(diff.removedMetadata.size()))+"\n"+magdrum::ui::localisedText("Added metadata events: ")+juce::String(static_cast<int>(diff.addedMetadata.size()))
        +"\n"+magdrum::ui::localisedText("Pattern length changed: ")+magdrum::ui::localisedText(diff.lengthChanged?"Yes":"No");
    const auto describeEvent=[](const DrumEvent& event)
    {
        static constexpr const char* names[]{"Kick","Snare","Hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};
        const auto index=static_cast<size_t>(event.instrument);
        const auto name=index<9?names[index]:"Drum";
        return magdrum::ui::localisedText(name)+" · "+magdrum::ui::localisedText("Beat")+" "+juce::String(event.positionBeats,2)+" · "+magdrum::ui::localisedText("Velocity")+" "+juce::String(event.velocity);
    };
    for(const auto& event:diff.removedEvents)details+="\n− "+describeEvent(event);
    for(const auto& event:diff.addedEvents)details+="\n+ "+describeEvent(event);
    for(const auto& meta:diff.removedMetadata)
        details+="\n− "+magdrum::ui::localisedText(meta.message.isTempoMetaEvent()?"Tempo":"Time signature")+" · "+magdrum::ui::localisedText("metadata @ beat")+" "+juce::String(meta.positionBeats,2);
    for(const auto& meta:diff.addedMetadata)
        details+="\n+ "+magdrum::ui::localisedText(meta.message.isTempoMetaEvent()?"Tempo":"Time signature")+" · "+magdrum::ui::localisedText("metadata @ beat")+" "+juce::String(meta.positionBeats,2);
    if(diff.removedEvents.empty()&&diff.addedEvents.empty()&&diff.removedMetadata.empty()&&diff.addedMetadata.empty()&&!diff.lengthChanged)
        details+="\n\n"+magdrum::ui::localisedText("No changes since import.");
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,magdrum::ui::localisedText("MIDI pattern changes"),details);
}

void MagDrumAudioProcessorEditor::updateSequencerGrid()
{
    const int bars=juce::jlimit(1,magdrum::sequencer::maximumBars,lengthSelect.getSelectedId()>0?lengthSelect.getSelectedId():magdrum::sequencer::maximumBars);
    const int selectedBar=barSelect.getSelectedId()>0?barSelect.getSelectedId():1;
    barSelect.clear(juce::dontSendNotification);
    for(int bar=1;bar<=bars;++bar)barSelect.addItem(magdrum::ui::localisedText("Bar ")+juce::String(bar),bar);
    barSelect.setSelectedId(juce::jlimit(1,bars,selectedBar),juce::dontSendNotification);
    refreshStepTooltips();
    resized();
    repaint();
}

int MagDrumAudioProcessorEditor::visibleStepIndex(int localStep) const
{
    const auto map=currentBarMap();
    const int selectedBar=juce::jlimit(1,map.barCount,barSelect.getSelectedId()>0?barSelect.getSelectedId():1);
    const auto& span=map.bars[static_cast<size_t>(selectedBar-1)];
    if(localStep<0||localStep>=span.stepCount)return -1;
    return span.startStep+localStep;
}

void MagDrumAudioProcessorEditor::refreshStepTooltips()
{
    const auto map=currentBarMap();
    const int selectedBar=juce::jlimit(1,map.barCount,barSelect.getSelectedId()>0?barSelect.getSelectedId():1);
    const auto& span=map.bars[static_cast<size_t>(selectedBar-1)];
    for(size_t lane=0;lane<steps.size();++lane)
        for(size_t localStep=0;localStep<steps[lane].size();++localStep)
        {
            auto& cell=steps[lane][localStep];
            if(localStep>=static_cast<size_t>(span.stepCount)){cell.setVisible(false);continue;}
            const auto globalStep=static_cast<size_t>(span.startStep)+localStep;
            cell.setVisible(page==2);
            cell.setToggleState(sequenceStepActive[lane][globalStep],juce::dontSendNotification);
            cell.setColour(juce::TextButton::buttonOnColourId,categoryColour(laneNames[lane]).withAlpha(static_cast<float>(stepVelocities[lane][globalStep])/127.0f));
            const auto midiNoteText=mappedLaneNotes[lane]>=0?"MIDI "+juce::String(mappedLaneNotes[lane]):magdrum::ui::localisedText("No MIDI mapping");
            const auto tooltip=localizedSequencerLaneName(lane)+" - "+midiNoteText+" - "+magdrum::ui::localisedText("bar")+" "+juce::String(selectedBar)+" - "+magdrum::ui::localisedText("step")+" "+juce::String(localStep+1)+" - "+magdrum::ui::localisedText("Velocity")+" "+juce::String(stepVelocities[lane][globalStep])+" ("+magdrum::ui::localisedText("Right-click to edit")+")";
            cell.setTooltip(tooltip);
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
    if(const auto assignment=sampleAssignmentLabels.find(sample.file.getFullPathName().toStdString());assignment!=sampleAssignmentLabels.end())
        info+="  ·  MIDI → "+assignment->second;
    g.setColour(Palette::Muted);g.setFont(10.5f);
    g.drawText(info,18,22,width-24,height-23,juce::Justification::centredLeft,true);
}
void MagDrumAudioProcessorEditor::selectedRowsChanged(int row)
{
    if (juce::isPositiveAndBelow(row, samples.size())) sampleMapSelectedFile = {};
    int mapRow = -1;
    if (juce::isPositiveAndBelow(row, samples.size()))
    {
        const auto& selected = samples.getReference(row).file;
        for (int index = 0; index < sampleMapSamples.size(); ++index)
            if (sampleMapSamples.getReference(index).file == selected) { mapRow = index; break; }
    }
    sampleMap->setSelected(mapRow);
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
void MagDrumAudioProcessorEditor::listBoxItemDoubleClicked(int row,const juce::MouseEvent&){if(juce::isPositiveAndBelow(row,samples.size()))previewSampleAsync(samples.getReference(row).file);}
void MagDrumAudioProcessorEditor::listBoxItemClicked(int row,const juce::MouseEvent& event)
{
    if (!event.mods.isRightButtonDown() || !juce::isPositiveAndBelow(row, samples.size())) return;
    indexedList.selectRow(row);
    juce::PopupMenu menu;
    menu.addItem(1, magdrum::ui::localisedText("Preview"));
    menu.addItem(2, magdrum::ui::localisedText("Assign to pad"));
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&indexedList), [safe, row](int action)
    {
        if (safe == nullptr || !juce::isPositiveAndBelow(row, safe->samples.size())) return;
        const auto file = safe->samples.getReference(row).file;
        if (action == 1) safe->previewSampleAsync(file);
        else if (action == 2)
            safe->loadSampleAsync(file, safe->selectedNote, velocityMinimum(safe->velocityRange.getText()),
                                  velocityMaximum(safe->velocityRange.getText()), true);
    });
}

void MagDrumAudioProcessorEditor::previewSampleAsync(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("Could not read this audio file."), juce::dontSendNotification);
        return;
    }

    const auto generation = samplePreviewGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    status.setText(magdrum::ui::localisedText("Previewing ") + file.getFileName(), juce::dontSendNotification);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    samplePreviewWorkers.addJob(new PluginSamplePreviewJob(processor, file, samplePreviewGeneration, generation,
        [safe, generation](bool previewed)
        {
            if (safe == nullptr || safe->samplePreviewGeneration.load(std::memory_order_acquire) != generation)
                return;
            if (!previewed)
                safe->status.setText(magdrum::ui::localisedText("Could not read this audio file."), juce::dontSendNotification);
        }), true);
}

void MagDrumAudioProcessorEditor::stopSamplePreview()
{
    samplePreviewGeneration.fetch_add(1, std::memory_order_acq_rel);
    processor.stopSamplePreview();
    status.setText(magdrum::ui::localisedText("Preview cancelled."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::loadSampleAsync(const juce::File& file, int note,
                                                   int minimumVelocity, int maximumVelocity,
                                                   bool fromLibrary)
{
    if (!file.existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("Could not assign sample. Check range, format and layer limit."),
                       juce::dontSendNotification);
        return;
    }

    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    sampleLoadWorkers.addJob(new PluginSampleLoadJob(processor, file, note, minimumVelocity, maximumVelocity,
        sampleLoadCancellation,
        [safe, file, note, fromLibrary](bool loaded)
        {
            if (safe == nullptr) return;
            if (safe->selectedNote == note) safe->updateLayerSummary();
            if (!loaded)
            {
                safe->status.setText(fromLibrary
                    ? magdrum::ui::localisedText("Assignment failed. Check velocity ranges and the eight-layer limit.")
                    : magdrum::ui::localisedText("Could not assign sample. Check range, format and layer limit."),
                    juce::dontSendNotification);
                return;
            }
            safe->refreshSampleAssignmentLabels();
            if (!fromLibrary)
            {
                safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
                safe->status.setText(magdrum::ui::localisedText("Loaded ") + file.getFileName(),
                                     juce::dontSendNotification);
                return;
            }
            const auto pad=std::find(safe->mappedPadNotes.begin(),safe->mappedPadNotes.end(),note);
            const auto padName=pad==safe->mappedPadNotes.end()?noteName(note)
                :localizedPadName(static_cast<size_t>(std::distance(safe->mappedPadNotes.begin(),pad)));
            safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
            safe->status.setText(magdrum::ui::localisedText("Assigned ") + file.getFileName()
                + magdrum::ui::localisedText(" to ") + padName + ".",juce::dontSendNotification);
        }), true);
}

void MagDrumAudioProcessorEditor::refreshSamples()
{
    if (!databaseReady || databaseFile == juce::File())
        return;

    const auto requestGeneration = ++sampleBrowseGeneration;
    sampleBrowseQueryPending = true;
    indexedList.setEnabled(false);
    sampleMap->setEnabled(false);
    openSourceKitButton.setEnabled(false);
    previewButton.setEnabled(false);
    assignButton.setEnabled(false);
    libraryToolsButton.setEnabled(false);
    moreSamplesButton.setEnabled(false);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    juce::Timer::callAfterDelay(120, [safe, requestGeneration]
    {
        if (safe != nullptr && safe->sampleBrowseGeneration == requestGeneration)
            safe->loadSamplePage(true, requestGeneration);
    });
}

void MagDrumAudioProcessorEditor::selectSampleMapSample(int index)
{
    if (!juce::isPositiveAndBelow(index, sampleMapSamples.size())) return;
    const auto& record = sampleMapSamples.getReference(index);
    const auto pageRecord = std::find_if(samples.begin(), samples.end(), [&record](const auto& item)
    { return item.file == record.file; });
    if (pageRecord != samples.end())
    {
        sampleMapSelectedFile = {};
        indexedList.selectRow(static_cast<int>(std::distance(samples.begin(), pageRecord)));
        return;
    }
    sampleMapSelectedFile = record.file;
    indexedList.deselectAllRows();
    sampleMap->setSelected(index);
    auto details = magdrum::ui::localisedText(record.category) + " | "
        + juce::String(record.durationSeconds, 2) + " s | "
        + juce::String(static_cast<int>(record.brightnessHz)) + " Hz";
    if (record.pluginName.isNotEmpty())
        details += " | " + record.pluginName + " / " + record.kitName;
    details += "\n" + record.file.getFullPathName();
    sampleDetails.setText(details, juce::dontSendNotification);
    openSourceKitButton.setEnabled(record.pluginIdentifier.isNotEmpty());
    status.setText(record.file.getFileName(), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::refreshSampleAssignmentLabels()
{
    sampleAssignmentLabels.clear();
    std::unordered_map<std::string, int> counts;
    for (const auto& assignment : processor.getAllSampleAssignments())
    {
        const auto path = juce::File(assignment.path).getFullPathName().toStdString();
        auto& count = counts[path];
        if (count < 3)
        {
            auto& label = sampleAssignmentLabels[path];
            if (label.isNotEmpty()) label += " · ";
            label += noteName(assignment.midiNote) + " " + juce::String(assignment.minimumVelocity)
                + "–" + juce::String(assignment.maximumVelocity);
            if (assignment.roundRobinCount > 1)
                label += " · RR " + juce::String(assignment.roundRobinIndex) + "/"
                    + juce::String(assignment.roundRobinCount);
        }
        ++count;
    }
    for (const auto& [path, count] : counts)
        if (count > 3)
            sampleAssignmentLabels[path] += " +" + juce::String(count - 3);
    indexedList.repaint();
}

void MagDrumAudioProcessorEditor::refreshPluginInventory()
{
    const auto inventoryFile = getMagDrumDataDirectory().getChildFile("vst3-plugins.xml");
    discoveredPluginInstruments.clear();
    if (inventoryFile.existsAsFile())
    {
        const auto xml = juce::XmlDocument::parse(inventoryFile);
        if (xml != nullptr)
        {
            juce::KnownPluginList known;
            known.recreateFromXml(*xml);
            for (const auto& description : known.getTypes())
                if (description.isInstrument && description.pluginFormatName == "VST3")
                    discoveredPluginInstruments.add(description);
        }
    }

    const auto previous = pluginInstrumentSelect.getText();
    pluginInstrumentSelect.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& description : discoveredPluginInstruments)
    {
        const auto identifier = description.createIdentifierString();
        pluginInstrumentSelect.addItem(description.name + "  ·  " + description.manufacturerName, id);
        if (previous == description.name + "  ·  " + description.manufacturerName)
            pluginInstrumentSelect.setSelectedId(id, juce::dontSendNotification);
        if (databaseReady)
            database.upsertPluginInstrument({identifier, description.pluginFormatName, description.name,
                description.manufacturerName, description.category, description.fileOrIdentifier, description.version});
        ++id;
    }
    if (pluginInstrumentSelect.getSelectedId() == 0 && pluginInstrumentSelect.getNumItems() > 0)
        pluginInstrumentSelect.setSelectedId(1, juce::dontSendNotification);
    refreshPluginKits();
    pluginScanInfo.setText(discoveredPluginInstruments.isEmpty()
        ? magdrum::ui::localisedText("No discovered VST3 instruments. Scan plugin folders above to find instruments.")
        : magdrum::ui::localisedText("Select a discovered instrument, import a preset as a kit, then scan its MIDI notes."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::choosePluginFolder()
{
    if (pluginInventoryScanRunning)
        return;
    pluginFolderChooser = std::make_unique<juce::FileChooser>(
        magdrum::ui::localisedText("Choose VST3 folder"), juce::File(), juce::String(), true, false, this);
    const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    pluginFolderChooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;
            const auto folder = chooser.getResult();
            if (folder.isDirectory())
                safe->startPluginInventoryScan(folder);
        });
}

void MagDrumAudioProcessorEditor::startPluginInventoryScan(const juce::File& additionalFolder)
{
    if (pluginInventoryScanRunning)
    {
        if (pluginInventoryScanCancellation) pluginInventoryScanCancellation->store(true);
        scanPluginInventoryButton.setEnabled(false);
        choosePluginFolderButton.setEnabled(false);
        return;
    }
    if (! bundledScannerApplication().existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }
    if (additionalFolder != juce::File() && ! additionalFolder.isDirectory())
    {
        status.setText(magdrum::ui::localisedText("The selected VST3 folder is unavailable."), juce::dontSendNotification);
        return;
    }
    pluginInventoryScanCancellation = std::make_shared<std::atomic<bool>>(false);
    pluginInventoryScanRunning = true;
    pluginInventoryProgressValue = -1.0;
    pluginInventoryProgress.repaint();
    pluginInventoryProgressFile = getMagDrumDataDirectory().getChildFile(
        "vst3-scan-progress-" + juce::Uuid().toString() + ".json");
    choosePluginFolderButton.setEnabled(false);
    scanPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Cancel instrument scan"));
    status.setText(magdrum::ui::localisedText("Scanning for VST3 instruments…"), juce::dontSendNotification);
    pollPluginInventoryProgress();
    pluginInventoryWorkers.addJob(new PluginInventoryScanJob(*this, pluginInventoryScanCancellation,
        pluginInventoryProgressFile, additionalFolder), true);
}

void MagDrumAudioProcessorEditor::pollPluginInventoryProgress()
{
    if (!pluginInventoryScanRunning)
        return;

    if (pluginInventoryProgressFile.existsAsFile())
    {
        const auto parsed = juce::JSON::parse(pluginInventoryProgressFile.loadFileAsString());
        if (const auto* progress = parsed.getDynamicObject())
        {
            const int completed = juce::jmax(0, static_cast<int>(progress->getProperty("completed")));
            const int total = juce::jmax(0, static_cast<int>(progress->getProperty("total")));
            const auto currentModule = progress->getProperty("current").toString().trim();
            if (total > 0)
            {
                const int percent = juce::jlimit(0, 100, juce::roundToInt(100.0 * completed / total));
                pluginInventoryProgressValue = static_cast<double>(percent) / 100.0;
                const auto moduleLabel = currentModule == "skipped"
                    ? magdrum::ui::localisedText("Skipping a module that crashed…") : currentModule;
                const auto moduleSuffix = moduleLabel.isNotEmpty() && currentModule != "discovering"
                    && currentModule != "scanning" && currentModule != "complete"
                    ? " — " + moduleLabel : juce::String();
                status.setText(magdrum::ui::localisedText("Scanning for VST3 instruments…") + " "
                    + juce::String(percent) + "% (" + juce::String(completed) + "/"
                    + juce::String(total) + ")" + moduleSuffix, juce::dontSendNotification);
            }
        }
    }

    const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    juce::Timer::callAfterDelay(200, [safe]
    {
        if (safe != nullptr) safe->pollPluginInventoryProgress();
    });
}

void MagDrumAudioProcessorEditor::finishPluginInventoryScan(int exitCode, bool cancelled,
    int skippedAfterCrash)
{
    pluginInventoryScanRunning = false;
    pluginInventoryScanCancellation.reset();
    pluginInventoryProgressValue = exitCode == 0 && !cancelled ? 1.0 : 0.0;
    pluginInventoryProgress.repaint();
    if (pluginInventoryProgressFile.existsAsFile()) pluginInventoryProgressFile.deleteFile();
    pluginInventoryProgressFile = {};
    scanPluginInventoryButton.setEnabled(true);
    scanPluginInventoryButton.setButtonText(magdrum::ui::localisedText("Scan plugin folders"));
    choosePluginFolderButton.setEnabled(true);
    if (cancelled) { status.setText(magdrum::ui::localisedText("VST3 instrument scan cancelled."), juce::dontSendNotification); return; }
    if (exitCode != 0) { status.setText(magdrum::ui::localisedText("VST3 instrument scan failed. Check the MagDrum helper."), juce::dontSendNotification); return; }
    refreshPluginInventory();
    status.setText(skippedAfterCrash > 0
        ? magdrum::ui::localisedText("VST3 instrument scan complete; %1 plug-in(s) skipped after a crash.")
            .replace("%1", juce::String(skippedAfterCrash))
        : magdrum::ui::localisedText("VST3 instrument scan complete."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::choosePluginPreset()
{
    const int index = pluginInstrumentSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(index, discoveredPluginInstruments.size())
        || discoveredPluginInstruments.getReference(index).pluginFormatName != "VST3")
    {
        status.setText(magdrum::ui::localisedText("Select a discovered VST3 instrument first."), juce::dontSendNotification);
        return;
    }

    pluginPresetChooser = std::make_unique<juce::FileChooser>(
        magdrum::ui::localisedText("Choose a VST3 preset"), juce::File(), "*.vstpreset", true, false, this);
    const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    pluginPresetChooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser)
        {
            if (safe == nullptr)
                return;
            const auto file = chooser.getResult();
            if (! file.existsAsFile() || ! file.getFileExtension().equalsIgnoreCase(".vstpreset"))
                return;
            safe->selectedPluginPresetFile = file;
            safe->pluginPresetKitName.setText(file.getFileNameWithoutExtension(), juce::dontSendNotification);
            safe->importPluginPresetButton.setEnabled(! safe->pluginPresetImportRunning);
            safe->status.setText(magdrum::ui::localisedText("Selected VST3 preset: ") + file.getFileName(), juce::dontSendNotification);
        });
}

void MagDrumAudioProcessorEditor::startPluginPresetImport()
{
    if (pluginPresetImportRunning)
    {
        if (pluginPresetImportCancellation)
            pluginPresetImportCancellation->store(true);
        importPluginPresetButton.setEnabled(false);
        return;
    }

    const int index = pluginInstrumentSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(index, discoveredPluginInstruments.size())
        || discoveredPluginInstruments.getReference(index).pluginFormatName != "VST3")
    {
        status.setText(magdrum::ui::localisedText("Select a discovered VST3 instrument first."), juce::dontSendNotification);
        return;
    }
    if (! selectedPluginPresetFile.existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("Choose a .vstpreset file first."), juce::dontSendNotification);
        return;
    }
    const auto name = pluginPresetKitName.getText().trim();
    if (name.isEmpty())
    {
        status.setText(magdrum::ui::localisedText("Enter a name for the preset kit."), juce::dontSendNotification);
        return;
    }
    if (! bundledScannerApplication().existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }

    const auto description = discoveredPluginInstruments.getReference(index);
    const auto identifier = description.createIdentifierString();
    pluginPresetImportCancellation = std::make_shared<std::atomic<bool>>(false);
    pluginPresetImportRunning = true;
    pluginPresetImportFromProgram = false;
    choosePluginPresetButton.setEnabled(false);
    importPluginProgramButton.setEnabled(false);
    importPluginPresetButton.setButtonText(magdrum::ui::localisedText("Cancel preset import"));
    status.setText(magdrum::ui::localisedText("Loading preset in isolated helper…"), juce::dontSendNotification);
    pluginPresetImportWorkers.addJob(new PluginPresetImportJob(*this, description, selectedPluginPresetFile,
        identifier, name, pluginPresetImportCancellation), true);
}

void MagDrumAudioProcessorEditor::startPluginProgramDiscovery()
{
    if (pluginProgramScanRunning)
    {
        if (pluginProgramScanCancellation)
            pluginProgramScanCancellation->store(true);
        scanPluginProgramsButton.setEnabled(false);
        return;
    }
    const int index = pluginInstrumentSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(index, discoveredPluginInstruments.size())
        || discoveredPluginInstruments.getReference(index).pluginFormatName != "VST3")
    {
        status.setText(magdrum::ui::localisedText("Select a discovered VST3 instrument first."), juce::dontSendNotification);
        return;
    }
    if (! bundledScannerApplication().existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }
    const auto description = discoveredPluginInstruments.getReference(index);
    pluginProgramScanIdentifier = description.createIdentifierString();
    pluginProgramSelect.clear(juce::dontSendNotification);
    importPluginProgramButton.setEnabled(false);
    pluginProgramScanCancellation = std::make_shared<std::atomic<bool>>(false);
    pluginProgramScanRunning = true;
    scanPluginProgramsButton.setButtonText(magdrum::ui::localisedText("Cancel program scan"));
    status.setText(magdrum::ui::localisedText("Scanning instrument programs…"), juce::dontSendNotification);
    pluginProgramScanWorkers.addJob(new PluginProgramDiscoveryJob(*this, description, pluginProgramScanCancellation), true);
}

void MagDrumAudioProcessorEditor::finishPluginProgramDiscovery(int exitCode, bool cancelled,
                                                               juce::StringArray programs,
                                                               const juce::String& message)
{
    pluginProgramScanRunning = false;
    pluginProgramScanCancellation.reset();
    scanPluginProgramsButton.setEnabled(true);
    scanPluginProgramsButton.setButtonText(magdrum::ui::localisedText("Scan programs"));
    const int selectedIndex = pluginInstrumentSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(selectedIndex, discoveredPluginInstruments.size())
        || discoveredPluginInstruments.getReference(selectedIndex).createIdentifierString() != pluginProgramScanIdentifier)
        return;
    if (cancelled)
    {
        status.setText(magdrum::ui::localisedText("Instrument program scan cancelled."), juce::dontSendNotification);
        return;
    }
    if (exitCode != 0)
    {
        status.setText(message.isNotEmpty() ? message : magdrum::ui::localisedText("Could not scan this instrument's programs."), juce::dontSendNotification);
        return;
    }
    pluginProgramSelect.clear(juce::dontSendNotification);
    for (int i = 0; i < programs.size(); ++i)
        pluginProgramSelect.addItem(programs[i], i + 1);
    if (! programs.isEmpty())
    {
        pluginProgramSelect.setSelectedId(1, juce::dontSendNotification);
        pluginPresetKitName.setText(programs[0], juce::dontSendNotification);
    }
    importPluginProgramButton.setEnabled(! programs.isEmpty() && ! pluginPresetImportRunning);
    status.setText(programs.isEmpty() ? magdrum::ui::localisedText("No programs exposed by this instrument.")
                                      : magdrum::ui::localisedText("Instrument programs loaded: ") + juce::String(programs.size()),
                   juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::startPluginProgramImport()
{
    if (pluginPresetImportRunning)
    {
        if (pluginPresetImportCancellation)
            pluginPresetImportCancellation->store(true);
        importPluginProgramButton.setEnabled(false);
        importPluginPresetButton.setEnabled(false);
        return;
    }
    const int instrumentIndex = pluginInstrumentSelect.getSelectedId() - 1;
    const int programIndex = pluginProgramSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(instrumentIndex, discoveredPluginInstruments.size())
        || ! juce::isPositiveAndBelow(programIndex, pluginProgramSelect.getNumItems()))
    {
        status.setText(magdrum::ui::localisedText("Choose an instrument program first."), juce::dontSendNotification);
        return;
    }
    const auto name = pluginPresetKitName.getText().trim();
    if (name.isEmpty())
    {
        status.setText(magdrum::ui::localisedText("Enter a name for the preset kit."), juce::dontSendNotification);
        return;
    }
    if (! bundledScannerApplication().existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }
    const auto description = discoveredPluginInstruments.getReference(instrumentIndex);
    const auto identifier = description.createIdentifierString();
    pluginPresetImportCancellation = std::make_shared<std::atomic<bool>>(false);
    pluginPresetImportRunning = true;
    pluginPresetImportFromProgram = true;
    choosePluginPresetButton.setEnabled(false);
    importPluginPresetButton.setEnabled(false);
    importPluginProgramButton.setButtonText(magdrum::ui::localisedText("Cancel preset import"));
    status.setText(magdrum::ui::localisedText("Saving the selected instrument program as a kit…"), juce::dontSendNotification);
    pluginPresetImportWorkers.addJob(new PluginPresetImportJob(*this, description, {}, identifier, name,
        pluginPresetImportCancellation, programIndex), true);
}

void MagDrumAudioProcessorEditor::finishPluginPresetImport(int exitCode, bool cancelled,
                                                            juce::MemoryBlock state,
                                                            const juce::String& message,
                                                            const juce::String& pluginIdentifier,
                                                            const juce::String& kitName)
{
    const bool importedProgram = pluginPresetImportFromProgram;
    pluginPresetImportRunning = false;
    pluginPresetImportFromProgram = false;
    pluginPresetImportCancellation.reset();
    importPluginPresetButton.setButtonText(magdrum::ui::localisedText("Import preset as kit"));
    importPluginProgramButton.setButtonText(magdrum::ui::localisedText("Import program as kit"));
    refreshPluginKits();
    if (cancelled)
    {
        status.setText(magdrum::ui::localisedText("Preset import cancelled."), juce::dontSendNotification);
        return;
    }
    if (exitCode != 0 || state.isEmpty())
    {
        status.setText(message.isNotEmpty() ? message : magdrum::ui::localisedText("Could not import this VST3 preset."), juce::dontSendNotification);
        return;
    }
    if (! databaseReady || ! database.savePluginKit(pluginIdentifier, kitName, state))
    {
        status.setText(magdrum::ui::localisedText("Could not save the imported preset kit."), juce::dontSendNotification);
        return;
    }

    const int selectedIndex = pluginInstrumentSelect.getSelectedId() - 1;
    if (juce::isPositiveAndBelow(selectedIndex, discoveredPluginInstruments.size())
        && discoveredPluginInstruments.getReference(selectedIndex).createIdentifierString() == pluginIdentifier)
    {
        refreshPluginKits();
        for (int item = 0; item < pluginKitSelect.getNumItems(); ++item)
            if (pluginKitSelect.getItemText(item) == kitName)
                pluginKitSelect.setSelectedId(pluginKitSelect.getItemId(item), juce::dontSendNotification);
        scanPluginNotesButton.setEnabled(! pluginScanRunning && pluginKitSelect.getSelectedId() > 0);
    }
    status.setText(magdrum::ui::localisedText(importedProgram ? "Instrument program imported and saved as kit."
                                                              : "VST3 preset imported and saved as kit."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::refreshPluginKits()
{
    const int instrumentId = pluginInstrumentSelect.getSelectedId();
    const int descriptionIndex = instrumentId - 1;
    const bool hasVst3Instrument = juce::isPositiveAndBelow(descriptionIndex, discoveredPluginInstruments.size())
        && discoveredPluginInstruments.getReference(descriptionIndex).pluginFormatName == "VST3";
    scanPluginProgramsButton.setEnabled(hasVst3Instrument && ! pluginProgramScanRunning);
    choosePluginPresetButton.setEnabled(hasVst3Instrument && ! pluginPresetImportRunning);
    importPluginPresetButton.setEnabled(hasVst3Instrument && selectedPluginPresetFile.existsAsFile()
        && ! pluginPresetImportRunning);
    importPluginProgramButton.setEnabled(hasVst3Instrument && pluginProgramSelect.getSelectedId() > 0
        && ! pluginPresetImportRunning);
    const auto previousKit = pluginKitSelect.getText();
    pluginKitSelect.clear(juce::dontSendNotification);
    if (! juce::isPositiveAndBelow(descriptionIndex, discoveredPluginInstruments.size()) || ! databaseReady)
    {
        scanPluginNotesButton.setEnabled(false);
        return;
    }
    const auto identifier = discoveredPluginInstruments.getReference(descriptionIndex).createIdentifierString();
    int id = 1;
    for (const auto& kit : database.getPluginKitNames(identifier))
    {
        pluginKitSelect.addItem(kit, id);
        if (kit == previousKit)
            pluginKitSelect.setSelectedId(id, juce::dontSendNotification);
        ++id;
    }
    if (pluginKitSelect.getSelectedId() == 0 && pluginKitSelect.getNumItems() > 0)
        pluginKitSelect.setSelectedId(1, juce::dontSendNotification);
    scanPluginNotesButton.setEnabled(! pluginScanRunning && pluginKitSelect.getSelectedId() > 0);
}

void MagDrumAudioProcessorEditor::startPluginNoteScan()
{
    const int instrumentIndex = pluginInstrumentSelect.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow(instrumentIndex, discoveredPluginInstruments.size()) || ! databaseReady)
    {
        status.setText(magdrum::ui::localisedText("Select a discovered VST3 instrument first."), juce::dontSendNotification);
        return;
    }
    const auto description = discoveredPluginInstruments.getReference(instrumentIndex);
    const auto kitName = pluginKitSelect.getText();
    const auto kitId = database.getPluginKitId(description.createIdentifierString(), kitName);
    const auto savedState = database.loadPluginKitState(description.createIdentifierString(), kitName);
    if (! kitId || ! savedState || savedState->getSize() == 0)
    {
        status.setText(magdrum::ui::localisedText("Choose an imported or saved kit state."), juce::dontSendNotification);
        return;
    }

    PluginNoteScanSettings settings;
    settings.velocityLevels.clear();
    juce::StringArray tokens;
    tokens.addTokens(pluginVelocities.getText(), ",; ", "");
    for (const auto& token : tokens)
    {
        if (token.trim().isEmpty()) continue;
        const int velocity = token.getIntValue();
        if (velocity < 1 || velocity > 127 || settings.velocityLevels.size() >= 16)
        {
            status.setText(magdrum::ui::localisedText("Enter 1 to 16 velocity levels from 1 to 127."), juce::dontSendNotification);
            return;
        }
        if (std::find(settings.velocityLevels.begin(), settings.velocityLevels.end(), velocity) == settings.velocityLevels.end())
            settings.velocityLevels.push_back(velocity);
    }
    if (settings.velocityLevels.empty())
    {
        status.setText(magdrum::ui::localisedText("Enter at least one velocity level."), juce::dontSendNotification);
        return;
    }
    settings.noteDurationSeconds = pluginNoteDuration.getText().getDoubleValue();
    settings.releaseSeconds = pluginRelease.getText().getDoubleValue();
    settings.captureLengthSeconds = pluginCaptureLength.getText().getDoubleValue();
    settings.repeatCount = pluginRepeatCount.getText().getIntValue();
    settings.renderMode = pluginRenderMode.getSelectedId() == 3 ? PluginNoteRenderMode::audioDevice
        : pluginRenderMode.getSelectedId() == 2 ? PluginNoteRenderMode::realtimePaced : PluginNoteRenderMode::offline;
    if (settings.noteDurationSeconds <= 0.0 || settings.noteDurationSeconds > 10.0
        || settings.releaseSeconds < 0.0 || settings.releaseSeconds > 20.0
        || settings.captureLengthSeconds <= 0.0 || settings.captureLengthSeconds > 30.0
        || settings.repeatCount < 1 || settings.repeatCount > 8)
    {
        status.setText(magdrum::ui::localisedText("Check note, release, capture and repeat values."), juce::dontSendNotification);
        return;
    }
    if (! bundledScannerApplication().existsAsFile())
    {
        status.setText(magdrum::ui::localisedText("The bundled MagDrum instrument scanner was not found beside this VST3. Reinstall the latest package."), juce::dontSendNotification);
        return;
    }

    pluginScanCancellation.store(false);
    pluginScanRunning = true;
    scanPluginNotesButton.setEnabled(false);
    cancelPluginNotesButton.setEnabled(true);
    refreshPluginInventoryButton.setEnabled(false);
    openPluginManagerButton.setEnabled(false);
    pluginScanProgress.setText(magdrum::ui::localisedText("Preparing isolated MIDI scan…"), juce::dontSendNotification);
    status.setText(magdrum::ui::localisedText("The instrument will render MIDI notes in a separate process."), juce::dontSendNotification);
    pluginScanWorkers.addJob(new VstPluginNoteScanJob(*this, description, *savedState, settings, databaseFile,
        description.createIdentifierString(), kitName, *kitId, pluginScanCancellation), true);
}

void MagDrumAudioProcessorEditor::cancelPluginNoteScan()
{
    pluginScanCancellation.store(true);
    cancelPluginNotesButton.setEnabled(false);
    status.setText(magdrum::ui::localisedText("Cancellation requested. The scanner will stop at its next safe point."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::updatePluginScanProgress(int completed, int total, int note, int velocity)
{
    pluginScanProgress.setText(magdrum::ui::localisedText("Scanned {done} of {total} captures · MIDI {note} · velocity {velocity}")
        .replace("{done}", juce::String(completed)).replace("{total}", juce::String(total))
        .replace("{note}", juce::String(note)).replace("{velocity}", juce::String(velocity)), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::finishPluginNoteScan(int saved, int sounding, int silent, int failures,
    bool cancelled, const juce::String& error, const juce::String& summary)
{
    pluginScanRunning = false;
    cancelPluginNotesButton.setEnabled(false);
    refreshPluginInventoryButton.setEnabled(true);
    openPluginManagerButton.setEnabled(true);
    refreshPluginKits();
    if (databaseReady) { refreshSourceKitFilters(); refreshSamples(); }
    juce::String result;
    if (error.isNotEmpty())
        result = magdrum::ui::localisedText("Plugin note scan failed. Check the selected instrument and scan settings.");
    else
        result = magdrum::ui::localisedText(
            "Plugin note scan {state}: {saved} saved, {sounding} sounding, {silent} silent, {failures} errors. {summary}")
            .replace("{state}", magdrum::ui::localisedText(cancelled ? "cancelled" : "complete"))
            .replace("{saved}", juce::String(saved))
            .replace("{sounding}", juce::String(sounding))
            .replace("{silent}", juce::String(silent))
            .replace("{failures}", juce::String(failures))
            .replace("{summary}", summary);
    pluginScanInfo.setText(result, juce::dontSendNotification);
    pluginScanProgress.setText(result, juce::dontSendNotification);
    status.setText(result, juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::refreshSourceKitFilters()
{
    const int priorSourceId=sourceFilter.getSelectedId(),priorKitId=kitFilter.getSelectedId();const auto priorSource=sourceFilter.getText(),priorKit=kitFilter.getText();sourceFilter.clear(juce::dontSendNotification);sourceFilter.addItem(magdrum::ui::localisedText("All sources"),1);sourceFilter.addItem(magdrum::ui::localisedText("Sample folders"),2);int sourceId=3;
    juce::StringArray pluginNames;for(const auto& plugin:database.getPluginInstruments())pluginNames.addIfNotAlreadyThere(plugin.name);
    for(const auto& name:pluginNames){sourceFilter.addItem(name,sourceId);if(name==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    for(const auto& folder:database.getSampleSources("folder")){const auto label=magdrum::ui::localisedText("Folder: ")+folder.identifier;sourceFilter.addItem(label,sourceId);if(label==priorSource)sourceFilter.setSelectedId(sourceId,juce::dontSendNotification);++sourceId;}
    if(priorSourceId==2)sourceFilter.setSelectedId(2,juce::dontSendNotification);else if(priorSourceId<=1||sourceFilter.getSelectedId()==0)sourceFilter.setSelectedId(1,juce::dontSendNotification);else if(priorSourceId<sourceFilter.getNumItems()+1)sourceFilter.setSelectedId(priorSourceId,juce::dontSendNotification);
    kitFilter.clear(juce::dontSendNotification);kitFilter.addItem(magdrum::ui::localisedText("All kits"),1);int kitId=2;for(const auto& name:database.getPluginKitNames()){kitFilter.addItem(name,kitId);if(name==priorKit)kitFilter.setSelectedId(kitId,juce::dontSendNotification);++kitId;}if(priorKitId==1||priorKit.isEmpty()||kitFilter.getSelectedId()==0)kitFilter.setSelectedId(1,juce::dontSendNotification);else kitFilter.setSelectedId(priorKitId,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::loadSamplePage(bool reset, std::uint64_t requestGeneration)
{
    if (!databaseReady || databaseFile == juce::File())
        return;

    if (requestGeneration == 0)
        requestGeneration = ++sampleBrowseGeneration;
    if (requestGeneration != sampleBrowseGeneration)
        return;

    SampleBrowseFilter options;
    options.query = sampleSearch.getText();
    options.contentKind = juce::jlimit(0, 7, contentTypeFilter.getSelectedId() - 1);
    if (categoryFilter.getSelectedId() > 1)
    {
        static constexpr const char* categories[]{"All sounds", "Kick", "Snare", "Closed hi-hat", "Open hi-hat",
                                                  "Pedal hi-hat", "Tom", "Ride", "Crash", "Clap", "Percussion", "Unknown"};
        const int categoryId = categoryFilter.getSelectedId() - 1;
        if (juce::isPositiveAndBelow(categoryId, 12))
            options.category = categories[categoryId];
    }
    options.feature = juce::jmax(1, featureFilter.getSelectedId());
    options.sort = juce::jmax(1, sortFilter.getSelectedId());
    options.needsReview = confidenceFilter.getSelectedId() == 2;

    const auto source = sourceFilter.getText();
    if (sourceFilter.getSelectedId() == 2)
        options.sourceType = "folder";
    else if (source.startsWith(magdrum::ui::localisedText("Folder: ")))
    {
        options.sourceType = "folder";
        options.sourceIdentifier = source.fromFirstOccurrenceOf(magdrum::ui::localisedText("Folder: "), false, false);
    }
    else if (sourceFilter.getSelectedId() > 2 && source.isNotEmpty())
    {
        options.sourceType = "plugin";
        options.pluginName = source;
    }

    const auto kit = kitFilter.getText();
    if (kitFilter.getSelectedId() > 1 && kit.isNotEmpty())
    {
        options.sourceType = "plugin";
        if (options.pluginName.isEmpty())
            options.pluginName = kit.upToFirstOccurrenceOf(" / ", false, false);
        options.kitName = kit.fromFirstOccurrenceOf(" / ", false, false);
    }

    const int offset = reset ? 0 : sampleOffset;
    const bool requestMap = reset && sampleMapGeneration != requestGeneration;
    if (requestMap)
    {
        sampleMapGeneration = requestGeneration;
        sampleMapQueryPending = true;
    }
    sampleBrowseQueryPending = true;
    indexedList.setEnabled(false);
    sampleMap->setEnabled(!sampleMapQueryPending);
    previewButton.setEnabled(false);
    assignButton.setEnabled(false);
    libraryToolsButton.setEnabled(false);
    moreSamplesButton.setEnabled(false);

    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    if (requestMap)
    {
        auto mapOptions = options;
        sampleMapWorkers.addJob(new PluginSampleBrowseJob(databaseFile, std::move(mapOptions), 3000, 0,
            [safe, requestGeneration](bool opened, SampleBrowsePage result)
            {
                if (safe == nullptr || requestGeneration != safe->sampleBrowseGeneration) return;
                safe->sampleMapQueryPending = false;
                safe->sampleMapSamples = opened ? std::move(result.records) : juce::Array<SampleBrowseRecord>{};
                int selectedMapRow = -1;
                const auto selectedFile = safe->sampleMapSelectedFile != juce::File()
                    ? safe->sampleMapSelectedFile : safe->selectedSample();
                if (selectedFile != juce::File())
                    for (int index = 0; index < safe->sampleMapSamples.size(); ++index)
                        if (safe->sampleMapSamples.getReference(index).file == selectedFile)
                        { selectedMapRow = index; break; }
                safe->sampleMap->setSamples(safe->sampleMapSamples, selectedMapRow);
                safe->sampleMap->setEnabled(!safe->sampleBrowseQueryPending);
            }), true);
    }
    sampleBrowseWorkers.addJob(new PluginSampleBrowseJob(databaseFile, std::move(options), 250, offset,
        [safe, requestGeneration, reset](bool opened, SampleBrowsePage result)
        {
            if (safe == nullptr || requestGeneration != safe->sampleBrowseGeneration)
                return;

            safe->sampleBrowseQueryPending = false;
            safe->indexedList.setEnabled(true);
            safe->sampleMap->setEnabled(!safe->sampleMapQueryPending);
            safe->previewButton.setEnabled(true);
            safe->assignButton.setEnabled(true);
            safe->libraryToolsButton.setEnabled(true);

            if (!opened)
            {
                safe->selectedRowsChanged(safe->indexedList.getSelectedRow());
                safe->moreSamplesButton.setEnabled(safe->hasMoreSamples);
                safe->status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                                     juce::dontSendNotification);
                return;
            }

            if (reset)
            {
                safe->samples.clear();
                safe->sampleOffset = 0;
                safe->sampleMapSelectedFile = {};
                safe->indexedList.deselectAllRows();
            }
            for (const auto& record : result.records)
                safe->samples.add(record);
            safe->sampleOffset = safe->samples.size();
            safe->hasMoreSamples = result.hasMore;
            safe->indexedList.updateContent();
            safe->refreshSampleAssignmentLabels();
            safe->selectedRowsChanged(safe->indexedList.getSelectedRow());
            safe->moreSamplesButton.setEnabled(safe->hasMoreSamples);
            safe->status.setText(juce::String(safe->samples.size()) + (safe->hasMoreSamples ? "+" : "") + " "
                                     + magdrum::ui::localisedText("samples - select, preview or use Sample tools"),
                                 juce::dontSendNotification);
        }), true);
}
juce::File MagDrumAudioProcessorEditor::selectedSample()const{if(sampleMapSelectedFile!=juce::File())return sampleMapSelectedFile;const int row=indexedList.getSelectedRow();return juce::isPositiveAndBelow(row,samples.size())?samples.getReference(row).file:juce::File();}

void MagDrumAudioProcessorEditor::chooseSampleFolder(bool chooseDifferentFolder)
{
    if(scanningDefaultLibraries&&!scanning&&defaultMidiSearchCancellation)
    {
        defaultMidiSearchCancellation->store(true,std::memory_order_release);
        scanButton.setButtonText(magdrum::ui::localisedText("Cancelling…"));
        scanButton.setEnabled(false);
        status.setText(magdrum::ui::localisedText("Stopping the drum MIDI search…"),juce::dontSendNotification);
        return;
    }
    if(scanning){if(scanningDefaultLibraries)return;if(scanCancellation)scanCancellation->store(true);scanButton.setButtonText(magdrum::ui::localisedText("Cancelling…"));scanButton.setEnabled(false);status.setText(magdrum::ui::localisedText("Stopping the current folder scan…"),juce::dontSendNotification);return;}
    if(!chooseDifferentFolder&&lastSampleScanFolder.isDirectory()){startSampleScan(lastSampleScanFolder);return;}
    chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose a drum sample folder"),juce::File(),juce::String());
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser& fileChooser){if(safe!=nullptr){const auto folder=fileChooser.getResult();if(folder.isDirectory())safe->startSampleScan(folder);}});
}

void MagDrumAudioProcessorEditor::scanDefaultLibraryFolders()
{
    if(scanningDefaultLibraries&&!scanning&&defaultMidiSearchCancellation)
    {
        defaultMidiSearchCancellation->store(true,std::memory_order_release);
        scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Cancelling…"));scanDefaultsButton.setEnabled(false);
        status.setText(magdrum::ui::localisedText("Stopping the current folder scan…"),juce::dontSendNotification);
        return;
    }
    if(scanningDefaultLibraries&&scanning)
    {
        if(scanCancellation)scanCancellation->store(true,std::memory_order_release);
        scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Cancelling…"));scanDefaultsButton.setEnabled(false);
        status.setText(magdrum::ui::localisedText("Stopping the current folder scan…"),juce::dontSendNotification);
        return;
    }
    if(defaultLibraryScanCanResume&&!defaultLibraryFolders.empty())
    {
        defaultLibraryScanCanResume=false;scanningDefaultLibraries=true;scanDefaultsButton.setEnabled(true);
        scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));startNextDefaultLibraryFolder();return;
    }
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
    defaultLibraryVstSoundCount=0;
    defaultLibraryKontaktNcwCount=0;
    defaultMidiPatterns.clear();
    browseMidiButton.setButtonText(magdrum::ui::localisedText("Find drum MIDI"));
    browseMidiButton.setEnabled(false);
    scanningDefaultLibraries=true;
    scanDefaultsButton.setEnabled(true);
    scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));
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
    const SampleBrowseRecord* sample = nullptr;
    if (sampleMapSelectedFile != juce::File())
    {
        const auto found = std::find_if(sampleMapSamples.begin(), sampleMapSamples.end(), [this](const auto& item)
        { return item.file == sampleMapSelectedFile; });
        if (found != sampleMapSamples.end()) sample = &*found;
    }
    const int row = indexedList.getSelectedRow();
    if (sample == nullptr && juce::isPositiveAndBelow(row, samples.size())) sample = &samples.getReference(row);
    if (sample == nullptr)
    {
        status.setText(magdrum::ui::localisedText("Select a sample first."), juce::dontSendNotification);
        return;
    }

    PluginSourceKitRequest request{ sample->pluginIdentifier, sample->kitName };
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
        finishDefaultMidiPatternSearch({},false);
        return;
    }
    scanButton.setButtonText(magdrum::ui::localisedText("Searching MIDI…"));
    status.setText(magdrum::ui::localisedText("Searching detected library folders for drum MIDI patterns…"),juce::dontSendNotification);
    defaultMidiSearchCancellation=std::make_shared<std::atomic<bool>>(false);
    const auto cancellation=defaultMidiSearchCancellation;
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    auto completion=[safe](std::vector<juce::File> patterns,bool cancelled)
    {
        juce::MessageManager::callAsync([safe,patterns=std::move(patterns),cancelled]() mutable
        {
            if(safe!=nullptr)
                safe->finishDefaultMidiPatternSearch(std::move(patterns),cancelled);
        });
    };
    scanButton.setEnabled(true);
    scanButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));
    scanWorkers.addJob(new PluginMidiPatternDiscoveryJob(defaultLibraryFolders,cancellation,std::move(completion)),true);
}

void MagDrumAudioProcessorEditor::finishDefaultMidiPatternSearch(std::vector<juce::File> patterns,bool cancelled)
{
    defaultMidiSearchCancellation.reset();
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
    auto completionMessage=magdrum::ui::localisedText("Library scan complete")+" | "+juce::String(defaultLibraryIndexedCount)+" "+magdrum::ui::localisedText("audio samples indexed across")+" "+juce::String(folderCount)+" "+magdrum::ui::localisedText("locations")+" | "+juce::String(static_cast<int>(defaultMidiPatterns.size()))+" "+magdrum::ui::localisedText("drum MIDI patterns found")+" | "+juce::String(defaultLibraryErrorCount)+" "+magdrum::ui::localisedText("errors")+".";
    if(defaultLibraryVstSoundCount>0||defaultLibraryKontaktNcwCount>0)
    {
        const auto containerWarning=magdrum::ui::localisedText("Detected %1 Steinberg VST Sound containers and %2 Kontakt NCW files. Use Plugin Scanner to capture sounds from installed instruments.")
            .replace("%1",juce::String(defaultLibraryVstSoundCount)).replace("%2",juce::String(defaultLibraryKontaktNcwCount));
        completionMessage+=" "+containerWarning;
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
            magdrum::ui::localisedText("Some installed library sounds need Plugin Scanner"), containerWarning,
            magdrum::ui::localisedText("Close"), this);
    }
    if(cancelled)
    {
        completionMessage=magdrum::ui::localisedText("Library scan cancelled · ")+juce::String(defaultLibraryIndexedCount)+" "+magdrum::ui::localisedText("audio samples indexed so far.");
        if(!defaultMidiPatterns.empty())
            completionMessage+=" · "+juce::String(static_cast<int>(defaultMidiPatterns.size()))+" "+magdrum::ui::localisedText("drum MIDI patterns found");
    }
    status.setText(completionMessage,juce::dontSendNotification);
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
    scanning=true;lastSampleScanFolder=folder;sampleScanProgressValue=-1.0;scanCancellation=std::make_shared<std::atomic<bool>>(false);
    if(scanningDefaultLibraries){scanDefaultsButton.setEnabled(true);scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));}
    else{scanButton.setEnabled(true);scanButton.setButtonText(magdrum::ui::localisedText("Cancel scan"));}
    sampleScanProgressLabel.setText(magdrum::ui::localisedText("Scanning folder: ")+folder.getFileName(),juce::dontSendNotification);resized();status.setText(magdrum::ui::localisedText("Scanning folder: ")+folder.getFileName()+"...",juce::dontSendNotification);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    auto completion=[safe](const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error){juce::MessageManager::callAsync([safe,stats,indexed,error]{if(safe!=nullptr)safe->finishSampleScan(stats,indexed,error);});};
    auto progress=[safe](const juce::File& file,int completed,int total){if(total>0&&completed!=0&&completed!=total&&completed%5!=0)return;juce::MessageManager::callAsync([safe,file,completed,total]{if(safe!=nullptr)safe->updateSampleScanProgress(file,completed,total);});};
    scanWorkers.addJob(new PluginSampleScanJob(folder,databaseFile,scanCancellation,std::move(completion),std::move(progress)),true);
}

void MagDrumAudioProcessorEditor::updateSampleScanProgress(const juce::File& file,int completed,int total)
{
    if(!scanning)return;
    sampleScanProgressValue=total>0?static_cast<double>(juce::jlimit(0,total,completed))/total:-1.0;
    const int percent=total>0?juce::jlimit(0,100,juce::roundToInt(100.0*completed/total)):-1;
    const auto name=file.getFileName();
    sampleScanProgressLabel.setText(total>0?juce::String(percent)+"% - "+juce::String(completed)+" / "+juce::String(total)+" - "+name:name,juce::dontSendNotification);
    status.setText(magdrum::ui::localisedText("Scanning sample: ")+name,juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::finishSampleScan(const WavScanner::Stats& stats,std::int64_t indexed,const juce::String& error)
{
    scanning=false;scanCancellation.reset();sampleScanProgressValue=stats.cancelled?0.0:1.0;sampleScanProgressLabel.setText(juce::String{},juce::dontSendNotification);scanButton.setEnabled(true);scanButton.setButtonText(stats.cancelled&&!scanningDefaultLibraries?magdrum::ui::localisedText("Resume scan"):magdrum::ui::localisedText("Add sample folder"));refreshSourceKitFilters();refreshSamples();resized();
    if(scanningDefaultLibraries)
    {
        defaultLibraryIndexedCount+=indexed;
        defaultLibraryErrorCount+=stats.errors+(error.isNotEmpty()?1:0);
        defaultLibraryVstSoundCount+=stats.steinbergVstSoundContainers;
        defaultLibraryKontaktNcwCount+=stats.kontaktNcwFiles;
        if(stats.cancelled)
        {
            scanningDefaultLibraries=false;
            defaultLibraryScanCanResume=true;
            scanDefaultsButton.setEnabled(true);scanDefaultsButton.setButtonText(magdrum::ui::localisedText("Resume scan"));
            browseMidiButton.setEnabled(true);
            status.setText(magdrum::ui::localisedText("Scan cancelled. ")+juce::String(defaultLibraryIndexedCount)+" "+magdrum::ui::localisedText("audio samples indexed so far."),juce::dontSendNotification);
            return;
        }
        ++defaultLibraryFolderIndex;
        startNextDefaultLibraryFolder();
        return;
    }
    if(error.isNotEmpty())status.setText(magdrum::ui::localisedText("Sample folder scan failed. Check the selected folder and try again."),juce::dontSendNotification);
    else
    {
        auto message=magdrum::ui::localisedText(stats.cancelled?"Scan cancelled. ":"Scan complete. ")+juce::String(indexed)+" "+magdrum::ui::localisedText("indexed, ")+juce::String(stats.filesSkippedUnchanged)+" "+magdrum::ui::localisedText("unchanged, ")+juce::String(stats.errors)+" "+magdrum::ui::localisedText("errors.");
        if(stats.steinbergVstSoundContainers>0||stats.kontaktNcwFiles>0)
            message+=" "+magdrum::ui::localisedText("Detected %1 Steinberg VST Sound containers and %2 Kontakt NCW files. Use Plugin Scanner to capture sounds from installed instruments.").replace("%1",juce::String(stats.steinbergVstSoundContainers)).replace("%2",juce::String(stats.kontaktNcwFiles));
        status.setText(message,juce::dontSendNotification);
    }
    if(!stats.cancelled)lastSampleScanFolder=juce::File();
}

void MagDrumAudioProcessorEditor::showLibraryTools()
{
    juce::PopupMenu menu;menu.addItem(1,magdrum::ui::localisedText("Edit sample tags…"));
    juce::PopupMenu categories;const char* names[]{"Unknown","Kick","Snare","Closed hi-hat","Open hi-hat","Pedal hi-hat","Tom","Ride","Crash","Clap","Rimshot","Percussion"};for(int i=0;i<12;++i)categories.addItem(100+i,magdrum::ui::localisedText(names[i]));menu.addSubMenu(magdrum::ui::localisedText("Set drum category"),categories);
    menu.addSeparator();menu.addItem(2,magdrum::ui::localisedText("Find similar samples"));menu.addItem(3,magdrum::ui::localisedText("Find exact duplicates"));menu.addItem(4,magdrum::ui::localisedText("Find near duplicates"));menu.addItem(5,magdrum::ui::localisedText("Compare two selected waveforms"));menu.addSeparator();menu.addItem(6,magdrum::ui::localisedText("Scan history"));menu.addItem(7,magdrum::ui::localisedText("Export portable metadata…"));menu.addItem(8,magdrum::ui::localisedText("Import portable metadata…"));menu.addItem(9,magdrum::ui::localisedText("Sync metadata"));
    const int selectedRow = indexedList.getSelectedRow();
    const bool hasPluginKit = juce::isPositiveAndBelow(selectedRow, samples.size())
        && samples.getReference(selectedRow).pluginIdentifier.isNotEmpty()
        && samples.getReference(selectedRow).kitName.isNotEmpty();
    menu.addSeparator();
    menu.addItem(10, magdrum::ui::localisedText("View layer candidates"), hasPluginKit);
    menu.addItem(11, magdrum::ui::localisedText("View round-robin candidates"), hasPluginKit);
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
    if(action==10||action==11)
    {
        const int row = indexedList.getSelectedRow();
        if (!juce::isPositiveAndBelow(row, samples.size()))
        {
            status.setText(magdrum::ui::localisedText("Select a sample first."), juce::dontSendNotification);
            return;
        }
        const auto sample = samples.getReference(row);
        if (sample.pluginIdentifier.isEmpty() || sample.kitName.isEmpty())
        {
            status.setText(magdrum::ui::localisedText("The selected sample has no saved plugin source kit."),
                           juce::dontSendNotification);
            return;
        }
        if (sampleReviewRunning)
            return;
        if (!databaseReady || databaseFile == juce::File())
        {
            status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                           juce::dontSendNotification);
            return;
        }

        sampleReviewRunning = true;
        libraryToolsButton.setEnabled(false);
        status.setText(magdrum::ui::localisedText(action == 10 ? "View layer candidates"
                                                               : "View round-robin candidates"),
                       juce::dontSendNotification);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        sampleReviewWorkers.addJob(new PluginSampleCandidateReviewJob(databaseFile, sample, action,
            [safe](int reviewAction, bool opened, SampleBrowseRecord sampleRecord,
                   std::vector<PluginVelocityLayerBoundary> layerCandidates,
                   std::vector<PluginConfirmedVelocityLayerRange> confirmedLayers,
                   std::vector<PluginRoundRobinCandidate> roundRobinCandidates,
                   std::vector<PluginConfirmedRoundRobinGroup> confirmedGroups) mutable
            {
                if (safe != nullptr)
                    safe->finishSampleCandidateReview(reviewAction, opened, std::move(sampleRecord),
                        std::move(layerCandidates), std::move(confirmedLayers),
                        std::move(roundRobinCandidates), std::move(confirmedGroups));
            }), true);
        return;
    }
    if(action==1)
    {
        const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample before editing its tags."),juce::dontSendNotification);return;}
        auto* dialog=new juce::AlertWindow(magdrum::ui::localisedText("Edit sample tags"),file.getFileName(),juce::AlertWindow::NoIcon);dialog->addTextEditor("tags",database.getUserTags(file).joinIntoString(", "),magdrum::ui::localisedText("Comma-separated tags"));if(auto* tagsEditor=dialog->getTextEditor("tags"))tagsEditor->setJustification(juce::Justification::centredLeft);dialog->addButton(magdrum::ui::localisedText("Save"),1);dialog->addButton(magdrum::ui::localisedText("Cancel"),0);dialog->setSize(480,180);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe,safeDialog,file](int result){if(result!=1||safe==nullptr||safeDialog==nullptr)return;juce::StringArray tags;tags.addTokens(safeDialog->getTextEditorContents("tags"),",","\"");tags.removeEmptyStrings();if(safe->database.setUserTags(file,tags)){safe->refreshSamples();safe->status.setText(magdrum::ui::localisedText("Sample tags saved."),juce::dontSendNotification);}else safe->status.setText(magdrum::ui::localisedText("Could not save sample tags."),juce::dontSendNotification);}),true);return;
    }
    const auto file=selectedSample();
    if(action>=2&&action<=4&&file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}
    if(action>=2&&action<=4)
    {
        if (sampleLookupRunning)
            return;
        if (!databaseReady || databaseFile == juce::File())
        {
            status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                           juce::dontSendNotification);
            return;
        }

        sampleLookupRunning = true;
        libraryToolsButton.setEnabled(false);
        const char* progressText = action == 2 ? "Similar sample candidates"
                                   : action == 3 ? "Exact file duplicates"
                                                 : "Near-duplicate candidates";
        status.setText(magdrum::ui::localisedText(progressText), juce::dontSendNotification);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        sampleLookupWorkers.addJob(new PluginSampleLookupJob(databaseFile, file, action,
            [safe](int lookupAction, bool opened, juce::Array<juce::File> matches,
                   std::vector<PerceptualDuplicateMatch> perceptualMatches) mutable
            {
                if (safe != nullptr)
                    safe->finishSampleLookup(lookupAction, opened, std::move(matches),
                                             std::move(perceptualMatches));
            }), true);
        return;
    }
    if(action==5)
    {
        const auto selected=indexedList.getSelectedRows();if(selected.size()!=2){status.setText(magdrum::ui::localisedText("Ctrl-click exactly two samples in the list to compare their waveforms."),juce::dontSendNotification);return;}const auto first=juce::isPositiveAndBelow(selected[0],samples.size())?samples.getReference(selected[0]).file:juce::File();const auto second=juce::isPositiveAndBelow(selected[1],samples.size())?samples.getReference(selected[1]).file:juce::File();if(first==juce::File()||second==juce::File())return;openSampleComparisonWindow(first,second);return;
    }
    if(action==6)
    {
        const auto scans=database.getRecentSampleScans();juce::String report;for(const auto& scan:scans)report<<scan.startedAt<<"  ·  "<<scan.status<<"\n"<<scan.rootPath<<"\n"<<scan.filesIndexed<<" "<<magdrum::ui::localisedText("indexed, ")<<scan.filesSkipped<<" "<<magdrum::ui::localisedText("unchanged, ")<<scan.errors<<" "<<magdrum::ui::localisedText("errors.")<<"\n\n";juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,magdrum::ui::localisedText("Sample scan history"),report.isEmpty()?magdrum::ui::localisedText("No sample scans have been recorded."):report,magdrum::ui::localisedText("Close"));return;
    }
    if(action==7||action==8)
    {
        const bool exporting=action==7;
        chooser=std::make_unique<juce::FileChooser>(
            magdrum::ui::localisedText(exporting?"Export MagDrum metadata":"Import MagDrum metadata"),
            juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Metadata.json"),
            "*.json");
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(
            exporting?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles)
                     :(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),
            [safe,exporting](const juce::FileChooser& fc)
            {
                if(safe==nullptr)return;
                const auto target=fc.getResult();
                if(target==juce::File())return;
                if(safe->metadataArchiveRunning)return;
                if(!safe->databaseReady||safe->databaseFile==juce::File())
                {
                    safe->status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                                         juce::dontSendNotification);
                    return;
                }

                safe->metadataArchiveRunning=true;
                safe->libraryToolsButton.setEnabled(false);
                safe->status.setText(magdrum::ui::localisedText(
                    exporting?"Export MagDrum metadata":"Import MagDrum metadata"),juce::dontSendNotification);
                safe->metadataArchiveWorkers.addJob(new PluginMetadataArchiveJob(
                    safe->databaseFile,target,exporting,
                    [safe](bool isExport,bool databaseOpened,bool archiveValid,bool fileSaved,
                           juce::File archiveFile,PortableMetadataMergeResult result)
                    {
                        if(safe!=nullptr)
                            safe->finishPortableMetadataArchive(isExport,databaseOpened,archiveValid,
                                                                fileSaved,archiveFile,result);
                    }),true);
            });
        return;
    }
    if(action==9)
    {
        if(!databaseReady||databaseFile==juce::File()){status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),juce::dontSendNotification);return;}
        if(metadataSyncRunning){status.setText(magdrum::ui::localisedText("Sync metadata"),juce::dontSendNotification);return;}
        chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose a folder for metadata sync"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory));
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectDirectories,[safe](const juce::FileChooser& fc)
        {
            if(safe==nullptr)return;
            const auto folder=fc.getResult();if(folder==juce::File())return;
            if(safe->metadataSyncRunning)return;
            safe->metadataSyncRunning=true;safe->status.setText(magdrum::ui::localisedText("Sync metadata"),juce::dontSendNotification);
            safe->metadataSyncWorkers.addJob(new PluginMetadataFolderSyncJob(safe->databaseFile,folder,[safe](PortableMetadataSyncResult result)
            {
                if(safe!=nullptr)safe->finishMetadataFolderSync(result);
            }),true);
        });
        return;
    }
}

void MagDrumAudioProcessorEditor::finishSampleLookup(
    int action, bool opened, juce::Array<juce::File> matches,
    std::vector<PerceptualDuplicateMatch> perceptualMatches)
{
    sampleLookupRunning = false;
    libraryToolsButton.setEnabled(true);

    const auto title = magdrum::ui::localisedText(action == 2 ? "Similar sample candidates"
                                                : action == 3 ? "Exact file duplicates"
                                                              : "Near-duplicate candidates");
    if (!opened)
    {
        status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                       juce::dontSendNotification);
        return;
    }

    juce::String details;
    if (action == 4)
    {
        for (const auto& match : perceptualMatches)
            details << juce::String(match.similarity * 100.0, 1) << "%  " << match.file.getFileName()
                    << "\n" << match.file.getFullPathName() << "\n\n";
        const auto message = perceptualMatches.empty()
            ? magdrum::ui::localisedText("No likely near duplicates found.")
            : details + "\n" + magdrum::ui::localisedText(
                "These are feature-based candidates. Listen before replacing or deleting samples.");
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, title, message,
                                                magdrum::ui::localisedText("Close"));
        return;
    }

    for (const auto& match : matches)
        details << match.getFileName() << "\n" << match.getFullPathName() << "\n\n";
    const auto message = matches.isEmpty()
        ? magdrum::ui::localisedText(action == 2 ? "No similar samples found."
                                                  : "No exact duplicates found.")
        : details;
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, title, message,
                                            magdrum::ui::localisedText("Close"));
}

void MagDrumAudioProcessorEditor::finishSampleCandidateReview(
    int action, bool opened, SampleBrowseRecord sample,
    std::vector<PluginVelocityLayerBoundary> layerCandidates,
    std::vector<PluginConfirmedVelocityLayerRange> confirmedLayers,
    std::vector<PluginRoundRobinCandidate> roundRobinCandidates,
    std::vector<PluginConfirmedRoundRobinGroup> confirmedGroups)
{
    sampleReviewRunning = false;
    libraryToolsButton.setEnabled(true);
    if (!opened)
    {
        status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                       juce::dontSendNotification);
        return;
    }

    if (action == 10)
        reviewVelocityLayers(sample, layerCandidates, confirmedLayers);
    else if (action == 11)
        reviewRoundRobinGroups(sample, roundRobinCandidates, confirmedGroups);
}

void MagDrumAudioProcessorEditor::reviewVelocityLayers(
    const SampleBrowseRecord& sample,
    const std::vector<PluginVelocityLayerBoundary>& candidates,
    const std::vector<PluginConfirmedVelocityLayerRange>& confirmed)
{
    if (candidates.empty() && confirmed.empty())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            magdrum::ui::localisedText("Velocity layer candidates"),
            magdrum::ui::localisedText("No candidates or confirmed ranges are saved for this kit. Scan the same MIDI notes at two or more velocity levels first."));
        return;
    }

    auto report = magdrum::ui::localisedText("%1 heuristic boundary candidate(s). Listen to the captures before recording confirmed ranges. These ranges describe your review, not a claim about the plugin's internal sample zones.\n\nCandidates:\n")
        .replace("%1", juce::String(candidates.size()));
    const int shown = juce::jmin(48, static_cast<int>(candidates.size()));
    for (int index = 0; index < shown; ++index)
    {
        const auto& candidate = candidates[static_cast<size_t>(index)];
        report += magdrum::ui::localisedText("MIDI note %1: velocity %2 to %3 (distance %4)\n")
            .replace("%1", juce::String(candidate.midiNote))
            .replace("%2", juce::String(candidate.lowerVelocity))
            .replace("%3", juce::String(candidate.upperVelocity))
            .replace("%4", juce::String(candidate.spectralDistance, 2));
    }
    if (candidates.size() > static_cast<size_t>(shown))
        report += magdrum::ui::localisedText("Showing the first %1 candidates.\n").replace("%1", juce::String(shown));
    report += magdrum::ui::localisedText("\nUser-confirmed ranges:\n");

    juce::String serialized;
    for (const auto& range : confirmed)
    {
        report += magdrum::ui::localisedText("MIDI note %1: velocity %2 to %3\n")
            .replace("%1", juce::String(range.midiNote))
            .replace("%2", juce::String(range.minimumVelocity))
            .replace("%3", juce::String(range.maximumVelocity));
        if (serialized.isNotEmpty()) serialized += "; ";
        serialized += juce::String(range.midiNote) + ":" + juce::String(range.minimumVelocity)
            + "-" + juce::String(range.maximumVelocity);
    }
    if (confirmed.empty()) report += magdrum::ui::localisedText("None\n");

    auto* dialog = new juce::AlertWindow(
        magdrum::ui::localisedText("Review velocity layers — %1").replace("%1", sample.kitName),
        report, juce::AlertWindow::InfoIcon);
    dialog->addTextEditor("ranges", serialized, magdrum::ui::localisedText("Confirmed ranges (note:min-max; ...)"));
    if (auto* rangesEditor = dialog->getTextEditor("ranges")) rangesEditor->setJustification(juce::Justification::centredLeft);
    dialog->addButton(magdrum::ui::localisedText("Save confirmed ranges"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(magdrum::ui::localisedText("Close"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->setSize(660, 480);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
    const auto pluginIdentifier = sample.pluginIdentifier;
    const auto kitName = sample.kitName;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe, safeDialog, pluginIdentifier, kitName](int result)
        {
            const auto input = result == 1 && safeDialog != nullptr
                ? safeDialog->getTextEditorContents("ranges") : juce::String();
            if (safeDialog != nullptr) delete safeDialog.getComponent();
            if (result != 1 || safe == nullptr) return;

            juce::StringArray entries;
            entries.addTokens(input, "; ,", "");
            std::vector<PluginConfirmedVelocityLayerRange> ranges;
            for (const auto& entry : entries)
            {
                const auto item = entry.trim();
                if (item.isEmpty()) continue;
                const auto noteText = item.upToFirstOccurrenceOf(":", false, false).trim();
                const auto rangeText = item.fromFirstOccurrenceOf(":", false, false).trim();
                const auto minimumText = rangeText.upToFirstOccurrenceOf("-", false, false).trim();
                const auto maximumText = rangeText.fromFirstOccurrenceOf("-", false, false).trim();
                if (noteText.isEmpty() || rangeText == item || minimumText.isEmpty() || maximumText.isEmpty()
                    || !noteText.containsOnly("0123456789") || !minimumText.containsOnly("0123456789")
                    || !maximumText.containsOnly("0123456789"))
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                        magdrum::ui::localisedText("Invalid velocity ranges"),
                        magdrum::ui::localisedText("Use entries such as 36:1-48; 36:49-127."));
                    return;
                }
                ranges.push_back({noteText.getIntValue(), minimumText.getIntValue(), maximumText.getIntValue()});
            }

            const auto kitId = safe->database.getPluginKitId(pluginIdentifier, kitName);
            if (!kitId || !safe->database.replaceConfirmedPluginVelocityLayers(*kitId, ranges))
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    magdrum::ui::localisedText("Could not save velocity ranges"),
                    magdrum::ui::localisedText("Check that note numbers are 0–127, velocities are 1–127, and ranges for each note do not overlap."));
            else
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                    magdrum::ui::localisedText("Velocity ranges saved"),
                    magdrum::ui::localisedText("The user-confirmed ranges are stored with this kit."));
        }), false);
}

void MagDrumAudioProcessorEditor::reviewRoundRobinGroups(
    const SampleBrowseRecord& sample,
    const std::vector<PluginRoundRobinCandidate>& candidates,
    const std::vector<PluginConfirmedRoundRobinGroup>& confirmed)
{
    if (candidates.empty())
    {
        auto report = magdrum::ui::localisedText("No repeated-trigger variation candidates are saved. Scan with 2–8 repeats, then listen to the extra takes in the sample browser.");
        for (const auto& group : confirmed)
        {
            juce::String takes;
            for (const int take : group.takeIndices)
            {
                if (takes.isNotEmpty()) takes += " ";
                takes += juce::String(take);
            }
            report += "\n" + magdrum::ui::localisedText("Confirmed MIDI note %1, velocity %2, takes %3\n")
                .replace("%1", juce::String(group.midiNote))
                .replace("%2", juce::String(group.velocity))
                .replace("%3", takes);
        }
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            magdrum::ui::localisedText("Round-robin groups — %1").replace("%1", sample.kitName), report);
        return;
    }

    auto report = magdrum::ui::localisedText("%1 heuristic variation candidate(s). Listen to the indexed takes; only confirm a group after reviewing its audio.\n\n")
        .replace("%1", juce::String(candidates.size()));
    juce::StringArray choices;
    for (const auto& candidate : candidates)
    {
        const auto isConfirmed = std::any_of(confirmed.begin(), confirmed.end(), [&](const auto& group)
        {
            return group.midiNote == candidate.midiNote && group.velocity == candidate.velocity;
        });
        const auto confirmation = isConfirmed ? magdrum::ui::localisedText(" (confirmed)") : juce::String();
        const auto label = magdrum::ui::localisedText("MIDI note %1, velocity %2 — %3 variants / %4 takes%5")
            .replace("%1", juce::String(candidate.midiNote))
            .replace("%2", juce::String(candidate.velocity))
            .replace("%3", juce::String(candidate.distinctVariants))
            .replace("%4", juce::String(candidate.takeCount))
            .replace("%5", confirmation);
        choices.add(label);
        report += label + "\n";
    }

    auto* dialog = new juce::AlertWindow(
        magdrum::ui::localisedText("Review round-robin groups — %1").replace("%1", sample.kitName),
        report, juce::AlertWindow::InfoIcon);
    dialog->addComboBox("group", choices, magdrum::ui::localisedText("Candidate"));
    dialog->addButton(magdrum::ui::localisedText("Toggle confirmation"), 1,
                      juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton(magdrum::ui::localisedText("Close"), 0,
                      juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->setSize(660, 480);
    juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
    const auto pluginIdentifier = sample.pluginIdentifier;
    const auto kitName = sample.kitName;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe, safeDialog, candidates, confirmed, pluginIdentifier, kitName](int result)
        {
            const auto* selection = safeDialog != nullptr ? safeDialog->getComboBoxComponent("group") : nullptr;
            const int index = selection != nullptr ? selection->getSelectedItemIndex() : -1;
            if (safeDialog != nullptr) delete safeDialog.getComponent();
            if (result != 1 || safe == nullptr || !juce::isPositiveAndBelow(index, static_cast<int>(candidates.size())))
                return;

            const auto& candidate = candidates[static_cast<size_t>(index)];
            const auto isConfirmed = std::any_of(confirmed.begin(), confirmed.end(), [&](const auto& group)
            {
                return group.midiNote == candidate.midiNote && group.velocity == candidate.velocity;
            });
            const auto kitId = safe->database.getPluginKitId(pluginIdentifier, kitName);
            const bool saved = kitId && (isConfirmed
                ? safe->database.removeConfirmedPluginRoundRobinGroup(*kitId, candidate.midiNote, candidate.velocity)
                : safe->database.confirmPluginRoundRobinGroup(*kitId, candidate.midiNote, candidate.velocity));
            const auto title = saved
                ? (isConfirmed ? magdrum::ui::localisedText("Confirmation removed")
                               : magdrum::ui::localisedText("Round-robin group confirmed"))
                : magdrum::ui::localisedText("Could not update group");
            const auto message = saved
                ? (isConfirmed ? magdrum::ui::localisedText("The group is no longer marked as confirmed.")
                               : magdrum::ui::localisedText("The exact audible take indices are saved as a user-confirmed group."))
                : magdrum::ui::localisedText("This candidate no longer has at least two indexed, audible takes.");
            juce::AlertWindow::showMessageBoxAsync(saved ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                title, message);
        }), false);
}

void MagDrumAudioProcessorEditor::finishMetadataFolderSync(const PortableMetadataSyncResult& result)
{
    metadataSyncRunning=false;
    if(!result.ok)
    {
        const auto error=result.metadataMerged?magdrum::ui::localisedText("Metadata merged, but the device snapshot could not be saved. Check folder permissions and sync again."):magdrum::ui::localisedText("Metadata folder sync failed. Check the folder and try again.");
        status.setText(error,juce::dontSendNotification);
        return;
    }
    refreshSamples();
    auto message=magdrum::ui::localisedText("Metadata sync saved. Merged {devices} other device(s); {matched} sample matches, {unavailable} unavailable, {tags} new tags, {profiles} new profiles.");
    message=message.replace("{devices}",juce::String(result.archivesMerged)).replace("{matched}",juce::String(result.samplesMatched)).replace("{unavailable}",juce::String(result.samplesUnavailable)).replace("{tags}",juce::String(result.tagsAdded)).replace("{profiles}",juce::String(result.profilesAdded));
    status.setText(message,juce::dontSendNotification);
    if(!result.conflicts.empty())
    {
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        magdrum::ui::showPortableMetadataConflictReview(this,result.conflicts,
            [safe](const PortableMetadataConflict& conflict,const PortableMetadataConflictCandidate& choice)
            {return safe!=nullptr&&safe->database.resolvePortableMetadataConflict(conflict,choice);},
            [safe](int applied,int failed,int skipped)
            {
                if(safe==nullptr)return;
                safe->refreshSamples();
                safe->status.setText(magdrum::ui::localisedText("Metadata conflict review: {applied} applied, {failed} failed, {skipped} left unchanged.")
                    .replace("{applied}",juce::String(applied)).replace("{failed}",juce::String(failed)).replace("{skipped}",juce::String(skipped)),
                    juce::dontSendNotification);
            });
    }
    else if(result.samplesUnavailable>0)
    {
        const auto report=magdrum::ui::localisedText("Sample identities not found on this device: {count}").replace("{count}",juce::String(result.samplesUnavailable));
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,magdrum::ui::localisedText("Metadata import report"),report,magdrum::ui::localisedText("Close"));
    }
}

void MagDrumAudioProcessorEditor::finishPortableMetadataArchive(
    bool exporting, bool databaseOpened, bool archiveValid, bool fileSaved,
    const juce::File& archiveFile, const PortableMetadataMergeResult& merged)
{
    metadataArchiveRunning=false;
    libraryToolsButton.setEnabled(true);
    if(!databaseOpened)
    {
        status.setText(magdrum::ui::localisedText("The shared MagDrum sample database is unavailable."),
                       juce::dontSendNotification);
        return;
    }
    if(exporting)
    {
        auto message=magdrum::ui::localisedText(fileSaved
            ?"Portable metadata exported.":"Could not write metadata archive.");
        if(fileSaved)message+=" "+archiveFile.getFullPathName();
        status.setText(message,juce::dontSendNotification);
        return;
    }
    if(!archiveValid)
    {
        status.setText(magdrum::ui::localisedText("The metadata archive is not valid JSON."),
                       juce::dontSendNotification);
        return;
    }

    refreshSamples();
    auto message=magdrum::ui::localisedText(merged.ok
        ?"Metadata imported: {tags} tags added, {profiles} mapping profiles added."
        :"Metadata import failed: {error}");
    message=message.replace("{tags}",juce::String(merged.tagsAdded))
        .replace("{profiles}",juce::String(merged.profilesAdded))
        .replace("{error}",merged.error);
    status.setText(message,juce::dontSendNotification);
    if(merged.ok&&!merged.conflicts.empty())
    {
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        magdrum::ui::showPortableMetadataConflictReview(this,merged.conflicts,
            [safe](const PortableMetadataConflict& conflict,const PortableMetadataConflictCandidate& choice)
            {return safe!=nullptr&&safe->database.resolvePortableMetadataConflict(conflict,choice);},
            [safe](int applied,int failed,int skipped)
            {
                if(safe==nullptr)return;
                safe->refreshSamples();
                safe->status.setText(magdrum::ui::localisedText("Metadata conflict review: {applied} applied, {failed} failed, {skipped} left unchanged.")
                    .replace("{applied}",juce::String(applied)).replace("{failed}",juce::String(failed)).replace("{skipped}",juce::String(skipped)),
                    juce::dontSendNotification);
            });
    }
}

void MagDrumAudioProcessorEditor::setPage(int value){page=juce::jlimit(0,highestEditorPage,value);resized();repaint();}
void MagDrumAudioProcessorEditor::beginPadParameterGesture(size_t parameterIndex)
{
    if (parameterIndex >= activePadParameterGestureNotes.size()
        || activePadParameterGestureNotes[parameterIndex] >= 0)
        return;
    activePadParameterGestureNotes[parameterIndex] = selectedNote;
    processor.beginNoteParameterGesture(selectedNote, parameterIndex);
}

void MagDrumAudioProcessorEditor::endPadParameterGesture(size_t parameterIndex)
{
    if (parameterIndex >= activePadParameterGestureNotes.size())
        return;
    const int note = activePadParameterGestureNotes[parameterIndex];
    if (note < 0)
        return;
    activePadParameterGestureNotes[parameterIndex] = -1;
    processor.endNoteParameterGesture(note, parameterIndex);
}

void MagDrumAudioProcessorEditor::setNote(int note)
{
    for (size_t index = 0; index < activePadParameterGestureNotes.size(); ++index)
        endPadParameterGesture(index);
    selectedNote=juce::jlimit(0,127,note);
    const int index=static_cast<int>(std::find(mappedPadNotes.begin(),mappedPadNotes.end(),selectedNote)-mappedPadNotes.begin());
    const int safeIndex=juce::jlimit(0,static_cast<int>(padNames.size())-1,index);
    for(size_t i=0;i<pads.size();++i)
    {
        pads[i].setButtonText(localizedPadName(i)+"\n"+noteName(mappedPadNotes[i]));
        pads[i].setToggleState(static_cast<int>(i)==index,juce::dontSendNotification);
        pads[i].setPadGlowColour(categoryColour(padNames[i]));
        pads[i].setColour(juce::TextButton::buttonColourId,static_cast<int>(i)==index?categoryColour(padNames[i]).withAlpha(0.54f):Palette::Surface);
    }
    selectedPadTitle.setText(localizedPadName(static_cast<size_t>(safeIndex))+"   ·   "+noteName(selectedNote),juce::dontSendNotification);
    updatingPadControls=true;
    const auto parameters=processor.getNoteParameters(selectedNote);
    chokeGroupSelect.setSelectedId(parameters.chokeGroup + 1, juce::dontSendNotification);
    waveform->setTrimRange(parameters.sampleStart,parameters.sampleEnd);
    gainSlider.setValue(parameters.gainDb,juce::dontSendNotification);
    panSlider.setValue(parameters.pan,juce::dontSendNotification);
    tuneSlider.setValue(parameters.tuneSemitones,juce::dontSendNotification);
    driveSlider.setValue(parameters.driveDb,juce::dontSendNotification);
    attackSlider.setValue(parameters.attackMs,juce::dontSendNotification);
    decaySlider.setValue(parameters.decayMs,juce::dontSendNotification);
    sustainSlider.setValue(parameters.sustainLevel * 100.0f,juce::dontSendNotification);
    releaseSlider.setValue(parameters.releaseMs,juce::dontSendNotification);
    filterCutoffSlider.setValue(parameters.filterCutoffHz,juce::dontSendNotification);
    filterResonanceSlider.setValue(parameters.filterResonance,juce::dontSendNotification);
    eqLowSlider.setValue(parameters.eqLowDb,juce::dontSendNotification);
    eqMidSlider.setValue(parameters.eqMidDb,juce::dontSendNotification);
    eqHighSlider.setValue(parameters.eqHighDb,juce::dontSendNotification);
    compressorThresholdSlider.setValue(parameters.compressorThresholdDb,juce::dontSendNotification);
    compressorRatioSlider.setValue(parameters.compressorRatio,juce::dontSendNotification);
    compressorAttackSlider.setValue(parameters.compressorAttackMs,juce::dontSendNotification);
    compressorReleaseSlider.setValue(parameters.compressorReleaseMs,juce::dontSendNotification);
    compressorMakeupSlider.setValue(parameters.compressorMakeupDb,juce::dontSendNotification);
    phaserRateSlider.setValue(parameters.phaserRateHz,juce::dontSendNotification);
    phaserDepthSlider.setValue(parameters.phaserDepth * 100.0f,juce::dontSendNotification);
    phaserFeedbackSlider.setValue(parameters.phaserFeedback,juce::dontSendNotification);
    phaserMixSlider.setValue(parameters.phaserMix * 100.0f,juce::dontSendNotification);
    updatingPadControls=false;
    updateLayerSummary();
    const auto layers=processor.getSampleLayersForNote(selectedNote);
    sampleDetails.setText(layers.empty()?magdrum::ui::localisedText("Selected pad sound")+"\n"+magdrum::ui::localisedText("No sample loaded"):magdrum::ui::localisedText("PAD SAMPLE")+"\n"+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path,juce::dontSendNotification);
    repaint(padShapePanelBounds.getUnion(padEqPanelBounds));
}
void MagDrumAudioProcessorEditor::updateDrumNotes()
{
    const int previousNote=selectedNote;
    mappedPadNotes=defaultPadNotes;
    for(size_t lane=0;lane<mappedLaneNotes.size();++lane)
    {
        const DrumEvent laneEvent{laneInstruments[lane],laneArticulations[lane],100,0.0,0.1};
        const auto mapped=mappingProfile.toMidi(laneEvent,false);
        mappedLaneNotes[lane]=mapped.note?static_cast<int>(*mapped.note):-1;
        if(lane>=9)mappedLaneNotes[lane]=defaultSequencerMidiNotes[lane];
        if (lane == 9 && mappedLaneNotes[lane] == mappedLaneNotes[6]) mappedLaneNotes[lane] = 57;
        if (lane == 12 && mappedLaneNotes[lane] == mappedLaneNotes[4]) mappedLaneNotes[lane] = 45;
    }
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
    saved.setAttribute("gridResolution",gridResolutionSubdivisions());
    const int fallbackPolicyId=juce::jlimit(1,3,midiFallbackPolicy.getSelectedId());
    saved.setAttribute("fallbackPolicy",fallbackPolicyId-1);
    saved.setAttribute("allowArticulationFallback",fallbackPolicyId>1);
    for(size_t lane=0;lane<sequenceStepActive.size();++lane)for(size_t step=0;step<sequenceStepActive[lane].size();++step)if(sequenceStepActive[lane][step]){auto* cell=saved.createNewChildElement("STEP");cell->setAttribute("lane",static_cast<int>(lane));cell->setAttribute("step",static_cast<int>(step));cell->setAttribute("velocity",stepVelocities[lane][step]);}
    for(const auto& meta:sequenceMetadata){
        auto* child=saved.createNewChildElement("META");child->setAttribute("beat",meta.positionBeats);
        if(meta.message.isTempoMetaEvent()){child->setAttribute("kind","tempo");child->setAttribute("microsecondsPerQuarterNote",juce::roundToInt(meta.message.getTempoSecondsPerQuarterNote()*1000000.0));}
        else if(meta.message.isTimeSignatureMetaEvent()){int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);child->setAttribute("kind","meter");child->setAttribute("numerator",numerator);child->setAttribute("denominator",denominator);}
    }
    for(const auto& event:sequenceEvents){auto* child=saved.createNewChildElement("EVENT");child->setAttribute("instrument",static_cast<int>(event.instrument));child->setAttribute("articulation",static_cast<int>(event.articulation));child->setAttribute("velocity",static_cast<int>(event.velocity));child->setAttribute("beat",event.positionBeats);child->setAttribute("duration",event.durationBeats);}
    processor.setPluginEditorState(saved);
    processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
}

void MagDrumAudioProcessorEditor::restorePluginEditorState()
{
    const auto saved=processor.getPluginEditorState();if(!saved)return;
    const int storedFallbackPolicy=saved->getIntAttribute("fallbackPolicy",-1);
    const int fallbackPolicyId=storedFallbackPolicy>=0&&storedFallbackPolicy<=2
        ?storedFallbackPolicy+1:(saved->getBoolAttribute("allowArticulationFallback",false)?3:1);
    midiFallbackPolicy.setSelectedId(fallbackPolicyId,juce::dontSendNotification);
    const auto name=saved->getStringAttribute("profileName");const auto csv=saved->getStringAttribute("profileCsv");
    if(name.isNotEmpty()&&csv.isNotEmpty())if(const auto parsed=MidiMappingProfile::fromCsv(csv.toStdString())){mappingProfile=*parsed;profileName.setText(name,juce::dontSendNotification);if(name!="General MIDI"&&databaseReady){database.saveMappingProfile(name,mappingProfile);int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);}}
    const auto tempo=saved->getStringAttribute("tempo");if(tempo.isNotEmpty())tempoEditor.setText(tempo,juce::dontSendNotification);
    const auto savedGrid=saved->getIntAttribute("gridResolution",4);if(magdrum::sequencer::isSupportedSubdivision(savedGrid))gridResolutionSelect.setSelectedId(savedGrid,juce::dontSendNotification);previousGridResolution=gridResolutionSubdivisions();
    const auto meter=saved->getStringAttribute("meter");const int slash=meter.indexOfChar('/');if(slash>0){const int id=meter.substring(0,slash).getIntValue()*100+meter.substring(slash+1).getIntValue();if(meterSelect.indexOfItemId(id)>=0)meterSelect.setSelectedId(id,juce::dontSendNotification);}previousMeterId=meterSelect.getSelectedId();
    const int savedBars=juce::jlimit(1,magdrum::sequencer::maximumBars,saved->getIntAttribute("bars",magdrum::sequencer::maximumBars));lengthSelect.setSelectedId(savedBars,juce::dontSendNotification);previousPatternBars=savedBars;
    sequenceMetadata.clear();sequenceEvents.clear();sequenceEventsInitialised=saved->getIntAttribute("eventModel",0)==1;
    for(auto& row:sequenceStepActive)row.fill(false);
    for(auto& row:stepVelocities)row.fill(100);
    for(auto* child=saved->getFirstChildElement();child!=nullptr;child=child->getNextElement()){
        if(child->hasTagName("STEP")){const int lane=child->getIntAttribute("lane",-1),step=child->getIntAttribute("step",-1);if(juce::isPositiveAndBelow(lane,static_cast<int>(sequenceStepActive.size()))&&juce::isPositiveAndBelow(step,512)){const auto r=static_cast<size_t>(lane),s=static_cast<size_t>(step);sequenceStepActive[r][s]=true;stepVelocities[r][s]=juce::jlimit(1,127,child->getIntAttribute("velocity",100));}}
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
    chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText(exportCsv?"Export MIDI mapping profile":"Import MIDI mapping profile"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Mapping.csv"),"*.csv");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(exportCsv?(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles):(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles),[safe,exportCsv](const juce::FileChooser& fc){if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(exportCsv){if(file.getFileExtension().isEmpty())file=file.withFileExtension(".csv");const bool ok=file.replaceWithText(juce::String::fromUTF8(safe->mappingProfile.toCsv().c_str()));safe->status.setText(magdrum::ui::localisedText(ok?"MIDI mapping profile exported.":"Could not write CSV profile."),juce::dontSendNotification);}else{const auto parsed=MidiMappingProfile::fromCsv(file.loadFileAsString().toStdString());if(!parsed){safe->status.setText(magdrum::ui::localisedText("The CSV file is not a valid MIDI mapping profile."),juce::dontSendNotification);return;}safe->mappingProfile=*parsed;if(safe->profileName.getText().trim().isEmpty())safe->profileName.setText(file.getFileNameWithoutExtension(),juce::dontSendNotification);safe->database.saveMappingProfile(safe->profileName.getText(),safe->mappingProfile);safe->updateDrumNotes();safe->refreshMappingSummary();safe->savePluginEditorState();safe->status.setText(magdrum::ui::localisedText("Imported MIDI mapping profile."),juce::dontSendNotification);}});
}

void MagDrumAudioProcessorEditor::importMidiPattern()
{
    chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Import drum MIDI pattern"),juce::File(),"*.mid;*.midi");juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe!=nullptr)safe->loadMidiPatternFile(fc.getResult());});
}

void MagDrumAudioProcessorEditor::loadMidiPatternFile(const juce::File& file)
{
    if(!file.existsAsFile())return;
    MidiPattern pattern;
    const auto report=MidiPatternIO{}.read(file,mappingProfile,pattern);
    if(!report.ok){status.setText(magdrum::ui::localisedText("Could not import the selected MIDI file."),juce::dontSendNotification);return;}
    int importedNumerator=4,importedDenominator=4;
    bool foundInitialMeter=false,unsupportedMeter=false,unsupportedMeterGrid=false;
    for(const auto& meta:pattern.tempoAndSignature)
        if(meta.message.isTimeSignatureMetaEvent())
        {
            int numerator=4,denominator=4;meta.message.getTimeSignatureInfo(numerator,denominator);
            if(std::abs(meta.positionBeats)<0.000001){importedNumerator=numerator;importedDenominator=denominator;foundInitialMeter=true;}
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
    int selectedResolution=0,fallbackResolution=0;
    for(const int subdivisions:{4,8,2})
    {
        const auto candidate=makeSequencerBarMap(timeSignatureNumerator(),timeSignatureDenominator(),magdrum::sequencer::maximumBars,sequenceMetadata,subdivisions);
        if(!candidate.valid)continue;
        fallbackResolution=subdivisions;
        const bool notesAligned=std::all_of(pattern.events.begin(),pattern.events.end(),[subdivisions](const auto& event)
        {
            return std::abs(event.positionBeats*subdivisions-std::round(event.positionBeats*subdivisions))<=magdrum::sequencer::gridBeatTolerance;
        });
        const bool metersAligned=std::all_of(sequenceMetadata.begin(),sequenceMetadata.end(),[subdivisions](const auto& meta)
        {
            return !meta.message.isTimeSignatureMetaEvent()
                || std::abs(meta.positionBeats*subdivisions-std::round(meta.positionBeats*subdivisions))<=magdrum::sequencer::gridBeatTolerance;
        });
        if(notesAligned&&metersAligned){selectedResolution=subdivisions;break;}
    }
    if(selectedResolution==0)selectedResolution=fallbackResolution>0?fallbackResolution:4;
    gridResolutionSelect.setSelectedId(selectedResolution,juce::dontSendNotification);previousGridResolution=selectedResolution;
    refreshSequenceMetadata();
    int importedBars=magdrum::sequencer::maximumBars;
    for(int bars=1;bars<=magdrum::sequencer::maximumBars;++bars)
    {
        const auto map=makeSequencerBarMap(timeSignatureNumerator(),timeSignatureDenominator(),bars,sequenceMetadata,gridResolutionSubdivisions());
        if(!map.valid){unsupportedMeterGrid=true;break;}
        if(map.lengthBeats+magdrum::sequencer::gridBeatTolerance>=pattern.lengthBeats){importedBars=bars;break;}
    }
    if(unsupportedMeterGrid)
    {
        const double beatsPerBar=static_cast<double>(timeSignatureNumerator())*4.0/static_cast<double>(timeSignatureDenominator());
        importedBars=juce::jlimit(1,magdrum::sequencer::maximumBars,static_cast<int>(std::ceil(std::max(0.0,pattern.lengthBeats)/beatsPerBar-1.0e-9)));
    }
    lengthSelect.setSelectedId(importedBars,juce::dontSendNotification);previousPatternBars=importedBars;
    updateSequencerGrid();
    for(auto& row:sequenceStepActive)row.fill(false);
    for(auto& row:stepVelocities)row.fill(100);
    int imported=0,outside=0;
    for(const auto& event:pattern.events)
    {
        int lane=-1;
        switch(event.instrument){case DrumInstrument::Kick:lane=0;break;case DrumInstrument::Snare:case DrumInstrument::Rimshot:lane=1;break;case DrumInstrument::HiHat:lane=event.articulation==DrumArticulation::Open?3:2;break;case DrumInstrument::Tom:lane=4;break;case DrumInstrument::Ride:lane=5;break;case DrumInstrument::Crash:lane=6;break;case DrumInstrument::Clap:lane=7;break;default:break;}
        const int step=juce::roundToInt(event.positionBeats*gridResolutionSubdivisions());
        if(lane<0||step<0||step>=patternStepCount()){++outside;continue;}
        const auto row=static_cast<size_t>(lane),column=static_cast<size_t>(step);
        sequenceStepActive[row][column]=true;
        stepVelocities[row][column]=event.velocity;
        ++imported;
    }
    refreshStepTooltips();
    for(const auto& meta:pattern.tempoAndSignature)if(std::abs(meta.positionBeats)<1.0e-9&&meta.message.isTempoMetaEvent()){const double seconds=meta.message.getTempoSecondsPerQuarterNote();if(seconds>0.0)tempoEditor.setText(juce::String(juce::roundToInt(60.0/seconds)),juce::dontSendNotification);break;}
    sequenceEvents=pattern.events;sequenceEventsInitialised=true;importedMidiBaseline=currentSequencePattern();hasImportedMidiBaseline=true;reviewMidiButton.setEnabled(true);editMidiEventsButton.setEnabled(true);
    savePluginEditorState();
    auto importMessage=magdrum::ui::localisedText("Imported {hits} hits from {file}. {unmapped} unmapped notes; {ambiguous} ambiguous notes; {outside} hits outside the imported pattern length.{unsupported}{meterChanges}");importMessage=importMessage.replace("{hits}",juce::String(imported)).replace("{file}",file.getFileName()).replace("{unmapped}",juce::String(report.unmappedNotes.size())).replace("{ambiguous}",juce::String(report.ambiguousNotes.size())).replace("{outside}",juce::String(outside)).replace("{unsupported}",unsupportedMeter?" "+magdrum::ui::localisedText("Unsupported meter; used 4/4."):juce::String()).replace("{meterChanges}",unsupportedMeterGrid?" "+magdrum::ui::localisedText("Later meter events are preserved for MIDI export; the grid shows the opening meter."):juce::String());status.setText(importMessage,juce::dontSendNotification);
}
void MagDrumAudioProcessorEditor::updateLayerSummary()
{
    const auto layers=processor.getSampleLayersForNote(selectedNote);juce::String text;
    if(layers.empty())text=magdrum::ui::localisedText("No sample loaded")+"\n"+magdrum::ui::localisedText("Load sample");
    else for(size_t i=0;i<layers.size();++i){if(i>0)text+="\n";text+=magdrum::ui::localisedText("Velocity")+" "+juce::String(layers[i].minimumVelocity)+"–"+juce::String(layers[i].maximumVelocity);if(layers[i].roundRobinCount>1)text+=" · RR "+juce::String(layers[i].roundRobinIndex)+"/"+juce::String(layers[i].roundRobinCount);text+="\n"+juce::File(layers[i].path).getFileName();}
    layerSummary.setText(text,juce::dontSendNotification);
    sampleDetails.setText(magdrum::ui::localisedText("PAD SAMPLE")+(layers.empty()?"\n"+magdrum::ui::localisedText("No sample loaded"):"\n"+juce::File(layers.front().path).getFileName()+"\n"+layers.front().path),juce::dontSendNotification);
    waveform->setFile(layers.empty()?juce::File():juce::File(layers.front().path));
}

void MagDrumAudioProcessorEditor::manageSampleLayers()
{
    const auto layers = processor.getSampleLayersForNote(selectedNote);
    if (layers.empty())
    {
        status.setText(magdrum::ui::localisedText("No sample layers are assigned to this pad."),
                       juce::dontSendNotification);
        return;
    }

    juce::PopupMenu menu;
    for (size_t index = 0; index < layers.size(); ++index)
    {
        const auto& layer = layers[index];
        juce::PopupMenu actions;
        const int editAction = 100 + static_cast<int>(index) * 3;
        actions.addItem(editAction, magdrum::ui::localisedText("Preview layer"));
        actions.addItem(editAction + 1, magdrum::ui::localisedText("Edit velocity range"));
        actions.addItem(editAction + 2, magdrum::ui::localisedText("Remove layer"));
        auto label = magdrum::ui::localisedText("Velocity") + " "
            + juce::String(layer.minimumVelocity) + "-" + juce::String(layer.maximumVelocity)
            + " - " + juce::File(layer.path).getFileName();
        if (layer.roundRobinCount > 1)
            label += " · RR " + juce::String(layer.roundRobinIndex) + "/"
                + juce::String(layer.roundRobinCount);
        menu.addSubMenu(label, actions);
    }

    const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    const int note = selectedNote;
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&manageLayersButton),
        [safe, layers, note](int action)
        {
            if (safe == nullptr || action < 100)
                return;
            const size_t index = static_cast<size_t>((action - 100) / 3);
            if (index >= layers.size())
                return;
            safe->handleSampleLayerMenuAction(note, layers[index], (action - 100) % 3);
        });
}

void MagDrumAudioProcessorEditor::handleSampleLayerMenuAction(
    int note, const MagDrumAudioProcessor::SampleLayerInfo& layer, int action)
{
    if (action == 0)
    {
        previewSampleAsync(juce::File(layer.path));
        return;
    }
    if (action == 1)
    {
        editSampleLayerVelocityRange(layer);
        return;
    }
    if (action != 2)
        return;
    if (!processor.removeSampleLayer(note, layer.slotIndex))
    {
        status.setText(magdrum::ui::localisedText("Could not remove sample layer."),
                       juce::dontSendNotification);
        return;
    }
    processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    updateLayerSummary();
    refreshSampleAssignmentLabels();
    status.setText(magdrum::ui::localisedText("Layer removed."), juce::dontSendNotification);
}

void MagDrumAudioProcessorEditor::editSampleLayerVelocityRange(
    const MagDrumAudioProcessor::SampleLayerInfo& layer)
{
    if (layer.slotIndex < 0)
        return;
    auto* dialog = new juce::AlertWindow(magdrum::ui::localisedText("Edit velocity range"),
        magdrum::ui::localisedText("Enter an inclusive velocity range, for example 1-63."),
        juce::AlertWindow::NoIcon);
    dialog->addTextEditor("range", juce::String(layer.minimumVelocity) + "-"
        + juce::String(layer.maximumVelocity), magdrum::ui::localisedText("Velocity range"));
    if (auto* rangeEditor = dialog->getTextEditor("range"))
    {
        rangeEditor->setInputRestrictions(7, "0123456789-");
        rangeEditor->setJustification(juce::Justification::centred);
    }
    dialog->addButton(magdrum::ui::localisedText("Apply range"), 1);
    dialog->addButton(magdrum::ui::localisedText("Cancel"), 0);
    dialog->setSize(440, 190);
    const juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
    const juce::Component::SafePointer<juce::AlertWindow> safeDialog(dialog);
    const int note = selectedNote;
    const int slotIndex = layer.slotIndex;
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [safe, safeDialog, note, slotIndex](int result)
        {
            if (result != 1 || safe == nullptr || safeDialog == nullptr)
                return;
            const auto range = safeDialog->getTextEditorContents("range");
            const int minimumVelocity = velocityMinimum(range);
            const int maximumVelocity = velocityMaximum(range);
            if (!safe->processor.updateSampleLayerVelocityRange(note, slotIndex,
                    minimumVelocity, maximumVelocity))
            {
                safe->status.setText(magdrum::ui::localisedText(
                    "Velocity range overlaps another layer or is invalid."), juce::dontSendNotification);
                return;
            }
            safe->processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
            safe->updateLayerSummary();
            safe->refreshSampleAssignmentLabels();
            safe->status.setText(magdrum::ui::localisedText("Velocity range updated."),
                                 juce::dontSendNotification);
        }), true);
}

juce::String MagDrumAudioProcessorEditor::currentWaveformTooltip() const
{
    return waveform->getTooltip();
}

void MagDrumAudioProcessorEditor::buttonClicked(juce::Button* button)
{
    if(button==&manageLayersButton){manageSampleLayers();return;}
    if(button==&compressorTab){showPhaserControls=false;resized();return;}
    if(button==&phaserTab){showPhaserControls=true;resized();return;}
    if(button==&libraryTab){setPage(0);return;}if(button==&machineTab){setPage(1);return;}if(button==&sequencerTab){setPage(2);return;}if(button==&mapsTab){setPage(3);return;}
    if(button==&refreshPluginInventoryButton){refreshPluginInventory();return;}if(button==&scanPluginInventoryButton){startPluginInventoryScan();return;}if(button==&choosePluginFolderButton){choosePluginFolder();return;}if(button==&openPluginManagerButton){launchInstrumentScanner();return;}if(button==&choosePluginPresetButton){choosePluginPreset();return;}if(button==&importPluginPresetButton){startPluginPresetImport();return;}if(button==&scanPluginProgramsButton){startPluginProgramDiscovery();return;}if(button==&importPluginProgramButton){startPluginProgramImport();return;}if(button==&scanPluginNotesButton){startPluginNoteScan();return;}if(button==&cancelPluginNotesButton){cancelPluginNoteScan();return;}
     for(size_t lane=0;lane<steps.size();++lane)for(size_t localStep=0;localStep<steps[lane].size();++localStep)if(button==&steps[lane][localStep]){const int globalStep=visibleStepIndex(static_cast<int>(localStep));if(globalStep<0)return;sequenceStepActive[lane][static_cast<size_t>(globalStep)]=steps[lane][localStep].getToggleState();const bool preservedOffGrid=syncSequenceEventForCell(static_cast<int>(lane),globalStep);if(preservedOffGrid)status.setText(magdrum::ui::localisedText("Off-grid hits in this step remain; use Edit hits to review them."),juce::dontSendNotification);savePluginEditorState();return;}
    if(button==&scanButton)
    {
        if(!scanning&&lastSampleScanFolder.isDirectory())
        {
            juce::PopupMenu menu;menu.addItem(1,magdrum::ui::localisedText("Resume scan"));menu.addItem(2,magdrum::ui::localisedText("Add sample folder"));
            juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&scanButton),[safe](int choice){if(safe==nullptr)return;if(choice==1)safe->startSampleScan(safe->lastSampleScanFolder);else if(choice==2)safe->chooseSampleFolder(true);});
        }
        else chooseSampleFolder();
        return;
    }
    if(button==&scanDefaultsButton){scanDefaultLibraryFolders();return;}
    if(button==&instrumentScannerButton){setPage(4);refreshPluginInventory();if(discoveredPluginInstruments.isEmpty()&&!pluginInventoryScanRunning)startPluginInventoryScan();return;}
    if(button==&pluginScannerTab){setPage(4);refreshPluginInventory();if(discoveredPluginInstruments.isEmpty()&&!pluginInventoryScanRunning)startPluginInventoryScan();return;}
    if(button==&openSourceKitButton){openSourceKitForSelectedSample();return;}
    if(button==&searchButton){refreshSamples();return;}if(button==&libraryToolsButton){showLibraryTools();return;}if(button==&moreSamplesButton){if(hasMoreSamples&&!sampleBrowseQueryPending)loadSamplePage(false);return;}
    if(button==&previewButton){const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample to preview."),juce::dontSendNotification);return;}previewSampleAsync(file);return;}
    if(button==&stopPreviewButton){stopSamplePreview();return;}
    if(button==&assignButton){const auto file=selectedSample();if(file==juce::File()){status.setText(magdrum::ui::localisedText("Select a sample first."),juce::dontSendNotification);return;}loadSampleAsync(file,selectedNote,velocityMinimum(velocityRange.getText()),velocityMaximum(velocityRange.getText()),true);return;}
    for(size_t i=0;i<pads.size();++i)if(button==&pads[i]){pads[i].flash();setNote(mappedPadNotes[i]);processor.triggerNote(selectedNote);status.setText(processor.getSampleLayersForNote(selectedNote).empty()?magdrum::ui::localisedText("Pad selected · no sample assigned yet."):magdrum::ui::localisedText("Played ")+localizedPadName(i),juce::dontSendNotification);return;}
    if(button==&clearPadButton){const bool cleared=processor.clearSamplesForNote(selectedNote);if(cleared){processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));refreshSampleAssignmentLabels();}status.setText(cleared?magdrum::ui::localisedText("Pad cleared."):magdrum::ui::localisedText("Pad is already empty."),juce::dontSendNotification);updateLayerSummary();return;}
    if(button==&loadButton){chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Choose a drum sample"),juce::File(),"*.wav;*.aif;*.aiff;*.flac",true,false,this);juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe](const juce::FileChooser& fc){if(safe==nullptr)return;const auto file=fc.getResult();if(!file.existsAsFile())return;safe->loadSampleAsync(file,safe->selectedNote,velocityMinimum(safe->velocityRange.getText()),velocityMaximum(safe->velocityRange.getText()),false);});return;}
    if(button==&loadProfileButton){const auto name=profileSelect.getText();if(name=="General MIDI"){mappingProfile=MidiMappingProfile::generalMidi();}else if(const auto loaded=database.loadMappingProfile(name))mappingProfile=*loaded;else{status.setText(magdrum::ui::localisedText("Could not load mapping profile."),juce::dontSendNotification);return;}profileName.setText(name,juce::dontSendNotification);updateDrumNotes();refreshMappingSummary();status.setText(magdrum::ui::localisedText("Loaded MIDI profile: ")+name,juce::dontSendNotification);return;}
    if(button==&saveProfileButton){const auto name=profileName.getText().trim();if(name.isEmpty()||!database.saveMappingProfile(name,mappingProfile)){status.setText(magdrum::ui::localisedText("Could not save mapping profile."),juce::dontSendNotification);return;}int id=0;for(int i=0;i<profileSelect.getNumItems();++i)if(profileSelect.getItemText(i)==name)id=profileSelect.getItemId(i);if(id==0){id=profileSelect.getNumItems()+1;profileSelect.addItem(name,id);}profileSelect.setSelectedId(id,juce::dontSendNotification);savePluginEditorState();status.setText(magdrum::ui::localisedText("Saved MIDI profile: ")+name,juce::dontSendNotification);return;}
    if(button==&setMappingButton||button==&removeMappingButton){const int instrument=mappingInstrument.getSelectedId()-1,articulation=mappingArticulation.getSelectedId()-1,note=mappingNote.getText().getIntValue();if(instrument<0||articulation<0||(button==&setMappingButton&&(note<0||note>127))){status.setText(magdrum::ui::localisedText("Choose a drum, articulation and MIDI note from 0 to 127."),juce::dontSendNotification);return;}const bool changed=button==&setMappingButton?mappingProfile.setMapping({static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation),static_cast<std::uint8_t>(note)}):mappingProfile.removeMapping(static_cast<DrumInstrument>(instrument),static_cast<DrumArticulation>(articulation));if(changed){database.saveMappingProfile(profileName.getText().trim().isEmpty()?"General MIDI":profileName.getText().trim(),mappingProfile);updateDrumNotes();refreshMappingSummary();savePluginEditorState();status.setText(magdrum::ui::localisedText("MIDI mapping updated."),juce::dontSendNotification);}else status.setText(magdrum::ui::localisedText("No matching mapping was changed."),juce::dontSendNotification);return;}
    if(button==&importCsvButton){chooseMappingCsv(false);return;}if(button==&exportCsvButton){chooseMappingCsv(true);return;}if(button==&importMidiButton){importMidiPattern();return;}if(button==&browseMidiButton){browseDefaultMidiPatterns();return;}if(button==&editMidiEventsButton){openMidiEventEditor();return;}if(button==&reviewMidiButton){reviewMidiChanges();return;}
    if(button==&setMetadataButton){editSequenceMetadata(false);return;}if(button==&removeMetadataButton){editSequenceMetadata(true);return;}
    if(button==&playSequenceButton){if(sequencerPlaying){sequencerPlaying=false;processor.setSequencerPlaybackEnabled(false);stopTimer();playSequenceButton.setButtonText(magdrum::ui::localisedText("Play"));}else{const int bpm=sequenceTempoAtBeat(0.0);if(bpm<20||bpm>300){status.setText(magdrum::ui::localisedText("Tempo must be between 20 and 300 BPM."),juce::dontSendNotification);return;}savePluginEditorState();sequenceTempo=bpm;sequenceStep=0;sequencerPlaying=true;processor.setSequencerPlaybackEnabled(true);playSequenceButton.setButtonText(magdrum::ui::localisedText("Stop"));const int unmapped=processor.getSequencerUnmappedEventCount();if(unmapped>0)status.setText(magdrum::ui::localisedText("Sequencer skips {count} hits with no exact MIDI mapping. Review MIDI Maps.").replace("{count}",juce::String(unmapped)),juce::dontSendNotification);startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*gridResolutionSubdivisions()))));}return;}
    if(button==&clearSequenceButton){for(auto& row:sequenceStepActive)row.fill(false);for(auto& row:stepVelocities)row.fill(100);refreshStepTooltips();syncSequenceEventsFromGrid();savePluginEditorState();status.setText(magdrum::ui::localisedText("Pattern cleared."),juce::dontSendNotification);return;}
    if(button==&exportSequenceButton)
    {
        const int bpm=tempoEditor.getText().getIntValue();
        if(bpm<20||bpm>300){status.setText(magdrum::ui::localisedText("Tempo must be between 20 and 300 BPM."),juce::dontSendNotification);return;}
        savePluginEditorState();
        auto pattern=currentSequencePattern();
        const auto exportMapping=mappingProfile;
        const auto fallbackPolicy=static_cast<MidiFallbackPolicy>(juce::jlimit(0,2,midiFallbackPolicy.getSelectedId()-1));
        chooser=std::make_unique<juce::FileChooser>(magdrum::ui::localisedText("Export drum pattern"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("MagDrum-Pattern.mid"),"*.mid",true,false,this);
        juce::Component::SafePointer<MagDrumAudioProcessorEditor> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles,[safe,pattern,exportMapping,fallbackPolicy](const juce::FileChooser& fc)
        {
            if(safe==nullptr)return;auto file=fc.getResult();if(file==juce::File())return;if(file.getFileExtension().isEmpty())file=file.withFileExtension(".mid");
            std::vector<DrumEvent> unsupported;std::vector<MidiFallbackUse> fallbacks;
            const bool ok=MidiPatternIO{}.write(file,pattern,exportMapping,fallbackPolicy,&unsupported,&fallbacks);
            auto message=magdrum::ui::localisedText(ok?"MIDI exported; %1 fallback mapping(s), %2 unsupported event(s) omitted.":"Could not write MIDI file.");message=message.replace("%1",juce::String(fallbacks.size())).replace("%2",juce::String(unsupported.size()));safe->status.setText(message,juce::dontSendNotification);
            if(ok&&(!fallbacks.empty()||!unsupported.empty()))
            {
                juce::String report;
                for(const auto& fallback:fallbacks)
                    report+=magdrum::ui::localisedText("Fallback: %1 mapped to %2 at MIDI note %3.")
                        .replace("%1",describeMidiMapping(fallback.requestedEvent.instrument,fallback.requestedEvent.articulation))
                        .replace("%2",describeMidiMapping(fallback.requestedEvent.instrument,fallback.mappedArticulation))
                        .replace("%3",juce::String(fallback.midiNote))+"\n";
                for(const auto& event:unsupported)
                    report+=magdrum::ui::localisedText("Omitted: %1, velocity %2.")
                        .replace("%1",describeMidiMapping(event.instrument,event.articulation))
                        .replace("%2",juce::String(event.velocity))+"\n";
                juce::AlertWindow::showMessageBoxAsync(unsupported.empty()?juce::MessageBoxIconType::InfoIcon:juce::MessageBoxIconType::WarningIcon,
                    magdrum::ui::localisedText("MIDI export mapping report"),report,magdrum::ui::localisedText("Close"));
            }
        });
        return;
    }
}

void MagDrumAudioProcessorEditor::timerCallback()
{
    if(!sequencerPlaying)return;
    if(processor.isSequencerHostSynchronized())
    {
        // Keep polling while Cubase owns the sequencer clock. Without this,
        // the one-shot timer expires during host playback and cannot resume
        // the local preview after the host transport stops.
        startTimer(100);
        return;
    }
    const int step=sequenceStep;
    for(const auto& event:sequenceEvents)
        if(juce::roundToInt(event.positionBeats*gridResolutionSubdivisions())==step)
            if(const auto mapped=mappingProfile.toMidi(event,false);mapped.note)
                processor.triggerNote(*mapped.note,event.velocity);
    sequenceStep=(sequenceStep+1)%patternStepCount();
    sequenceTempo=sequenceTempoAtBeat(static_cast<double>(sequenceStep)/gridResolutionSubdivisions());
    startTimer(juce::jmax(1,juce::roundToInt(60000.0/(sequenceTempo*gridResolutionSubdivisions()))));
}

void MagDrumAudioProcessorEditor::updatePadActivity()
{
    std::array<bool, 128> triggered{};
    for (int note = 0; note < static_cast<int>(triggered.size()); ++note)
        triggered[static_cast<size_t>(note)] = processor.consumeNoteTriggerCount(note) > 0;

    if (page != 1)
        return;

    for (size_t pad = 0; pad < pads.size(); ++pad)
    {
        const int note = mappedPadNotes[pad];
        if (juce::isPositiveAndBelow(note, static_cast<int>(triggered.size()))
            && triggered[static_cast<size_t>(note)])
            pads[pad].flash();
    }
}
