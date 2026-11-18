#include "MidiMapping.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <utility>

std::string formatMidiNoteName(int midiNote)
{
    static constexpr std::array noteNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (midiNote < 0 || midiNote > 127)
        return {};

    const auto noteIndex = static_cast<std::size_t>(midiNote % 12);
    const auto octave = midiNote / 12 - 2;
    return std::string(noteNames[noteIndex]) + std::to_string(octave);
}

namespace
{
constexpr std::array instrumentNames{"Kick", "Snare", "HiHat", "Tom", "Ride", "Crash", "Clap", "Rimshot", "Percussion"};
constexpr std::array articulationNames{"Default", "Center", "Rimshot", "SideStick", "Closed", "Open", "Pedal", "Low", "Mid", "High", "Bell", "Bow", "Edge"};

template <typename Names>
std::optional<std::size_t> findName(const Names& names, const std::string& value)
{
    const auto found = std::find(names.begin(), names.end(), value);
    if (found == names.end()) return std::nullopt;
    return static_cast<std::size_t>(std::distance(names.begin(), found));
}
}

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
    return toMidi(event, allowFallback ? MidiFallbackPolicy::anyArticulation : MidiFallbackPolicy::disabled);
}

MidiMappingResult MidiMappingProfile::toMidi(const DrumEvent& event, MidiFallbackPolicy fallbackPolicy) const
{
    if (event.midiNote >= 0 && event.midiNote <= 127)
        return {static_cast<std::uint8_t>(event.midiNote), false, event.articulation};
    const auto exact=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==event.instrument&&m.articulation==event.articulation;});
    if(exact!=mappings.end())return{exact->note,false,exact->articulation};
    if(fallbackPolicy == MidiFallbackPolicy::disabled)return{};
    if (fallbackPolicy != MidiFallbackPolicy::defaultArticulation
        && fallbackPolicy != MidiFallbackPolicy::anyArticulation)
        return {};
    const auto fallback=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.instrument==event.instrument&&m.articulation==DrumArticulation::Default;});
    if(fallback!=mappings.end())return{fallback->note,true,fallback->articulation};
    if(fallbackPolicy == MidiFallbackPolicy::defaultArticulation)return{};
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

std::vector<MidiNoteMapping> MidiMappingProfile::mappingsForMidiNote(std::uint8_t note) const
{
    std::vector<MidiNoteMapping> matches;
    for (const auto& mapping : mappings)
    {
        if (mapping.note != note) continue;
        const auto duplicate = std::find_if(matches.begin(), matches.end(), [&](const auto& existing)
        {
            return existing.instrument == mapping.instrument && existing.articulation == mapping.articulation;
        });
        if (duplicate == matches.end()) matches.push_back(mapping);
    }
    return matches;
}

std::string MidiMappingProfile::toCsv() const
{
    std::ostringstream output;
    output << "instrument,articulation,note\n";
    for (const auto& mapping : mappings)
    {
        const auto instrument = static_cast<std::size_t>(mapping.instrument);
        const auto articulation = static_cast<std::size_t>(mapping.articulation);
        if (instrument >= instrumentNames.size() || articulation >= articulationNames.size()) continue;
        output << instrumentNames[instrument] << ',' << articulationNames[articulation] << ','
               << static_cast<unsigned int>(mapping.note) << '\n';
    }
    return output.str();
}

std::optional<MidiMappingProfile> MidiMappingProfile::fromCsv(const std::string& csv)
{
    std::istringstream input(csv);
    std::string line;
    if (!std::getline(input, line) || line != "instrument,articulation,note") return std::nullopt;
    std::vector<MidiNoteMapping> parsed;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto first = line.find(',');
        const auto second = first == std::string::npos ? first : line.find(',', first + 1);
        if (first == std::string::npos || second == std::string::npos || line.find(',', second + 1) != std::string::npos)
            return std::nullopt;
        const auto instrument = findName(instrumentNames, line.substr(0, first));
        const auto articulation = findName(articulationNames, line.substr(first + 1, second - first - 1));
        unsigned int note = 0;
        const auto noteText = line.substr(second + 1);
        const auto conversion = std::from_chars(noteText.data(), noteText.data() + noteText.size(), note);
        if (!instrument || !articulation || conversion.ec != std::errc{} || conversion.ptr != noteText.data() + noteText.size() || note > 127)
            return std::nullopt;
        const MidiNoteMapping mapping{static_cast<DrumInstrument>(*instrument),
                                      static_cast<DrumArticulation>(*articulation),
                                      static_cast<std::uint8_t>(note)};
        if (std::any_of(parsed.begin(), parsed.end(), [&](const auto& existing)
                        { return existing.instrument == mapping.instrument && existing.articulation == mapping.articulation; }))
            return std::nullopt;
        parsed.push_back(mapping);
    }
    if (parsed.empty()) return std::nullopt;
    return MidiMappingProfile(std::move(parsed));
}

std::optional<DrumEvent> MidiMappingProfile::fromMidi(std::uint8_t note,std::uint8_t velocity,double position,double duration) const
{
    if(note>127||velocity>127||position<0.0||duration<0.0)return std::nullopt;
    const auto found=std::find_if(mappings.begin(),mappings.end(),[&](const auto& m){return m.note==note;});
    if(found==mappings.end())return std::nullopt;
    return DrumEvent{found->instrument,found->articulation,velocity,position,duration};
}
