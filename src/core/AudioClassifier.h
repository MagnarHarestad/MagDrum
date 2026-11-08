#pragma once
#include "WavScanner.h"

struct DrumClassification
{
    juce::String category = "Unknown";
    double confidence = 0.0;
    juce::StringArray tags;
};

// Conservative deterministic baseline. Scores are heuristic, never guarantees.
class AudioClassifier
{
public:
    DrumClassification classify(const ScannedSample& sample) const;
};
