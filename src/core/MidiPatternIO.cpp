#include "MidiPatternIO.h"
#include <algorithm>
#include <map>

MidiImportReport MidiPatternIO::read(const juce::File& file,const MidiMappingProfile& profile,MidiPattern& output) const
{
    MidiImportReport report;
    output={};
    juce::FileInputStream input(file);
    juce::MidiFile midi;
    if(!input.openedOk()||!midi.readFrom(input)){report.error="Could not read Standard MIDI File";return report;}
    const int ticksPerQuarter=midi.getTimeFormat();
    if(ticksPerQuarter<=0){report.error="SMPTE time division is not supported; use a PPQ MIDI file";return report;}
    using Key=std::pair<int,int>;
    std::map<Key,std::vector<std::pair<double,std::uint8_t>>> pending;
    for(int track=0;track<midi.getNumTracks();++track)
    {
        const auto* sequence=midi.getTrack(track);if(sequence==nullptr)continue;
        for(int i=0;i<sequence->getNumEvents();++i)
        {
            const auto* item=sequence->getEventPointer(i);if(item==nullptr)continue;
            const auto& message=item->message;const double beat=item->message.getTimeStamp()/ticksPerQuarter;
            if(message.isTempoMetaEvent()||message.isTimeSignatureMetaEvent())
                output.tempoAndSignature.push_back({beat,message});
            if(message.isNoteOn())
            {
                const Key key{message.getChannel(),message.getNoteNumber()};
                pending[key].push_back({beat,static_cast<std::uint8_t>(juce::jlimit(0,127,message.getVelocity()))});
            }
            else if(message.isNoteOff())
            {
                const Key key{message.getChannel(),message.getNoteNumber()};auto found=pending.find(key);
                if(found==pending.end()||found->second.empty())continue;
                const auto start=found->second.front();found->second.erase(found->second.begin());
                auto event=profile.fromMidi(static_cast<std::uint8_t>(message.getNoteNumber()),start.second,start.first,juce::jmax(0.0,beat-start.first));
                if(event)output.events.push_back(*event);else report.unmappedNotes.push_back(message.getNoteNumber());
            }
        }
    }
    // Keep malformed files with missing note-offs usable; assign a short default gate.
    for(const auto& [key,starts]:pending)for(const auto& start:starts){auto event=profile.fromMidi(static_cast<std::uint8_t>(key.second),start.second,start.first,0.1);if(event)output.events.push_back(*event);else report.unmappedNotes.push_back(key.second);}
    std::stable_sort(output.events.begin(),output.events.end(),[](const auto&a,const auto&b){return a.positionBeats<b.positionBeats;});
    std::stable_sort(output.tempoAndSignature.begin(),output.tempoAndSignature.end(),[](const auto&a,const auto&b){return a.positionBeats<b.positionBeats;});
    report.ok=true;return report;
}

bool MidiPatternIO::write(const juce::File& file,const MidiPattern& pattern,const MidiMappingProfile& profile,
                          bool allowFallback,std::vector<DrumEvent>* unsupported) const
{
    constexpr int ppq=960;juce::MidiFile midi;midi.setTicksPerQuarterNote(ppq);juce::MidiMessageSequence sequence;
    auto metadata=pattern.tempoAndSignature;
    if(std::none_of(metadata.begin(),metadata.end(),[](const auto&m){return m.message.isTempoMetaEvent();}))metadata.push_back({0.0,juce::MidiMessage::tempoMetaEvent(500000)});
    if(std::none_of(metadata.begin(),metadata.end(),[](const auto&m){return m.message.isTimeSignatureMetaEvent();}))metadata.push_back({0.0,juce::MidiMessage::timeSignatureMetaEvent(4,4)});
    for(const auto& meta:metadata)if(meta.positionBeats>=0.0&&(meta.message.isTempoMetaEvent()||meta.message.isTimeSignatureMetaEvent()))sequence.addEvent(meta.message,meta.positionBeats*ppq);
    for(const auto& event:pattern.events)
    {
        const auto mapped=profile.toMidi(event,allowFallback);
        if(!mapped.note){if(unsupported)unsupported->push_back(event);continue;}
        const auto note=*mapped.note;const int velocity=juce::jlimit(1,127,static_cast<int>(event.velocity));
        const double start=juce::jmax(0.0,event.positionBeats)*ppq;const double end=(juce::jmax(0.0,event.positionBeats)+juce::jmax(0.0,event.durationBeats))*ppq;
        sequence.addEvent(juce::MidiMessage::noteOn(10,note,static_cast<juce::uint8>(velocity)),start);
        sequence.addEvent(juce::MidiMessage::noteOff(10,note),end);
    }
    sequence.updateMatchedPairs();sequence.sort();midi.addTrack(sequence);
    auto stream=file.createOutputStream();if(!stream)return false;const bool ok=midi.writeTo(*stream);stream->flush();return ok;
}
