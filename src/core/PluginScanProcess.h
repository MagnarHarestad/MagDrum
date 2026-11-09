#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

struct PluginScanProcessCapture
{
    int midiNote = 0;
    int velocity = 0;
    int takeIndex = 1;
    int channels = 0;
    int sampleCount = 0;
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
};

class PluginScanProcess
{
public:
    static bool readManifest(const juce::File& file, PluginScanProcessManifest& result);
    static int runWorker(const juce::File& requestFile);
};
