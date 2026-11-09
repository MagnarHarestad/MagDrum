#pragma once

#include "SampleDatabase.h"
#include <vector>

class PluginRoundRobinAnalyzer
{
public:
    static constexpr int algorithmVersion = 1;
    static constexpr double defaultDistanceThreshold = 0.18;

    std::vector<PluginRoundRobinCandidate> analyze(const std::vector<PluginRoundRobinTake>& takes,
                                                   double distanceThreshold = defaultDistanceThreshold) const;
};
