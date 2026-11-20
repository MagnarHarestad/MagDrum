#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

inline constexpr double sampleClassificationReviewThreshold = 0.75;

enum class SampleClassificationEvidence
{
    Unclassified,
    Manual,
    NameOrFolder,
    AudioSuggestion
};

inline bool hasSampleClassificationEvidence(const juce::String& category, double confidence)
{
    return category.isNotEmpty() && category != "Unknown" && std::isfinite(confidence)
        && confidence > 0.0 && confidence <= 1.0;
}

inline SampleClassificationEvidence classificationEvidenceFor(bool manuallyClassified,
                                                               const juce::String& category,
                                                               double confidence)
{
    if (manuallyClassified)
        return SampleClassificationEvidence::Manual;
    if (!hasSampleClassificationEvidence(category, confidence))
        return SampleClassificationEvidence::Unclassified;
    // Audio-only suggestions are deliberately capped below the review threshold;
    // scores at or above it come from the filename/folder cue rules.
    return confidence >= sampleClassificationReviewThreshold
        ? SampleClassificationEvidence::NameOrFolder
        : SampleClassificationEvidence::AudioSuggestion;
}

inline bool needsClassificationReview(bool manuallyClassified, const juce::String& category,
                                      double confidence)
{
    if (manuallyClassified)
        return false;
    if (!hasSampleClassificationEvidence(category, confidence))
        return true;
    return confidence < sampleClassificationReviewThreshold;
}
