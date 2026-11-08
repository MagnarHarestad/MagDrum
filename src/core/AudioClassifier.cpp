#include "AudioClassifier.h"

DrumClassification AudioClassifier::classify(const ScannedSample& sample) const
{
    DrumClassification out;
    if (sample.durationSeconds <= 0.0 || sample.rms <= 0.00001 || sample.peak <= 0.0001)
        return out;

    const double crest = sample.peak / juce::jmax(sample.rms, 0.00001);
    const double transientRatio = sample.transientStrength / juce::jmax(sample.rms, 0.00001);
    if (sample.durationSeconds < 0.24 && sample.rms < 0.12 && sample.bitDepth > 0)
        out.tags.add("Short");
    if (sample.durationSeconds > 0.8) out.tags.add("Long");
    if (sample.peak > 0.85) out.tags.add("Loud");
    if (transientRatio > 1.2) out.tags.add("Punchy");
    if (sample.rms < 0.05) out.tags.add("Quiet");

    // Classification intentionally stays Unknown when the available measurements
    // do not separate likely drum families with useful confidence.
    if (sample.spectralCentroidHz > 0.0 && sample.spectralCentroidHz < 1800.0 && sample.durationSeconds < 1.2)
    {
        out.category = "Kick";
        out.confidence = juce::jlimit(0.35, 0.72, 0.62 - sample.spectralCentroidHz / 12000.0 + (transientRatio > 1.0 ? 0.04 : 0.0));
    }
    else if (sample.spectralCentroidHz >= 1800.0 && transientRatio > 0.45)
    {
        out.category = sample.durationSeconds < 0.32 ? "Closed hi-hat" : "Snare";
        out.confidence = juce::jlimit(0.30, 0.68, 0.42 + juce::jmin(0.2, transientRatio * 0.08) + (crest > 4.0 ? 0.04 : 0.0));
    }
    return out;
}
