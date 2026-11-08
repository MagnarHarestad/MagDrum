#pragma once
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>

struct ScannedSample
{
    juce::File file;
    double durationSeconds = 0.0;
    int channels = 0;
    double sampleRate = 0.0;
    juce::String format;
    int bitDepth = 0;
    double peak = 0.0;
    double rms = 0.0;
    double spectralCentroidHz = 0.0;
    double transientStrength = 0.0;
    juce::String fingerprint;
    juce::String category = "Unknown";
    double classificationConfidence = 0.0;
};

class WavScanner
{
public:
    using ShouldCancel = std::function<bool()>;
    juce::Array<ScannedSample> scan(const juce::File& root, ShouldCancel shouldCancel = {}) const;
};
