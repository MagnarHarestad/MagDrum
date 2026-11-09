#pragma once

#include "SampleDatabase.h"
#include <vector>

class PluginVelocityLayerAnalyzer
{
public:
    static constexpr int algorithmVersion = 1;
    static constexpr double defaultDistanceThreshold = 0.28;

    std::vector<PluginVelocityLayerBoundary> analyze(const std::vector<PluginSoundRecord>& sounds,
                                                       double distanceThreshold = defaultDistanceThreshold) const;
};
