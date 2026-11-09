#pragma once

#include <cmath>

enum class SampleFeatureFilter
{
    all = 1,
    veryShort,
    shortSample,
    mediumSample,
    longSample,
    dark,
    midBrightness,
    bright
};

inline bool matchesSampleFeatureFilter(SampleFeatureFilter filter, double durationSeconds,
                                       double spectralCentroidHz)
{
    switch (filter)
    {
        case SampleFeatureFilter::all: return true;
        case SampleFeatureFilter::veryShort: return std::isfinite(durationSeconds) && durationSeconds >= 0.0 && durationSeconds < 0.1;
        case SampleFeatureFilter::shortSample: return std::isfinite(durationSeconds) && durationSeconds >= 0.1 && durationSeconds < 0.5;
        case SampleFeatureFilter::mediumSample: return std::isfinite(durationSeconds) && durationSeconds >= 0.5 && durationSeconds < 2.0;
        case SampleFeatureFilter::longSample: return std::isfinite(durationSeconds) && durationSeconds >= 2.0;
        case SampleFeatureFilter::dark: return std::isfinite(spectralCentroidHz) && spectralCentroidHz > 0.0 && spectralCentroidHz < 1000.0;
        case SampleFeatureFilter::midBrightness: return std::isfinite(spectralCentroidHz) && spectralCentroidHz >= 1000.0 && spectralCentroidHz < 3000.0;
        case SampleFeatureFilter::bright: return std::isfinite(spectralCentroidHz) && spectralCentroidHz >= 3000.0;
    }
    return false;
}
