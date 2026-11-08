#pragma once
#include "DrumEvent.h"
#include <optional>
#include <vector>

struct MidiNoteMapping
{
    DrumInstrument instrument;
    DrumArticulation articulation;
    std::uint8_t note;
};

struct MidiMappingResult
{
    std::optional<std::uint8_t> note;
    bool usedFallback = false;
    DrumArticulation mappedArticulation = DrumArticulation::Default;
};

class MidiMappingProfile
{
public:
    explicit MidiMappingProfile(std::vector<MidiNoteMapping> mappings = {});
    static MidiMappingProfile generalMidi();
    MidiMappingResult toMidi(const DrumEvent&, bool allowFallback) const;
    std::optional<DrumEvent> fromMidi(std::uint8_t note, std::uint8_t velocity,
                                      double positionBeats = 0.0, double durationBeats = 0.1) const;
    const std::vector<MidiNoteMapping>& entries() const { return mappings; }
private:
    std::vector<MidiNoteMapping> mappings;
};
