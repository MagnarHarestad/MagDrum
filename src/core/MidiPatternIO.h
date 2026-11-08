#pragma once
#include "DrumEvent.h"
#include "MidiMapping.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

struct MidiMetaEvent
{
    double positionBeats = 0.0;
    juce::MidiMessage message;
};

struct MidiPattern
{
    std::vector<DrumEvent> events;
    std::vector<MidiMetaEvent> tempoAndSignature;
};

bool setPatternTempo(MidiPattern&, int beatsPerMinute, double positionBeats = 0.0);
bool setPatternTimeSignature(MidiPattern&, int numerator, int denominator, double positionBeats = 0.0);

struct MidiImportReport
{
    std::vector<int> unmappedNotes;
    juce::String error;
    bool ok = false;
};

class MidiPatternIO
{
public:
    MidiImportReport read(const juce::File&, const MidiMappingProfile&, MidiPattern&) const;
    bool write(const juce::File&, const MidiPattern&, const MidiMappingProfile&,
               bool allowFallback, std::vector<DrumEvent>* unsupported = nullptr) const;
};
