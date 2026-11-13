#pragma once

namespace magdrum::sequencer
{
inline constexpr int maximumBars = 16;
inline constexpr int maximumStepsPerBar = 32;
inline constexpr int maximumSteps = maximumBars * maximumStepsPerBar;

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
}