#pragma once
#include "WavScanner.h"
#include <memory>

struct DrumClassification
{
    juce::String category = "Unknown";
    double confidence = 0.0;
    juce::StringArray tags;
};

// Classification strategies can be replaced by a trained model without changing
// the scanner, database or browser call sites.
class DrumClassificationModel
{
public:
    virtual ~DrumClassificationModel() = default;
    virtual DrumClassification classify(const ScannedSample& sample) const = 0;
};

// Conservative deterministic baseline. Scores are heuristic, never guarantees.
class AudioClassifier
{
public:
    AudioClassifier();
    explicit AudioClassifier(std::shared_ptr<const DrumClassificationModel> model);
    DrumClassification classify(const ScannedSample& sample) const;
private:
    std::shared_ptr<const DrumClassificationModel> classificationModel;
};
