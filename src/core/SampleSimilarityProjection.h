#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include <juce_core/juce_core.h>

namespace magdrum
{
struct SampleSimilarityPoint
{
    float x = 0.5f;
    float y = 0.5f;
    int sampleIndex = -1;
    bool hasAcousticFingerprint = false;
};

inline bool isSampleSimilarityPointVisible(float x, float y, float zoom, float panX, float panY)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(zoom)
        || !std::isfinite(panX) || !std::isfinite(panY) || zoom < 1.0f)
        return false;

    const float visibleWidth = 1.0f / zoom;
    return x >= panX && x <= panX + visibleWidth
        && y >= 1.0f - panY - visibleWidth && y <= 1.0f - panY;
}

inline std::vector<int> representativeSampleIndices(size_t sampleCount)
{
    constexpr size_t maximumPoints = 3000;
    const size_t count = std::min(sampleCount, maximumPoints);
    std::vector<int> indices;
    indices.reserve(count);
    if (count == 0) return indices;
    if (count == sampleCount)
    {
        for (size_t i = 0; i < count; ++i) indices.push_back(static_cast<int>(i));
        return indices;
    }
    for (size_t i = 0; i < count; ++i)
        indices.push_back(static_cast<int>((sampleCount - 1) * i / (count - 1)));
    return indices;
}

// Project the indexed 4 x 16 log-band fingerprints into two principal
// components. A deterministic uniform subset bounds work and drawing cost for
// very large libraries. Invalid fingerprints remain neutral for metadata fallback.
inline std::vector<SampleSimilarityPoint> projectAcousticFingerprints(
    const std::vector<juce::String>& fingerprintTexts)
{
    constexpr size_t dimensions = 64;
    const auto decode = [](const juce::String& text, std::array<double, dimensions>& values)
    {
        if (text.length() != static_cast<int>(dimensions * 2)) return false;
        const auto nibble = [](juce::juce_wchar c) -> int
        {
            if (c >= '0' && c <= '9') return static_cast<int>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<int>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<int>(c - 'A' + 10);
            return -1;
        };
        for (size_t i = 0; i < dimensions; ++i)
        {
            const int high = nibble(text[static_cast<int>(i * 2)]);
            const int low = nibble(text[static_cast<int>(i * 2 + 1)]);
            if (high < 0 || low < 0) return false;
            values[i] = static_cast<double>((high << 4) | low) / 255.0;
        }
        return true;
    };

    std::vector<SampleSimilarityPoint> points;
    std::vector<std::array<double, dimensions>> vectors;
    std::vector<size_t> projectedPointIndices;
    const auto selectedIndices = representativeSampleIndices(fingerprintTexts.size());
    points.reserve(selectedIndices.size());
    for (const int sampleIndex : selectedIndices)
    {
        const size_t i = static_cast<size_t>(sampleIndex);
        std::array<double, dimensions> values{};
        SampleSimilarityPoint point;
        point.sampleIndex = sampleIndex;
        if (!decode(fingerprintTexts[i], values))
        {
            points.push_back(point);
            continue;
        }
        // Each of the four time windows stores a spectrum. Normalize per
        // window to compare timbre and its evolution rather than playback level.
        for (size_t segment = 0; segment < 4; ++segment)
        {
            double total = 0.0;
            for (size_t band = 0; band < 16; ++band) total += values[segment * 16 + band];
            if (total > 1.0e-8)
                for (size_t band = 0; band < 16; ++band) values[segment * 16 + band] /= total;
        }
        point.hasAcousticFingerprint = true;
        points.push_back(point);
        vectors.push_back(values);
        projectedPointIndices.push_back(points.size() - 1);
    }

    if (vectors.size() < 2) return points;
    std::array<double, dimensions> mean{};
    for (const auto& row : vectors)
        for (size_t d = 0; d < dimensions; ++d) mean[d] += row[d] / static_cast<double>(vectors.size());
    std::array<double, dimensions * dimensions> covariance{};
    for (const auto& row : vectors)
        for (size_t a = 0; a < dimensions; ++a)
        {
            const double da = row[a] - mean[a];
            for (size_t b = 0; b < dimensions; ++b)
                covariance[a * dimensions + b] += da * (row[b] - mean[b]);
        }

    const auto principalComponent = [&covariance](std::array<double, dimensions> seed,
                                                  const std::array<double, dimensions>* previous)
    {
        for (int iteration = 0; iteration < 48; ++iteration)
        {
            std::array<double, dimensions> next{};
            for (size_t a = 0; a < dimensions; ++a)
                for (size_t b = 0; b < dimensions; ++b)
                    next[a] += covariance[a * dimensions + b] * seed[b];
            if (previous != nullptr)
            {
                double dot = 0.0;
                for (size_t d = 0; d < dimensions; ++d) dot += next[d] * (*previous)[d];
                for (size_t d = 0; d < dimensions; ++d) next[d] -= dot * (*previous)[d];
            }
            double norm = 0.0;
            for (const auto component : next) norm += component * component;
            norm = std::sqrt(norm);
            if (norm < 1.0e-12) return std::array<double, dimensions>{};
            for (size_t d = 0; d < dimensions; ++d) seed[d] = next[d] / norm;
        }
        size_t dominant = 0;
        for (size_t d = 1; d < dimensions; ++d)
            if (std::abs(seed[d]) > std::abs(seed[dominant])) dominant = d;
        if (seed[dominant] < 0.0)
            for (auto& component : seed) component = -component;
        return seed;
    };

    std::array<double, dimensions> seedX{}, seedY{};
    for (size_t d = 0; d < dimensions; ++d)
    {
        seedX[d] = 1.0 + static_cast<double>((d * 37) % 17) / 17.0;
        seedY[d] = 1.0 + static_cast<double>((d * 13 + 7) % 19) / 19.0;
    }
    const auto axisX = principalComponent(seedX, nullptr);
    const auto axisY = principalComponent(seedY, &axisX);
    std::vector<std::array<double, 2>> projected;
    double minX = std::numeric_limits<double>::max(), maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max(), maxY = std::numeric_limits<double>::lowest();
    for (const auto& row : vectors)
    {
        double x = 0.0, y = 0.0;
        for (size_t d = 0; d < dimensions; ++d)
        {
            x += (row[d] - mean[d]) * axisX[d];
            y += (row[d] - mean[d]) * axisY[d];
        }
        projected.push_back({x, y});
        minX = std::min(minX, x); maxX = std::max(maxX, x);
        minY = std::min(minY, y); maxY = std::max(maxY, y);
    }
    const double span = std::max(maxX - minX, maxY - minY);
    const double centerX = (minX + maxX) * 0.5;
    const double centerY = (minY + maxY) * 0.5;
    const double scale = span > 1.0e-9 ? 1.0 / span : 0.0;
    for (size_t i = 0; i < projected.size(); ++i)
    {
        auto& point = points[projectedPointIndices[i]];
        point.x = static_cast<float>(0.5 + (projected[i][0] - centerX) * scale);
        point.y = static_cast<float>(0.5 + (projected[i][1] - centerY) * scale);
    }
    return points;
}
}
