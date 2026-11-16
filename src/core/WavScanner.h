#pragma once
#include <functional>
#include <cstdint>
#include <memory>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>

class DrumClassificationModel;

inline constexpr int currentSampleAnalysisVersion = 8;

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
    double spectralFlatness = 0.0;
    double transientStrength = 0.0;
    int transientCount = 0;
    std::vector<double> transientAttackTimesSeconds;
    double fundamentalHz = 0.0;
    double pitchConfidence = 0.0;
    int analysisVersion = currentSampleAnalysisVersion;
    std::int64_t fileSizeBytes = 0;
    std::int64_t modifiedTimeMillis = 0;
    juce::String fingerprint;
    juce::String acousticFingerprint;
    int acousticFingerprintVersion = 0;
    juce::String category = "Unknown";
    juce::StringArray classificationTags;
    double classificationConfidence = 0.0;
};

class WavScanner
{
public:
    using ShouldCancel = std::function<bool()>;
    using ShouldSkipFile = std::function<bool(const juce::File&, std::int64_t, std::int64_t)>;
    // When provided, each analyzed file is delivered immediately and is not retained in the returned array.
    using SampleConsumer = std::function<bool(const ScannedSample&)>;
    using FileProgressConsumer = std::function<bool(const juce::File&)>;
    struct Stats
    {
        std::int64_t filesConsidered = 0;
        std::int64_t filesSkippedUnchanged = 0;
        std::int64_t filesAnalyzed = 0;
        std::int64_t errors = 0;
        std::int64_t steinbergVstSoundContainers = 0;
        std::int64_t kontaktNcwFiles = 0;
        bool cancelled = false;
    };
    juce::Array<ScannedSample> scan(const juce::File& root, ShouldCancel shouldCancel = {},
                                    ShouldSkipFile shouldSkipFile = {}, Stats* stats = nullptr,
                                    SampleConsumer sampleConsumer = {},
                                    const juce::String& resumeAfterFile = {},
                                    FileProgressConsumer fileProgressConsumer = {},
                                    std::shared_ptr<const DrumClassificationModel> classificationModel = {}) const;
};
