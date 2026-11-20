#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include <optional>
#include <vector>

namespace magdrum::battery
{
struct SampleZone
{
    int zoneIndex = 0;
    int sampleIndex = 0;
    int minimumNote = 0;
    int maximumNote = 127;
    int minimumVelocity = 1;
    int maximumVelocity = 127;
    juce::String path;
    bool embedded = false;
};

struct Cell
{
    int cellIndex = 0;
    juce::String name;
    std::vector<SampleZone> zones;
};

struct Kit
{
    juce::String name;
    int rows = 0;
    int columns = 0;
    int zoneCount = 0;
    std::vector<Cell> cells;
    std::vector<juce::String> samplePaths;
    bool hasEmbeddedSamples = false;
};

struct ReadResult
{
    std::optional<Kit> kit;
    juce::String error;
};

// Reads Battery 4's length-bounded Hsin/DSIN container and references only.
// It never extracts or modifies sample files.
ReadResult readKit(const juce::File& file);
ReadResult readKit(const void* data, size_t size);
}
