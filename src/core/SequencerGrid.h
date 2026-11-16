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

inline bool isPositionOnGridStep(double positionBeats, int step, int subdivisionsPerBeat = 4) noexcept
{
    if (!std::isfinite(positionBeats) || positionBeats < 0.0 || step < 0 || subdivisionsPerBeat < 1)
        return false;

    const double expectedBeat = static_cast<double>(step) / static_cast<double>(subdivisionsPerBeat);
    return std::abs(positionBeats - expectedBeat) <= gridBeatTolerance;
}

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
    int subdivisionsPerBeat = 4;
    double lengthBeats = 0.0;
    bool valid = false;
};

constexpr bool isSupportedSubdivision(int subdivisionsPerBeat) noexcept
{
    return subdivisionsPerBeat == 2 || subdivisionsPerBeat == 4 || subdivisionsPerBeat == 8;
}

constexpr int stepsPerBar(int numerator, int denominator, int subdivisionsPerBeat = 4) noexcept
{
    if (numerator < 1 || !isSupportedSubdivision(subdivisionsPerBeat)
        || (denominator != 2 && denominator != 4 && denominator != 8 && denominator != 16))
        return 0;
    const int numeratorSteps = numerator * 4 * subdivisionsPerBeat;
    const int steps = numeratorSteps / denominator;
    return numeratorSteps % denominator == 0 && steps <= maximumStepsPerBar ? steps : 0;
}

constexpr int stepsForBars(int numerator, int denominator, int bars, int subdivisionsPerBeat = 4) noexcept
{
    const int perBar = stepsPerBar(numerator, denominator, subdivisionsPerBeat);
    return perBar > 0 && bars >= 1 && bars <= maximumBars ? perBar * bars : 0;
}

inline BarMap buildBarMap(int initialNumerator, int initialDenominator, int bars,
                          const std::vector<MeterChange>& changes, int subdivisionsPerBeat = 4)
{
    BarMap result;
    if (stepsPerBar(initialNumerator, initialDenominator, subdivisionsPerBeat) == 0
        || bars < 1 || bars > maximumBars)
        return result;
    result.subdivisionsPerBeat = subdivisionsPerBeat;

    auto ordered = changes;
    for (const auto& change : ordered)
        if (!std::isfinite(change.positionBeats) || change.positionBeats < 0.0)
            return result;

    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    {
        return left.positionBeats < right.positionBeats;
    });
    std::vector<MeterChange> effectiveChanges;
    effectiveChanges.reserve(ordered.size());
    for (const auto& change : ordered)
    {
        if (!effectiveChanges.empty()
            && std::abs(effectiveChanges.back().positionBeats - change.positionBeats) <= gridBeatTolerance)
            effectiveChanges.back() = change;
        else
            effectiveChanges.push_back(change);
    }
    const auto isGridCompatible = [subdivisionsPerBeat](const MeterChange& change)
    {
        return stepsPerBar(change.numerator, change.denominator, subdivisionsPerBeat) > 0
            && std::abs(change.positionBeats * subdivisionsPerBeat
                        - std::round(change.positionBeats * subdivisionsPerBeat)) <= gridBeatTolerance;
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

        const auto stepsAtMeter = stepsPerBar(numerator, denominator, subdivisionsPerBeat);
        if (stepsAtMeter == 0)
            return {};

        double endBeat = beat + static_cast<double>(numerator) * 4.0 / static_cast<double>(denominator);
        if (nextChange < effectiveChanges.size() && effectiveChanges[nextChange].positionBeats < endBeat - gridBeatTolerance)
        {
            if (!isGridCompatible(effectiveChanges[nextChange]))
                return {};
            endBeat = effectiveChanges[nextChange].positionBeats;
        }

        const auto startStep = static_cast<int>(std::llround(beat * subdivisionsPerBeat));
        const auto endStep = static_cast<int>(std::llround(endBeat * subdivisionsPerBeat));
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
