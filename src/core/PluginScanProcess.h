#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace magdrum::plugins
{
struct CachedVst3ModuleInfo
{
    bool hasFileModTime = false;
    bool cachedModTimesAgree = true;
    juce::Time fileModTime;
    juce::String filePath;
    bool cachedFilePathsAgree = true;
    bool hasPluginDescriptions = false;
    bool hasInstrumentDescription = false;
};
using CachedVst3ModuleIndex = std::unordered_map<std::string, CachedVst3ModuleInfo>;

struct Vst3ModuleInstrumentCacheEntry
{
    juce::String metadataPath;
    juce::int64 metadataSize = 0;
    juce::int64 metadataModifiedMilliseconds = 0;
    juce::String binaryPath;
    juce::int64 binarySize = 0;
    juce::int64 binaryModifiedMilliseconds = 0;
    bool mayBeInstrument = true;
};
using Vst3ModuleInstrumentCache = std::unordered_map<std::string, Vst3ModuleInstrumentCacheEntry>;
enum class Vst3ModuleDiscoveryAction { Skip, ScanInstrument, ExcludeEffect };

juce::FileSearchPath createVst3DiscoverySearchPath(const juce::FileSearchPath& defaultLocations,
                                                    const juce::File& additionalFolder = {});
bool vst3ModuleMayBeInstrument(const juce::File& bundle);
Vst3ModuleInstrumentCache loadVst3ModuleInstrumentCache(const juce::File& cacheFile);
std::optional<bool> cachedVst3ModuleMayBeInstrument(const Vst3ModuleInstrumentCache& cache,
                                                    const juce::File& bundle);
void rememberVst3ModuleInstrumentStatus(Vst3ModuleInstrumentCache& cache, const juce::File& bundle,
                                        bool mayBeInstrument);
Vst3ModuleDiscoveryAction classifyVst3ModuleForDiscovery(Vst3ModuleInstrumentCache& cache,
                                                          const juce::File& bundle,
                                                          bool cachedBinaryIsUpToDate);
bool saveVst3ModuleInstrumentCache(const juce::File& cacheFile,
                                  const Vst3ModuleInstrumentCache& cache);
CachedVst3ModuleIndex indexCachedVst3Modules(
    const juce::Array<juce::PluginDescription>& cachedDescriptions);
bool isCachedVst3ModuleUpToDate(const CachedVst3ModuleInfo& cached, const juce::File& module);
}

struct PluginScanProcessCapture
{
    int midiNote = 0;
    int velocity = 0;
    int takeIndex = 1;
    int channels = 0;
    int sampleCount = 0;
    double sampleRate = 0.0;
    double peak = 0.0;
    double rms = 0.0;
    bool silent = true;
    juce::String fileName;
};

struct PluginScanProcessManifest
{
    std::vector<PluginScanProcessCapture> captures;
    juce::String error;
    bool succeeded = false;
    bool cancelled = false;
    bool partial = false;
};

struct PluginScanProcessExecution
{
    PluginScanProcessManifest manifest;
    bool started = false;
    bool manifestRead = false;
    bool terminatedForCancellation = false;
    int exitCode = -1;
};

class PluginScanProcess
{
public:
    using ShouldCancel = std::function<bool()>;
    using Progress = std::function<void(int completed, int total, int midiNote, int velocity)>;

    static bool readManifest(const juce::File& file, PluginScanProcessManifest& result);
    static bool readCaptureLog(const juce::File& file, PluginScanProcessManifest& result);
    static PluginScanProcessExecution monitorWorker(const juce::StringArray& command,
        const juce::File& progressFile, const juce::File& resultFile, const juce::File& cancellationFile,
        ShouldCancel shouldCancel = {}, Progress progress = {}, int cancellationGraceMilliseconds = 10000,
        const juce::File& captureLogFile = {});
    static int runWorker(const juce::File& requestFile);
};
