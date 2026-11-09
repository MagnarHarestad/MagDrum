#include "MidiPatternIO.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace
{
constexpr double patternComparisonTolerance = 1.0e-6;

bool sameDrumEvent(const DrumEvent& left, const DrumEvent& right)
{
    return left.instrument == right.instrument && left.articulation == right.articulation
        && left.velocity == right.velocity
        && std::abs(left.positionBeats - right.positionBeats) <= patternComparisonTolerance
        && std::abs(left.durationBeats - right.durationBeats) <= patternComparisonTolerance;
}

bool sameMetaEvent(const MidiMetaEvent& left, const MidiMetaEvent& right)
{
    if (std::abs(left.positionBeats - right.positionBeats) > patternComparisonTolerance
        || left.message.getRawDataSize() != right.message.getRawDataSize())
        return false;
    if (left.message.getRawDataSize() == 0) return true;
    return std::equal(left.message.getRawData(), left.message.getRawData() + left.message.getRawDataSize(),
                      right.message.getRawData());
}

void replaceMetaAtBeat(MidiPattern& pattern, bool tempo, const juce::MidiMessage& replacement, double positionBeats)
{
    pattern.tempoAndSignature.erase(std::remove_if(pattern.tempoAndSignature.begin(), pattern.tempoAndSignature.end(),
        [tempo, positionBeats](const MidiMetaEvent& item)
        {
            const bool sameType = tempo ? item.message.isTempoMetaEvent() : item.message.isTimeSignatureMetaEvent();
            return sameType && std::abs(item.positionBeats - positionBeats) < 1.0e-9;
        }), pattern.tempoAndSignature.end());
    pattern.tempoAndSignature.push_back({positionBeats, replacement});
    std::stable_sort(pattern.tempoAndSignature.begin(), pattern.tempoAndSignature.end(),
        [](const auto& left, const auto& right) { return left.positionBeats < right.positionBeats; });
}
}

MidiPatternDiff compareMidiPatterns(const MidiPattern& original, const MidiPattern& current)
{
    MidiPatternDiff result;
    std::vector<bool> matchedCurrentEvents(current.events.size(), false);
    for (const auto& originalEvent : original.events)
    {
        size_t match = current.events.size();
        for (size_t index = 0; index < current.events.size(); ++index)
            if (!matchedCurrentEvents[index] && sameDrumEvent(originalEvent, current.events[index]))
            {
                match = index;
                break;
            }
        if (match == current.events.size()) result.removedEvents.push_back(originalEvent);
        else matchedCurrentEvents[match] = true;
    }
    for (size_t index = 0; index < current.events.size(); ++index)
        if (!matchedCurrentEvents[index]) result.addedEvents.push_back(current.events[index]);

    std::vector<bool> matchedCurrentMetadata(current.tempoAndSignature.size(), false);
    for (const auto& originalMeta : original.tempoAndSignature)
    {
        size_t match = current.tempoAndSignature.size();
        for (size_t index = 0; index < current.tempoAndSignature.size(); ++index)
            if (!matchedCurrentMetadata[index] && sameMetaEvent(originalMeta, current.tempoAndSignature[index]))
            {
                match = index;
                break;
            }
        if (match == current.tempoAndSignature.size()) result.removedMetadata.push_back(originalMeta);
        else matchedCurrentMetadata[match] = true;
    }
    for (size_t index = 0; index < current.tempoAndSignature.size(); ++index)
        if (!matchedCurrentMetadata[index]) result.addedMetadata.push_back(current.tempoAndSignature[index]);

    result.lengthChanged = !std::isfinite(original.lengthBeats) || !std::isfinite(current.lengthBeats)
        ? original.lengthBeats != current.lengthBeats
        : std::abs(original.lengthBeats - current.lengthBeats) > patternComparisonTolerance;
    return result;
}

bool setPatternTempo(MidiPattern& pattern, int beatsPerMinute, double positionBeats)
{
    if (beatsPerMinute < 20 || beatsPerMinute > 300 || !std::isfinite(positionBeats) || positionBeats < 0.0)
        return false;
    replaceMetaAtBeat(pattern, true,
        juce::MidiMessage::tempoMetaEvent(static_cast<int>(std::round(60000000.0 / beatsPerMinute))), positionBeats);
    return true;
}

bool setPatternTimeSignature(MidiPattern& pattern, int numerator, int denominator, double positionBeats)
{
    if (numerator < 1 || numerator > 32 || (denominator != 1 && denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16)
        || !std::isfinite(positionBeats) || positionBeats < 0.0)
        return false;
    replaceMetaAtBeat(pattern, false, juce::MidiMessage::timeSignatureMetaEvent(numerator, denominator), positionBeats);
    return true;
}

namespace
{
bool removeMetaAtBeat(MidiPattern& pattern, bool tempo, double positionBeats)
{
    if (!std::isfinite(positionBeats) || positionBeats < 0.0)
        return false;

    const auto previousSize = pattern.tempoAndSignature.size();
    pattern.tempoAndSignature.erase(std::remove_if(pattern.tempoAndSignature.begin(), pattern.tempoAndSignature.end(),
        [tempo, positionBeats](const MidiMetaEvent& item)
        {
            const bool sameType = tempo ? item.message.isTempoMetaEvent() : item.message.isTimeSignatureMetaEvent();
            return sameType && std::abs(item.positionBeats - positionBeats) < 1.0e-9;
        }), pattern.tempoAndSignature.end());
    return pattern.tempoAndSignature.size() != previousSize;
}
}

bool removePatternTempo(MidiPattern& pattern, double positionBeats)
{
    return removeMetaAtBeat(pattern, true, positionBeats);
}

bool removePatternTimeSignature(MidiPattern& pattern, double positionBeats)
{
    return removeMetaAtBeat(pattern, false, positionBeats);
}

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
    std::map<int,MidiAmbiguousNote> ambiguousNotes;
    const auto appendImportedEvent = [&](int note, std::uint8_t velocity, double position, double duration)
    {
        const auto noteNumber = static_cast<std::uint8_t>(note);
        const auto candidates = profile.mappingsForMidiNote(noteNumber);
        if (candidates.size() > 1)
        {
            auto& ambiguous = ambiguousNotes[note];
            ambiguous.note = note;
            ++ambiguous.eventCount;
            ambiguous.candidates = candidates;
        }
        const auto event = profile.fromMidi(noteNumber, velocity, position, duration);
        if (event) output.events.push_back(*event);
        else report.unmappedNotes.push_back(note);
    };
    for(int track=0;track<midi.getNumTracks();++track)
    {
        const auto* sequence=midi.getTrack(track);if(sequence==nullptr)continue;
        for(int i=0;i<sequence->getNumEvents();++i)
        {
            const auto* item=sequence->getEventPointer(i);if(item==nullptr)continue;
            const auto& message=item->message;const double beat=item->message.getTimeStamp()/ticksPerQuarter;
            if(std::isfinite(beat))output.lengthBeats=juce::jmax(output.lengthBeats,beat);
            if(message.isTempoMetaEvent()||message.isTimeSignatureMetaEvent())
                output.tempoAndSignature.push_back({beat,message});
            if(message.isNoteOn())
            {
                const Key key{message.getChannel(),message.getNoteNumber()};
                pending[key].push_back({beat,static_cast<std::uint8_t>(message.getVelocity())});
            }
            else if(message.isNoteOff())
            {
                const Key key{message.getChannel(),message.getNoteNumber()};auto found=pending.find(key);
                if(found==pending.end()||found->second.empty())continue;
                const auto start=found->second.front();found->second.erase(found->second.begin());
                appendImportedEvent(message.getNoteNumber(),start.second,start.first,juce::jmax(0.0,beat-start.first));
            }
        }
    }
    // Keep malformed files with missing note-offs usable; assign a short default gate.
    for(const auto& [key,starts]:pending)for(const auto& start:starts)appendImportedEvent(key.second,start.second,start.first,0.1);
    for (auto& [note, ambiguous] : ambiguousNotes)
    {
        juce::ignoreUnused(note);
        report.ambiguousNotes.push_back(std::move(ambiguous));
    }
    std::stable_sort(output.events.begin(),output.events.end(),[](const auto&a,const auto&b){return a.positionBeats<b.positionBeats;});
    std::stable_sort(output.tempoAndSignature.begin(),output.tempoAndSignature.end(),[](const auto&a,const auto&b){return a.positionBeats<b.positionBeats;});
    report.ok=true;return report;
}

bool MidiPatternIO::write(const juce::File& file,const MidiPattern& pattern,const MidiMappingProfile& profile,
                          bool allowFallback,std::vector<DrumEvent>* unsupported,
                          std::vector<MidiFallbackUse>* fallbacks) const
{
    constexpr int ppq=960;juce::MidiFile midi;midi.setTicksPerQuarterNote(ppq);juce::MidiMessageSequence sequence;
    auto metadata=pattern.tempoAndSignature;
    if(std::none_of(metadata.begin(),metadata.end(),[](const auto&m){return m.message.isTempoMetaEvent();}))metadata.push_back({0.0,juce::MidiMessage::tempoMetaEvent(500000)});
    if(std::none_of(metadata.begin(),metadata.end(),[](const auto&m){return m.message.isTimeSignatureMetaEvent();}))metadata.push_back({0.0,juce::MidiMessage::timeSignatureMetaEvent(4,4)});
    double lengthBeats=juce::jmax(0.0,pattern.lengthBeats);
    for(const auto& meta:metadata)if(meta.positionBeats>=0.0&&(meta.message.isTempoMetaEvent()||meta.message.isTimeSignatureMetaEvent())){sequence.addEvent(meta.message,meta.positionBeats*ppq);lengthBeats=juce::jmax(lengthBeats,meta.positionBeats);}
    for(const auto& event:pattern.events)
    {
        const auto mapped=profile.toMidi(event,allowFallback);
        if(!mapped.note){if(unsupported)unsupported->push_back(event);continue;}
        if(mapped.usedFallback&&fallbacks)fallbacks->push_back({event,*mapped.note,mapped.mappedArticulation});
        const auto note=*mapped.note;const int velocity=juce::jlimit(1,127,static_cast<int>(event.velocity));
        const double start=juce::jmax(0.0,event.positionBeats)*ppq;const double end=(juce::jmax(0.0,event.positionBeats)+juce::jmax(0.0,event.durationBeats))*ppq;
        lengthBeats=juce::jmax(lengthBeats,end/ppq);
        sequence.addEvent(juce::MidiMessage::noteOn(10,note,static_cast<juce::uint8>(velocity)),start);
        sequence.addEvent(juce::MidiMessage::noteOff(10,note),end);
    }
    sequence.addEvent(juce::MidiMessage::endOfTrack(),lengthBeats*ppq);sequence.updateMatchedPairs();sequence.sort();midi.addTrack(sequence);
    auto stream=file.createOutputStream();if(!stream)return false;const bool ok=midi.writeTo(*stream);stream->flush();return ok;
}
