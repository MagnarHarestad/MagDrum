#pragma once
#include <juce_core/juce_core.h>
#include <sqlite3.h>

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
    bool setUserTags(const juce::File& file, const juce::StringArray& tags);
    juce::StringArray getUserTags(const juce::File& file) const;
    juce::Array<juce::File> findSimilar(const juce::File& file, int limit = 20) const;
    juce::Array<juce::File> getAllPaths() const;
private:
    sqlite3* connection = nullptr;
};
