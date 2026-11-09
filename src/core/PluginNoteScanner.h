#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

struct PluginNoteScanSettings
{
    double sampleRate = 44100.0;
    int blockSize = 512;
    std::vector<int> velocityLevels { 100 };
    int repeatCount = 1;
    double noteDurationSeconds = 0.25;
    double releaseSeconds = 0.5;
    double captureLengthSeconds = 0.75;
    double silencePeakThreshold = 1.0e-5;
};

struct PluginNoteCapture
{
    int midiNote = 0;
    int velocity = 100;
    int takeIndex = 1;
    juce::AudioBuffer<float> audio;
    double peak = 0.0;
    double rms = 0.0;
    bool silent = true;
};

struct PluginNoteScanResult
{
    std::vector<PluginNoteCapture> notes;
    juce::String error;
    bool cancelled = false;
};

class PluginNoteScanner
{
public:
    using ShouldCancel = std::function<bool()>;
    using Progress = std::function<void(int completed, int total, int midiNote, int velocity)>;
    // When provided, captures are delivered immediately and are not retained in the result vector.
    using CaptureConsumer = std::function<bool(const PluginNoteCapture&)>;

    PluginNoteScanResult scan(juce::AudioProcessor& plugin,
                              const PluginNoteScanSettings& settings = {},
                              ShouldCancel shouldCancel = {},
                              Progress progress = {},
                              CaptureConsumer captureConsumer = {}) const;
};
