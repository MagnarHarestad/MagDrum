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
    juce::String articulation, previewPath, fingerprint, acousticFingerprint, category = "Unknown";
    bool silent = false;
    double duration = 0.0, sampleRate = 0.0, peak = 0.0, rms = 0.0, spectralCentroid = 0.0, transientStrength = 0.0, fundamentalHz = 0.0, pitchConfidence = 0.0, confidence = 0.0;
    int channels = 0;
};

struct PluginVelocityLayerBoundary
{
    int midiNote = 0, lowerVelocity = 0, upperVelocity = 0;
    double spectralDistance = 0.0;
    int algorithmVersion = 1;
};

struct PluginConfirmedVelocityLayerRange
{
    int midiNote = 0, minimumVelocity = 1, maximumVelocity = 127;
};

struct PluginRoundRobinTake
{
    int midiNote = 0, velocity = 100, takeIndex = 1;
    juce::String previewPath, acousticFingerprint;
    bool silent = false;
};

struct PluginRoundRobinCandidate
{
    int midiNote = 0, velocity = 100, takeCount = 0, distinctVariants = 0;
    double maximumSpectralDistance = 0.0;
    int algorithmVersion = 1;
};

struct PluginConfirmedRoundRobinGroup
{
    int midiNote = 0, velocity = 100;
    std::vector<int> takeIndices;
};

struct PluginSoundBrowseRecord
{
    juce::File previewFile;
    juce::String pluginIdentifier, pluginName, kitName, articulation;
    int midiNote = 0, velocity = 0, takeIndex = 1;
};

struct SampleSourceRecord
{
    std::int64_t id = 0;
    juce::String type, identifier, displayName, rootPath;
};

struct SampleBrowseRecord
{
    juce::File file;
    juce::String category, sourceType, sourceName, sourceIdentifier;
    juce::String pluginIdentifier, pluginName, kitName, articulation;
    juce::StringArray classificationTags;
    double durationSeconds = 0.0, brightnessHz = 0.0;
    double classificationConfidence = 0.0;
    int midiNote = -1, velocity = 0, takeIndex = 1;
    bool manuallyClassified = false;
};

struct SampleBrowseFilter
{
    juce::String query, category, sourceType, sourceIdentifier, pluginName, kitName;
    int feature = 1; // 1=all, 2-5 duration bands, 6-8 spectral brightness bands
    int sort = 1; // 1=name, 2=shortest, 3=longest, 4=brightest, 5=darkest
    bool needsReview = false;
};

struct SampleBrowsePage
{
    juce::Array<SampleBrowseRecord> records;
    bool hasMore = false;
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
    std::optional<std::int64_t> ensureSampleSource(const juce::String& type, const juce::String& identifier,
                                                   const juce::String& displayName, const juce::String& rootPath = {});
    bool assignSamplesToSource(const juce::File& root, std::int64_t sourceId);
    juce::Array<SampleSourceRecord> getSampleSources(const juce::String& type = {}) const;
    bool insertSample(const juce::File& file, double durationSeconds, int channels, double sampleRate);
    bool insertSample(const ScannedSample& sample, std::int64_t sourceId = 0);
    bool isSampleUnchanged(const juce::File& file, std::int64_t sizeBytes, std::int64_t modifiedTimeMillis) const;
    std::optional<std::int64_t> beginSampleScan(const juce::File& root);
    bool finishSampleScan(std::int64_t scanId, const juce::String& status,
                          std::int64_t filesConsidered, std::int64_t filesSkipped,
                          std::int64_t filesIndexed, std::int64_t errors);
    std::int64_t markRunningSampleScansInterrupted();
    juce::Array<SampleScanRecord> getRecentSampleScans(int limit = 20) const;
    bool upsertPluginInstrument(const PluginInstrumentRecord& plugin);
    juce::Array<PluginInstrumentRecord> getPluginInstruments() const;
    juce::StringArray getPluginKitNames() const;
    std::optional<std::int64_t> savePluginKit(const juce::String& pluginIdentifier, const juce::String& kitName, const juce::MemoryBlock& pluginState);
    std::optional<std::int64_t> getPluginKitId(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    std::optional<juce::MemoryBlock> loadPluginKitState(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    bool upsertPluginSound(std::int64_t kitId, const PluginSoundRecord& sound);
    bool replacePluginVelocityLayerCandidates(std::int64_t kitId, const std::vector<PluginVelocityLayerBoundary>& candidates);
    std::vector<PluginVelocityLayerBoundary> getPluginVelocityLayerCandidates(std::int64_t kitId) const;
    std::vector<PluginVelocityLayerBoundary> getPluginVelocityLayerCandidates(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    bool replaceConfirmedPluginVelocityLayers(std::int64_t kitId, const std::vector<PluginConfirmedVelocityLayerRange>& ranges);
    std::vector<PluginConfirmedVelocityLayerRange> getConfirmedPluginVelocityLayers(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    bool replacePluginRoundRobinData(std::int64_t kitId, const std::vector<PluginRoundRobinTake>& takes,
                                     const std::vector<PluginRoundRobinCandidate>& candidates);
    std::vector<PluginRoundRobinCandidate> getPluginRoundRobinCandidates(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    bool confirmPluginRoundRobinGroup(std::int64_t kitId, int midiNote, int velocity);
    bool removeConfirmedPluginRoundRobinGroup(std::int64_t kitId, int midiNote, int velocity);
    std::vector<PluginConfirmedRoundRobinGroup> getConfirmedPluginRoundRobinGroups(const juce::String& pluginIdentifier, const juce::String& kitName) const;
    juce::Array<PluginSoundBrowseRecord> getPluginSoundBrowseRecords() const;
    bool setUserCategory(const juce::File& file, const juce::String& category);
    juce::String getCategory(const juce::File& file) const;
    juce::Array<SampleBrowseRecord> searchSampleBrowseRecords(const juce::String& query,
                                                              const juce::String& category,
                                                              int limit, int offset = 0) const;
    SampleBrowsePage searchSampleBrowseRecords(const SampleBrowseFilter& filter,
                                               int limit, int offset = 0) const;
    std::optional<juce::File> getFirstSampleForCategory(const juce::String& category) const;
    bool setUserTags(const juce::File& file, const juce::StringArray& tags);
    juce::StringArray getUserTags(const juce::File& file) const;
    juce::StringArray getClassificationTags(const juce::File& file) const;
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
