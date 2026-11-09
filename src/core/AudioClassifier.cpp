#include "AudioClassifier.h"
#include <cmath>
#include <utility>

namespace
{
class HeuristicDrumClassificationModel final : public DrumClassificationModel
{
public:
DrumClassification classify(const ScannedSample& sample) const override
{
    DrumClassification out;
    auto name = sample.file.getFileNameWithoutExtension().toLowerCase();
    constexpr char filenameSeparators[] = "-_.,;()[]{}+";
    for (const auto* separator = filenameSeparators; *separator != '\0'; ++separator)
        name = name.replaceCharacter(*separator, ' ');
    const auto has = [&name](std::initializer_list<const char*> words)
    {
        return std::any_of(words.begin(), words.end(), [&name](const char* word)
        {
            return name.containsWholeWordIgnoreCase(word);
        });
    };
    const auto addTag = [&out](const char* tag) { if (!out.tags.contains(tag)) out.tags.add(tag); };
    if (has({"808"})) { addTag("Electronic"); addTag("808"); }
    if (has({"909"})) { addTag("Electronic"); addTag("909"); }
    if (has({"electronic", "synth", "drum machine"})) addTag("Electronic");
    if (has({"acoustic", "brush", "live kit"})) addTag("Acoustic");
    if (has({"deep", "sub"})) addTag("Deep");
    if (has({"bright", "crisp"})) addTag("Bright");
    if (has({"dark", "muted"})) addTag("Dark");
    if (has({"distort", "saturat", "fuzz", "overdrive"})) addTag("Distorted");

    if (sample.durationSeconds <= 0.0 || sample.rms <= 0.00001 || sample.peak <= 0.0001)
        return out;

    const double crest = sample.peak / juce::jmax(sample.rms, 0.00001);
    const double transientRatio = sample.transientStrength / juce::jmax(sample.rms, 0.00001);
    if (sample.durationSeconds < 0.24 && sample.rms < 0.12 && sample.bitDepth > 0) addTag("Short");
    if (sample.durationSeconds > 0.8) addTag("Long");
    if (sample.peak > 0.85) addTag("Loud");
    if (transientRatio > 1.2) addTag("Punchy");
    if (sample.rms < 0.05) addTag("Quiet");

    juce::StringArray namedCategories;
    if (has({"kick", "bass drum", "bassdrum"})) namedCategories.add("Kick");
    if (has({"snare"})) namedCategories.add("Snare");
    if (has({"hi-hat", "hihat", "hi hat", "closed hat", "open hat", "pedal hat"}))
    {
        if (has({"pedal", "foot", "phh"})) namedCategories.add("Pedal hi-hat");
        else if (has({"open", "ohh"})) namedCategories.add("Open hi-hat");
        else namedCategories.add("Closed hi-hat");
    }
    if (has({"clap", "handclap"})) namedCategories.add("Clap");
    if (has({"tom", "floor tom", "rack tom"})) namedCategories.add("Tom");
    if (has({"rimshot", "rim shot", "side stick", "sidestick"})) namedCategories.add("Rimshot");
    if (has({"ride", "ride cymbal"})) namedCategories.add("Ride");
    if (has({"crash", "crash cymbal"})) namedCategories.add("Crash");
    if (has({"percussion", "conga", "bongo", "cowbell", "shaker", "tambourine"})) namedCategories.add("Percussion");

    if (namedCategories.contains("Rimshot") && namedCategories.contains("Snare"))
        namedCategories.removeString("Snare");

    if (namedCategories.size() == 1)
    {
        out.category = namedCategories[0];
        out.confidence = has({"808", "909", "kick", "snare", "hihat", "hi-hat", "hi hat", "tom", "clap", "ride", "crash", "rimshot", "percussion"}) ? 0.92 : 0.82;
        return out;
    }
    if (namedCategories.size() > 1)
        return out;

    // Classification intentionally stays Unknown when the available measurements
    // do not separate likely drum families with useful confidence.
    const bool hasLowKickPitch = sample.pitchConfidence < 0.65 || sample.fundamentalHz <= 220.0;
    if (sample.spectralCentroidHz > 0.0 && sample.spectralCentroidHz < 1300.0
        && sample.durationSeconds < 1.2 && transientRatio > 0.45 && hasLowKickPitch)
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
};
}

AudioClassifier::AudioClassifier() : classificationModel(std::make_shared<HeuristicDrumClassificationModel>())
{
}

AudioClassifier::AudioClassifier(std::shared_ptr<const DrumClassificationModel> model)
    : classificationModel(model != nullptr ? std::move(model) : std::make_shared<HeuristicDrumClassificationModel>())
{
}

DrumClassification AudioClassifier::classify(const ScannedSample& sample) const
{
    if (classificationModel == nullptr) return {};
    auto result = classificationModel->classify(sample);
    const juce::StringArray supported{"Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat",
                                      "Clap", "Tom", "Rimshot", "Ride", "Crash", "Percussion", "Unknown"};
    if (!supported.contains(result.category) || !std::isfinite(result.confidence)
        || result.confidence < 0.0 || result.confidence > 1.0)
        return {};
    return result;
}
