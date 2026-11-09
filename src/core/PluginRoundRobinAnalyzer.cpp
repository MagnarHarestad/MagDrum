#include "PluginRoundRobinAnalyzer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace
{
int hexDigit(juce::juce_wchar character)
{
    if (character >= '0' && character <= '9') return static_cast<int>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<int>(character - 'a' + 10);
    if (character >= 'A' && character <= 'F') return static_cast<int>(character - 'A' + 10);
    return -1;
}

bool decode(const juce::String& text, std::array<std::uint8_t, 64>& values)
{
    if (text.length() != static_cast<int>(values.size() * 2)) return false;
    for (size_t index = 0; index < values.size(); ++index)
    {
        const int high = hexDigit(text[static_cast<int>(index * 2)]);
        const int low = hexDigit(text[static_cast<int>(index * 2 + 1)]);
        if (high < 0 || low < 0) return false;
        values[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

double distance(const std::array<std::uint8_t, 64>& left,
                const std::array<std::uint8_t, 64>& right)
{
    double total = 0.0;
    for (size_t segment = 0; segment < 4; ++segment)
    {
        double segmentDistance = 0.0;
        for (size_t band = 0; band < 16; ++band)
        {
            const size_t index = segment * 16 + band;
            segmentDistance += std::abs(static_cast<int>(left[index]) - static_cast<int>(right[index]));
        }
        total += segmentDistance / (2.0 * 255.0);
    }
    return total / 4.0;
}
}

std::vector<PluginRoundRobinCandidate> PluginRoundRobinAnalyzer::analyze(
    const std::vector<PluginRoundRobinTake>& takes, double distanceThreshold) const
{
    std::vector<PluginRoundRobinCandidate> candidates;
    if (!std::isfinite(distanceThreshold) || distanceThreshold < 0.0 || distanceThreshold > 1.0)
        return candidates;

    std::map<std::pair<int, int>, std::vector<const PluginRoundRobinTake*>> groups;
    for (const auto& take : takes)
        if (!take.silent && take.midiNote >= 0 && take.midiNote <= 127
            && take.velocity >= 1 && take.velocity <= 127 && take.takeIndex >= 1)
            groups[{take.midiNote, take.velocity}].push_back(&take);

    for (auto& [key, group] : groups)
    {
        std::stable_sort(group.begin(), group.end(), [](const auto* left, const auto* right)
        {
            return left->takeIndex < right->takeIndex;
        });
        std::vector<std::array<std::uint8_t, 64>> representatives;
        double maximumDistance = 0.0;
        int validTakes = 0;
        for (const auto* take : group)
        {
            std::array<std::uint8_t, 64> current{};
            if (!decode(take->acousticFingerprint, current)) continue;
            ++validTakes;
            bool matchesExisting = false;
            for (const auto& representative : representatives)
            {
                const double currentDistance = distance(representative, current);
                maximumDistance = std::max(maximumDistance, currentDistance);
                if (currentDistance < distanceThreshold) matchesExisting = true;
            }
            if (!matchesExisting) representatives.push_back(current);
        }
        if (validTakes >= 2 && representatives.size() >= 2)
            candidates.push_back({key.first, key.second, validTakes, static_cast<int>(representatives.size()),
                                  maximumDistance, algorithmVersion});
    }
    return candidates;
}
