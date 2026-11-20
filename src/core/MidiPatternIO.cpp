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
        && (left.midiNote == -1 || right.midiNote == -1 || left.midiNote == right.midiNote)
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

bool hasImportAmbiguity(const std::vector<MidiNoteMapping>& candidates)
{
    for (std::size_t left = 0; left < candidates.size(); ++left)
    {
        for (std::size_t right = left + 1; right < candidates.size(); ++right)
        {
            const auto& a = candidates[left];
            const auto& b = candidates[right];
            if (a.instrument != b.instrument)
                return true;

            const bool bothSpecific = a.articulation != DrumArticulation::Default
                && b.articulation != DrumArticulation::Default;
            if (bothSpecific && a.articulation != b.articulation)
                return true;
        }
    }
    return false;
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
    const short timeFormat = midi.getTimeFormat();
    const bool usesPpqTiming = timeFormat > 0;
    double secondsPerTick = 0.0;
    if (timeFormat == 0)
    {
        report.error = "MIDI time division is invalid.";
        return report;
    }
    if (! usesPpqTiming)
    {
        const auto rawTimeFormat = static_cast<std::uint16_t>(timeFormat);
        const int encodedFrameCode = static_cast<int>((rawTimeFormat >> 8) & 0xff);
        const int signedFrameCode = encodedFrameCode >= 128 ? encodedFrameCode - 256 : encodedFrameCode;
        const int framesPerSecondCode = -signedFrameCode;
        const int ticksPerFrame = static_cast<int>(rawTimeFormat & 0xff);
        double framesPerSecond = 0.0;
        switch (framesPerSecondCode)
        {
            case 24: framesPerSecond = 24.0; break;
            case 25: framesPerSecond = 25.0; break;
            case 29: framesPerSecond = 30000.0 / 1001.0; break;
            case 30: framesPerSecond = 30.0; break;
            default: break;
        }
        if (framesPerSecond <= 0.0 || ticksPerFrame <= 0)
        {
            report.error = "SMPTE MIDI time division is invalid or unsupported.";
            return report;
        }
        secondsPerTick = 1.0 / (framesPerSecond * ticksPerFrame);
    }
    using Key=std::pair<int,int>;
    std::map<Key,std::vector<std::pair<double,std::uint8_t>>> pending;
    std::map<int,MidiAmbiguousNote> ambiguousNotes;
    const auto appendImportedEvent = [&](int note, std::uint8_t velocity, double position, double duration)
    {
        const auto noteNumber = static_cast<std::uint8_t>(note);
        const auto candidates = profile.mappingsForMidiNote(noteNumber);
        if (hasImportAmbiguity(candidates))
        {
            auto& ambiguous = ambiguousNotes[note];
            ambiguous.note = note;
            ++ambiguous.eventCount;
            ambiguous.candidates = candidates;
        }
        const auto event = profile.fromMidi(noteNumber, velocity, position, duration);
        if (event) output.events.push_back(*event);
        else
        {
            report.unmappedNotes.push_back(note);
            output.events.push_back({DrumInstrument::Percussion, DrumArticulation::Default,
                                     velocity, position, duration, note});
        }
    };
    struct TimedMessage
    {
        double absoluteTime = 0.0;
        double beat = 0.0;
        int track = 0;
        int order = 0;
        juce::MidiMessage message;
    };
    std::vector<TimedMessage> timedMessages;
    for (int track = 0; track < midi.getNumTracks(); ++track)
    {
        const auto* sequence = midi.getTrack(track);
        if (sequence == nullptr)
            continue;
        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto* item = sequence->getEventPointer(i);
            if (item == nullptr)
                continue;
            const double timestamp = item->message.getTimeStamp();
            const double absoluteTime = usesPpqTiming ? timestamp / timeFormat : timestamp * secondsPerTick;
            if (! std::isfinite(absoluteTime) || absoluteTime < 0.0)
                continue;
            timedMessages.push_back({absoluteTime, 0.0, track, i, item->message});
        }
    }

    // Pair note-ons and note-offs in musical time, not track-by-track order.
    // A type-1 MIDI file may split overlapping notes of the same pitch across tracks.
    std::stable_sort(timedMessages.begin(), timedMessages.end(), [](const TimedMessage& left, const TimedMessage& right)
    {
        if (left.absoluteTime != right.absoluteTime)
            return left.absoluteTime < right.absoluteTime;
        if (left.track != right.track)
            return left.track < right.track;
        return left.order < right.order;
    });

    if (usesPpqTiming)
    {
        for (auto& timed : timedMessages)
            timed.beat = timed.absoluteTime;
    }
    else
    {
        double previousSeconds = 0.0;
        double currentBeat = 0.0;
        double secondsPerQuarterNote = 0.5;
        for (auto& timed : timedMessages)
        {
            currentBeat += (timed.absoluteTime - previousSeconds) / secondsPerQuarterNote;
            previousSeconds = timed.absoluteTime;
            timed.beat = currentBeat;
            if (timed.message.isTempoMetaEvent())
            {
                const double newTempo = timed.message.getTempoSecondsPerQuarterNote();
                if (std::isfinite(newTempo) && newTempo > 0.0)
                    secondsPerQuarterNote = newTempo;
            }
        }
    }

    for (const auto& timed : timedMessages)
        output.lengthBeats = juce::jmax(output.lengthBeats, timed.beat);

    for (const auto& timed : timedMessages)
    {
        const auto& message = timed.message;
        const double beat = timed.beat;
        if (message.isTempoMetaEvent() || message.isTimeSignatureMetaEvent())
            output.tempoAndSignature.push_back({beat, message});
        if (message.isNoteOn())
        {
            const Key key{message.getChannel(), message.getNoteNumber()};
            pending[key].push_back({beat, static_cast<std::uint8_t>(message.getVelocity())});
        }
        else if (message.isNoteOff())
        {
            const Key key{message.getChannel(), message.getNoteNumber()};
            auto found = pending.find(key);
            if (found == pending.end() || found->second.empty())
                continue;
            const auto start = found->second.front();
            found->second.erase(found->second.begin());
            appendImportedEvent(message.getNoteNumber(), start.second, start.first,
                                juce::jmax(0.0, beat - start.first));
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
    return write(file, pattern, profile,
        allowFallback ? MidiFallbackPolicy::anyArticulation : MidiFallbackPolicy::disabled,
        unsupported, fallbacks);
}

bool MidiPatternIO::write(const juce::File& file, const MidiPattern& pattern, const MidiMappingProfile& profile,
                          MidiFallbackPolicy fallbackPolicy, std::vector<DrumEvent>* unsupported,
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
        const auto mapped=profile.toMidi(event,fallbackPolicy);
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
