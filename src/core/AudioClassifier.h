#pragma once
#include "WavScanner.h"
#include <memory>
#include <vector>

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

// A small local example model trained from samples whose drum categories were
// explicitly corrected by the user. It never modifies those corrections.
class ManualExampleDrumClassificationModel final : public DrumClassificationModel
{
public:
    explicit ManualExampleDrumClassificationModel(std::vector<ScannedSample> examples);
    DrumClassification classify(const ScannedSample& sample) const override;
    std::size_t trainingExampleCount() const { return examples.size(); }
private:
    std::vector<ScannedSample> examples;
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
