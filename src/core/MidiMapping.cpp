#include "MidiMapping.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

MidiMappingProfile::MidiMappingProfile(std::vector<MidiNoteMapping> value) : mappings(std::move(value))
{
    for (const auto& entry : mappings)
        if (entry.note > 127) throw std::invalid_argument("MIDI note must be in range 0..127");
}

MidiMappingProfile MidiMappingProfile::generalMidi()
{
    using I=DrumInstrument; using A=DrumArticulation;
    return MidiMappingProfile({
        {I::Kick,A::Default,36},{I::Kick,A::Low,35},
        {I::Snare,A::Center,38},{I::Snare,A::Default,38},{I::Snare,A::SideStick,37},{I::Snare,A::Rimshot,40},{I::Rimshot,A::Default,40},
        {I::HiHat,A::Closed,42},{I::HiHat,A::Open,46},{I::HiHat,A::Pedal,44},{I::Tom,A::Low,43},{I::Tom,A::Mid,47},{I::Tom,A::High,50},
        {I::Ride,A::Bow,51},{I::Ride,A::Bell,53},{I::Crash,A::Default,49},{I::Clap,A::Default,39},{I::Percussion,A::Default,54}
    });
}

MidiMappingResult MidiMappingProfile::toMidi(const DrumEvent& event,bool allowFallback) const
{
    const auto exact=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==event.instrument&&m.articulation==event.articulation;});
    if(exact!=mappings.end())return{exact->note,false,exact->articulation};
    if(!allowFallback)return{};
    const auto fallback=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==event.instrument&&m.articulation==DrumArticulation::Default;});
    if(fallback!=mappings.end())return{fallback->note,true,fallback->articulation};
    const auto first=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==event.instrument;});
    if(first!=mappings.end())return{first->note,true,first->articulation};
    return{};
}

bool MidiMappingProfile::setMapping(MidiNoteMapping entry)
{
    if(entry.note>127)return false;
    const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==entry.instrument&&m.articulation==entry.articulation;});
    if(found!=mappings.end())*found=entry;else mappings.push_back(entry);
    return true;
}

bool MidiMappingProfile::removeMapping(DrumInstrument instrument,DrumArticulation articulation)
{
    const auto oldSize=mappings.size();mappings.erase(std::remove_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==instrument&&m.articulation==articulation;}),mappings.end());
    return mappings.size()!=oldSize;
}

std::optional<DrumEvent> MidiMappingProfile::fromMidi(std::uint8_t note,std::uint8_t velocity,double position,double duration) const
{
    if(note>127||velocity>127||position<0.0||duration<0.0)return std::nullopt;
    const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.note==note;});
    if(found==mappings.end())return std::nullopt;
    return DrumEvent{found->instrument,found->articulation,velocity,position,duration};
}
