#pragma once
#include "DrumEvent.h"
#include <optional>
#include <string>
#include <vector>

// Display pitch names using the Cubase octave convention (MIDI note 36 is C1).
std::string formatMidiNoteName(int midiNote);

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
    bool setMapping(MidiNoteMapping);
    bool removeMapping(DrumInstrument, DrumArticulation);
    std::vector<MidiNoteMapping> mappingsForMidiNote(std::uint8_t note) const;
    std::string toCsv() const;
    static std::optional<MidiMappingProfile> fromCsv(const std::string&);
    std::optional<DrumEvent> fromMidi(std::uint8_t note, std::uint8_t velocity,
                                      double positionBeats = 0.0, double durationBeats = 0.1) const;
    const std::vector<MidiNoteMapping>& entries() const { return mappings; }
private:
    std::vector<MidiNoteMapping> mappings;
};
