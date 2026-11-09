#include "PluginVelocityLayerAnalyzer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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

double fingerprintDistance(const std::array<std::uint8_t, 64>& left,
                           const std::array<std::uint8_t, 64>& right)
{
    double distance = 0.0;
    for (size_t segment = 0; segment < 4; ++segment)
    {
        double segmentDifference = 0.0;
        for (size_t band = 0; band < 16; ++band)
        {
            const size_t index = segment * 16 + band;
            segmentDifference += std::abs(static_cast<int>(left[index]) - static_cast<int>(right[index]));
        }
        distance += segmentDifference / (2.0 * 255.0);
    }
    return distance / 4.0;
}
}

std::vector<PluginVelocityLayerBoundary> PluginVelocityLayerAnalyzer::analyze(
    const std::vector<PluginSoundRecord>& sounds, double distanceThreshold) const
{
    std::vector<PluginVelocityLayerBoundary> candidates;
    if (!std::isfinite(distanceThreshold) || distanceThreshold < 0.0 || distanceThreshold > 1.0)
        return candidates;

    std::vector<const PluginSoundRecord*> ordered;
    ordered.reserve(sounds.size());
    for (const auto& sound : sounds)
        if (!sound.silent && sound.midiNote >= 0 && sound.midiNote <= 127
            && sound.velocity >= 1 && sound.velocity <= 127)
            ordered.push_back(&sound);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right)
    {
        if (left->midiNote != right->midiNote) return left->midiNote < right->midiNote;
        return left->velocity < right->velocity;
    });

    for (size_t index = 1; index < ordered.size(); ++index)
    {
        const auto& lower = *ordered[index - 1];
        const auto& upper = *ordered[index];
        if (lower.midiNote != upper.midiNote || lower.velocity >= upper.velocity)
            continue;
        std::array<std::uint8_t, 64> lowerFingerprint{}, upperFingerprint{};
        if (!decode(lower.acousticFingerprint, lowerFingerprint)
            || !decode(upper.acousticFingerprint, upperFingerprint))
            continue;
        const double distance = fingerprintDistance(lowerFingerprint, upperFingerprint);
        if (distance >= distanceThreshold)
            candidates.push_back({lower.midiNote, lower.velocity, upper.velocity, distance});
    }
    return candidates;
}
