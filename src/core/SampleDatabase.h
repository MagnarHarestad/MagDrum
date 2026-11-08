#pragma once
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include "MidiMapping.h"
#include <optional>

struct ScannedSample;

class SampleDatabase
{
public:
    SampleDatabase() = default;
    ~SampleDatabase();
    SampleDatabase(const SampleDatabase&) = delete;
    SampleDatabase& operator=(const SampleDatabase&) = delete;
    bool open(const juce::File& file);
    bool insertSample(const juce::File& file, double durationSeconds, int channels, double sampleRate);
    bool insertSample(const ScannedSample& sample);
    bool setUserCategory(const juce::File& file, const juce::String& category);
    juce::String getCategory(const juce::File& file) const;
    bool setUserTags(const juce::File& file, const juce::StringArray& tags);
    juce::StringArray getUserTags(const juce::File& file) const;
    juce::Array<juce::File> findSimilar(const juce::File& file, int limit = 20) const;
    bool saveMappingProfile(const juce::String& name, const MidiMappingProfile& profile);
    std::optional<MidiMappingProfile> loadMappingProfile(const juce::String& name) const;
    juce::Array<juce::File> getAllPaths() const;
    juce::Array<juce::File> searchPaths(const juce::String& query, int limit = 200) const;
private:
    sqlite3* connection = nullptr;
};
