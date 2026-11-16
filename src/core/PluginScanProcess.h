#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

namespace magdrum::plugins
{
juce::FileSearchPath createVst3DiscoverySearchPath(const juce::FileSearchPath& defaultLocations,
                                                    const juce::File& additionalFolder = {});
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
