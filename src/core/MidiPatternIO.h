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
    double lengthBeats = 0.0;
};

struct MidiPatternDiff
{
    std::vector<DrumEvent> removedEvents;
    std::vector<DrumEvent> addedEvents;
    std::vector<MidiMetaEvent> removedMetadata;
    std::vector<MidiMetaEvent> addedMetadata;
    bool lengthChanged = false;
};

MidiPatternDiff compareMidiPatterns(const MidiPattern& original, const MidiPattern& current);

struct MidiAmbiguousNote
{
    int note = 0;
    int eventCount = 0;
    std::vector<MidiNoteMapping> candidates;
};

struct MidiFallbackUse
{
    DrumEvent requestedEvent;
    std::uint8_t midiNote = 0;
    DrumArticulation mappedArticulation = DrumArticulation::Default;
};

bool setPatternTempo(MidiPattern&, int beatsPerMinute, double positionBeats = 0.0);
bool setPatternTimeSignature(MidiPattern&, int numerator, int denominator, double positionBeats = 0.0);
bool removePatternTempo(MidiPattern&, double positionBeats);
bool removePatternTimeSignature(MidiPattern&, double positionBeats);

struct MidiImportReport
{
    std::vector<int> unmappedNotes;
    std::vector<MidiAmbiguousNote> ambiguousNotes;
    juce::String error;
    bool ok = false;
};

class MidiPatternIO
{
public:
    MidiImportReport read(const juce::File&, const MidiMappingProfile&, MidiPattern&) const;
    bool write(const juce::File&, const MidiPattern&, const MidiMappingProfile&,
               bool allowFallback, std::vector<DrumEvent>* unsupported = nullptr,
               std::vector<MidiFallbackUse>* fallbacks = nullptr) const;
    bool write(const juce::File&, const MidiPattern&, const MidiMappingProfile&,
               MidiFallbackPolicy fallbackPolicy, std::vector<DrumEvent>* unsupported = nullptr,
               std::vector<MidiFallbackUse>* fallbacks = nullptr) const;
};
