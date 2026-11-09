#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

inline constexpr double sampleClassificationReviewThreshold = 0.75;

inline bool needsClassificationReview(bool manuallyClassified, const juce::String& category,
                                      double confidence)
{
    if (manuallyClassified)
        return false;
    if (category == "Unknown" || !std::isfinite(confidence))
        return true;
    return confidence < sampleClassificationReviewThreshold;
}
