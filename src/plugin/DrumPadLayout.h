#pragma once

#include "../core/MidiMapping.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace magdrum::plugin
{
struct DrumPadDefinition
{
    const char* name;
    int midiNote;
    DrumInstrument instrument;
    DrumArticulation articulation;
};

// Row-major Boss DR-660 inspired layout. Keep each pad's semantic event and GM
// note together so a visual position cannot silently drift from its MIDI map.
inline constexpr std::array<DrumPadDefinition, 16> bossDr660PadLayout{{
    {"Crash 1", 49, DrumInstrument::Crash, DrumArticulation::Default},
    {"Crash 2", 57, DrumInstrument::Crash, DrumArticulation::Edge},
    {"Bell", 53, DrumInstrument::Ride, DrumArticulation::Bell},
    {"Ride", 51, DrumInstrument::Ride, DrumArticulation::Bow},
    {"Tom 1", 50, DrumInstrument::Tom, DrumArticulation::High},
    {"Tom 2", 47, DrumInstrument::Tom, DrumArticulation::Mid},
    {"Tom 3", 45, DrumInstrument::Tom, DrumArticulation::Low},
    {"Tom 4", 43, DrumInstrument::Tom, DrumArticulation::Default},
    {"Side Stick", 37, DrumInstrument::Rimshot, DrumArticulation::SideStick},
    {"Clap", 39, DrumInstrument::Clap, DrumArticulation::Default},
    {"Closed HH", 42, DrumInstrument::HiHat, DrumArticulation::Closed},
    {"Open HH", 46, DrumInstrument::HiHat, DrumArticulation::Open},
    {"Kick", 36, DrumInstrument::Kick, DrumArticulation::Default},
    {"Snare", 38, DrumInstrument::Snare, DrumArticulation::Default},
    {"Pedal HH", 44, DrumInstrument::HiHat, DrumArticulation::Pedal},
    {"Cowbell", 56, DrumInstrument::Percussion, DrumArticulation::Bell}
}};

inline constexpr auto defaultPadNotes = []
{
    std::array<int, bossDr660PadLayout.size()> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = bossDr660PadLayout[i].midiNote;
    return result;
}();

inline constexpr auto padNames = []
{
    std::array<const char*, bossDr660PadLayout.size()> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = bossDr660PadLayout[i].name;
    return result;
}();

inline constexpr auto padInstruments = []
{
    std::array<DrumInstrument, bossDr660PadLayout.size()> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = bossDr660PadLayout[i].instrument;
    return result;
}();

inline constexpr auto padArticulations = []
{
    std::array<DrumArticulation, bossDr660PadLayout.size()> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = bossDr660PadLayout[i].articulation;
    return result;
}();

static_assert((defaultPadNotes == std::array<int, 16>{
    49, 57, 53, 51, 50, 47, 45, 43, 37, 39, 42, 46, 36, 38, 44, 56}));
static_assert(std::string_view(padNames[12]) == "Kick");
static_assert(std::string_view(padNames[13]) == "Snare");
static_assert(std::string_view(padNames[14]) == "Pedal HH");
static_assert(std::string_view(padNames[15]) == "Cowbell");
static_assert(padArticulations[1] == DrumArticulation::Edge);
static_assert(padArticulations[7] == DrumArticulation::Default);
static_assert(padArticulations[15] == DrumArticulation::Bell);
}
