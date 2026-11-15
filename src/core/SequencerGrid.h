#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace magdrum::sequencer
{
inline constexpr int maximumBars = 16;
inline constexpr int maximumStepsPerBar = 32;
inline constexpr int maximumSteps = maximumBars * maximumStepsPerBar;
inline constexpr double gridBeatTolerance = 1.0e-6;

struct MeterChange
{
    double positionBeats = 0.0;
    int numerator = 4;
    int denominator = 4;
};

struct BarSpan
{
    int startStep = 0;
    int stepCount = 0;
    double startBeat = 0.0;
    int numerator = 4;
    int denominator = 4;
};

struct BarMap
{
    std::array<BarSpan, maximumBars> bars{};
    int barCount = 0;
    int totalSteps = 0;
    double lengthBeats = 0.0;
    bool valid = false;
};

constexpr int stepsPerBar(int numerator, int denominator) noexcept
{
    if (numerator < 1 || (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16))
        return 0;
    const int steps = numerator * 16 / denominator;
    return numerator * 16 % denominator == 0 && steps <= maximumStepsPerBar ? steps : 0;
}

constexpr int stepsForBars(int numerator, int denominator, int bars) noexcept
{
    const int perBar = stepsPerBar(numerator, denominator);
    return perBar > 0 && bars >= 1 && bars <= maximumBars ? perBar * bars : 0;
}

inline BarMap buildBarMap(int initialNumerator, int initialDenominator, int bars,
                          const std::vector<MeterChange>& changes)
{
    BarMap result;
    if (stepsPerBar(initialNumerator, initialDenominator) == 0 || bars < 1 || bars > maximumBars)
        return result;

    auto ordered = changes;
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    {
        return left.positionBeats < right.positionBeats;
    });
    std::vector<MeterChange> effectiveChanges;
    effectiveChanges.reserve(ordered.size());
    for (const auto& change : ordered)
    {
        if (!std::isfinite(change.positionBeats) || change.positionBeats < 0.0)
            return result;
        if (!effectiveChanges.empty()
            && std::abs(effectiveChanges.back().positionBeats - change.positionBeats) <= gridBeatTolerance)
            effectiveChanges.back() = change;
        else
            effectiveChanges.push_back(change);
    }
    const auto isGridCompatible = [](const MeterChange& change)
    {
        return stepsPerBar(change.numerator, change.denominator) > 0
            && std::abs(change.positionBeats * 4.0 - std::round(change.positionBeats * 4.0)) <= gridBeatTolerance;
    };

    int numerator = initialNumerator;
    int denominator = initialDenominator;
    size_t nextChange = 0;
    double beat = 0.0;
    for (int bar = 0; bar < bars; ++bar)
    {
    while (nextChange < effectiveChanges.size()
               && effectiveChanges[nextChange].positionBeats <= beat + gridBeatTolerance)
        {
            if (!isGridCompatible(effectiveChanges[nextChange]))
                return {};
            numerator = effectiveChanges[nextChange].numerator;
            denominator = effectiveChanges[nextChange].denominator;
            ++nextChange;
        }

        const auto stepsAtMeter = stepsPerBar(numerator, denominator);
        if (stepsAtMeter == 0)
            return {};

        double endBeat = beat + static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
        if (nextChange < effectiveChanges.size() && effectiveChanges[nextChange].positionBeats < endBeat - gridBeatTolerance)
        {
            if (!isGridCompatible(effectiveChanges[nextChange]))
                return {};
            endBeat = effectiveChanges[nextChange].positionBeats;
        }

        const auto startStep = static_cast<int>(std::llround(beat * 4.0));
        const auto endStep = static_cast<int>(std::llround(endBeat * 4.0));
        const auto stepCount = endStep - startStep;
        if (stepCount < 1 || stepCount > maximumStepsPerBar || endStep > maximumSteps)
            return {};

        result.bars[static_cast<size_t>(bar)] = {startStep, stepCount, beat, numerator, denominator};
        result.totalSteps = endStep;
        beat = endBeat;
    }
    result.barCount = bars;
    result.lengthBeats = beat;
    result.valid = true;
    return result;
}
}
