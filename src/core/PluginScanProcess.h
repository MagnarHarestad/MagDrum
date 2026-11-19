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

struct Vst3DiscoveryProgress
{
    int completed = 0;
    int total = 0;
    juce::String current;
    juce::String currentPath;
    juce::StringArray failedModules;
};

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
bool readVst3DiscoveryProgress(const juce::File& progressFile, Vst3DiscoveryProgress& progress);
bool writeVst3DiscoveryProgress(const juce::File& progressFile, const Vst3DiscoveryProgress& progress);
bool recordVst3DiscoveryModuleFailure(const juce::File& progressFile, const juce::String& modulePath);
bool vst3ModuleHasInstrumentDescription(
    const juce::KnownPluginList& knownPlugins, const juce::File& module);
bool vst3ModuleHasInstrumentDescription(
    const juce::Array<juce::PluginDescription>& descriptions, const juce::File& module);
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

struct PluginHelperExecution
{
    bool started = false;
    bool cancelled = false;
    bool timedOut = false;
    int exitCode = -1;
};

class PluginScanProcess
{
public:
    using ShouldCancel = std::function<bool()>;
    using Progress = std::function<void(const juce::String& phase, int completed, int total, int midiNote, int velocity)>;
    using TextProgress = std::function<void(const juce::String& phase)>;

    static bool readManifest(const juce::File& file, PluginScanProcessManifest& result);
    static bool readCaptureLog(const juce::File& file, PluginScanProcessManifest& result);
    static PluginScanProcessExecution monitorWorker(const juce::StringArray& command,
        const juce::File& progressFile, const juce::File& resultFile, const juce::File& cancellationFile,
        ShouldCancel shouldCancel = {}, Progress progress = {}, int cancellationGraceMilliseconds = 10000,
        const juce::File& captureLogFile = {});
    static PluginHelperExecution monitorHelper(const juce::StringArray& command,
        const juce::File& progressFile, int timeoutMilliseconds, ShouldCancel shouldCancel = {},
        TextProgress progress = {}, int cancellationGraceMilliseconds = 3000);
    static bool shouldPublishNoteScanProgress(int completed, int total);
    static int runWorker(const juce::File& requestFile);
};
