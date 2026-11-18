#pragma once
#include <cstdint>

enum class DrumInstrument : std::uint8_t { Kick, Snare, HiHat, Tom, Ride, Crash, Clap, Rimshot, Percussion };
enum class DrumArticulation : std::uint8_t { Default, Center, Rimshot, SideStick, Closed, Open, Pedal, Low, Mid, High, Bell, Bow, Edge };

struct DrumEvent
{
    DrumInstrument instrument = DrumInstrument::Percussion;
    DrumArticulation articulation = DrumArticulation::Default;
    std::uint8_t velocity = 100;
    double positionBeats = 0.0;
    double durationBeats = 0.1;
    // Preserves distinct drum pads that share the same semantic instrument/articulation.
    int midiNote = -1;
};
