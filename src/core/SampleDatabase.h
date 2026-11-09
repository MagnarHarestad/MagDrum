#pragma once
#include <juce_core/juce_core.h>
#include <sqlite3.h>
#include "MidiMapping.h"
#include <optional>
#include <cstdint>
#include <vector>

struct ScannedSample;

struct PluginInstrumentRecord
{
    juce::String identifier, format, name, manufacturer, category, file, version;
};

struct PluginSoundRecord
{
    int midiNote = 0, velocity = 100;
    juce::String articulation, previewPath, fingerprint, category = "Unknown";
    bool silent = false;
    double duration = 0.0, sampleRate = 0.0, peak = 0.0, rms = 0.0, spectralCentroid = 0.0, transientStrength = 0.0, fundamentalHz = 0.0, pitchConfidence = 0.0, confidence = 0.0;
    int channels = 0;
};

struct PluginSoundBrowseRecord
{
    juce::File previewFile;
    juce::String pluginIdentifier, pluginName, kitName, articulation;
    int midiNote = 0, velocity = 0;
};

struct SampleBrowseRecord
{
    juce::File file;
    juce::String category;
    double durationSeconds = 0.0, brightnessHz = 0.0;
};

struct SampleScanRecord
{
    std::int64_t id = 0;
    juce::String rootPath, startedAt, finishedAt, status;
    std::int64_t filesConsidered = 0, filesSkipped = 0, filesIndexed = 0, errors = 0;
};

struct PerceptualDuplicateMatch
{
    juce::File file;
    double similarity = 0.0;
};

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
    bool isSampleUnchanged(const juce::File& file, std::int64_t sizeBytes, std::int64_t modifiedTimeMillis) const;
    std::optional<std::int64_t> beginSampleScan(const juce::File& root);
    bool finishSampleScan(std::int64_t scanId, const juce::String& status,
                          std::int64_t filesConsidered, std::int64_t filesSkipped,
                          std::int64_t filesIndexed, std::int64_t errors);
    std::int64_t markRunningSampleScansInterrupted();
    juce::Array<SampleScanRecord> getRecentSampleScans(int limit = 20) const;
    bool upsertPluginInstrument(const PluginInstrumentRecord& plugin);
    juce::Array<PluginInstrumentRecord> getPluginInstruments() const;
    std::optional<std::int64_t> savePluginKit(const juce::String& pluginIdentifier, const juce::String& kitName, const juce::MemoryBlock& pluginState);
    std::optional<juce::MemoryBlock> loadPluginKitState(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    bool upsertPluginSound(std::int64_t kitId, const PluginSoundRecord& sound);
    juce::Array<PluginSoundBrowseRecord> getPluginSoundBrowseRecords() const;
    bool setUserCategory(const juce::File& file, const juce::String& category);
    juce::String getCategory(const juce::File& file) const;
    std::optional<juce::File> getFirstSampleForCategory(const juce::String& category) const;
    bool setUserTags(const juce::File& file, const juce::StringArray& tags);
    juce::StringArray getUserTags(const juce::File& file) const;
    juce::Array<juce::File> findSimilar(const juce::File& file, int limit = 20) const;
    juce::Array<juce::File> findExactDuplicates(const juce::File& file, int limit = 100) const;
    std::vector<PerceptualDuplicateMatch> findPerceptualDuplicates(const juce::File& file,
                                                                   double minimumSimilarity = 0.85,
                                                                   int limit = 100) const;
    bool saveMappingProfile(const juce::String& name, const MidiMappingProfile& profile);
    std::optional<MidiMappingProfile> loadMappingProfile(const juce::String& name) const;
    juce::Array<juce::File> getAllPaths() const;
    juce::Array<SampleBrowseRecord> getSampleBrowseRecords() const;
    juce::Array<juce::File> searchPaths(const juce::String& query, int limit = 200) const;
private:
    sqlite3* connection = nullptr;
};
