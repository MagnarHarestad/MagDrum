#include "AudioClassifier.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
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
    if (has({"rimshot", "rim shot"})) addTag("Articulation: Rimshot");
    if (has({"side stick", "sidestick"})) addTag("Articulation: Side stick");
    if (has({"cross stick", "cross-stick", "x-stick"})) addTag("Articulation: Cross stick");
    if (has({"ghost", "ghost note", "ghosted"})) addTag("Articulation: Ghost note");
    if (has({"flam", "flammed"})) addTag("Articulation: Flam");
    if (has({"drag", "ruff"})) addTag("Articulation: Drag");
    if (has({"roll", "rolls"})) addTag("Articulation: Roll");
    if (has({"brush", "brushes"})) addTag("Articulation: Brushes");
    if (has({"bow", "bowed"})) addTag("Articulation: Bow");
    if (has({"bell", "ridebell"})) addTag("Articulation: Bell");
    if (has({"edge", "rim"})) addTag("Articulation: Edge");
    if (has({"half open", "semi open"})) addTag("Articulation: Half-open");
    if (has({"808"})) { addTag("Electronic"); addTag("808"); }
    if (has({"909"})) { addTag("Electronic"); addTag("909"); }
    if (has({"electronic", "synth", "drum machine"})) addTag("Electronic");
    if (has({"acoustic", "brush", "live kit"})) addTag("Acoustic");
    if (has({"deep", "sub"})) addTag("Deep");
    if (has({"bright", "crisp"})) addTag("Bright");
    if (has({"dark", "muted"})) addTag("Dark");
    if (has({"distort", "distorted", "distortion", "distortions", "saturate", "saturated",
             "saturation", "fuzz", "fuzzy", "overdrive", "overdriven"})) addTag("Distorted");

    if (sample.durationSeconds <= 0.0 || sample.rms <= 0.00001 || sample.peak <= 0.0001)
        return out;

    const double crest = sample.peak / juce::jmax(sample.rms, 0.00001);
    const double transientRatio = sample.transientStrength / juce::jmax(sample.rms, 0.00001);
    if (sample.durationSeconds < 0.24) addTag("Short");
    if (sample.durationSeconds > 0.8) addTag("Long");
    if (sample.peak > 0.85) addTag("Loud");
    if (transientRatio > 1.2) addTag("Punchy");
    if (sample.rms < 0.05) addTag("Quiet");
    if (sample.spectralCentroidHz >= 3000.0) addTag("Bright");
    else if (sample.spectralCentroidHz > 0.0 && sample.spectralCentroidHz < 1000.0) addTag("Dark");

    const auto& attacks = sample.transientAttackTimesSeconds;
    if (attacks.size() == 2 && attacks.front() <= 0.04)
    {
        const auto separation = attacks[1] - attacks[0];
        if (separation >= 0.015 && separation <= 0.095)
            addTag("Articulation candidate: Flam");
    }
    if (attacks.size() >= 5 && attacks.front() <= 0.04)
    {
        double intervalTotal = 0.0;
        double intervalSquares = 0.0;
        bool intervalsValid = true;
        for (size_t index = 1; index < attacks.size(); ++index)
        {
            const auto interval = attacks[index] - attacks[index - 1];
            if (interval < 0.03 || interval > 0.15)
                intervalsValid = false;
            intervalTotal += interval;
            intervalSquares += interval * interval;
        }

        const auto intervalCount = static_cast<double>(attacks.size() - 1);
        const auto meanInterval = intervalTotal / intervalCount;
        const auto variance = juce::jmax(0.0, intervalSquares / intervalCount - meanInterval * meanInterval);
        const auto coefficientOfVariation = meanInterval > 0.0 ? std::sqrt(variance) / meanInterval : 1.0;
        if (intervalsValid && coefficientOfVariation <= 0.45)
            addTag("Articulation candidate: Roll");
    }

    juce::StringArray namedCategories;
    if (has({"kick", "bass drum", "bassdrum", "bd"})) namedCategories.add("Kick");
    if (has({"snare", "snr", "sd"})) namedCategories.add("Snare");
    if (has({"hi-hat", "hihat", "hi hat", "closed hat", "open hat", "pedal hat", "hh", "chh", "ohh", "phh", "hhc", "hho", "hhp"}))
    {
        if (has({"pedal", "foot", "phh", "hhp"})) namedCategories.add("Pedal hi-hat");
        else if (has({"open", "ohh", "hho"})) namedCategories.add("Open hi-hat");
        else namedCategories.add("Closed hi-hat");
    }
    if (has({"clap", "handclap"})) namedCategories.add("Clap");
    if (has({"tom", "floor tom", "rack tom", "tomhi", "tomlo"})) namedCategories.add("Tom");
    if (has({"rimshot", "rim shot", "side stick", "sidestick"})) namedCategories.add("Rimshot");
    if (has({"ride", "ride cymbal", "ridebell", "ride bell"})) namedCategories.add("Ride");
    if (has({"crash", "crash cymbal"})) namedCategories.add("Crash");
    if (has({"percussion", "conga", "bongo", "cowbell", "shaker", "tambourine"})) namedCategories.add("Percussion");

    if (namedCategories.contains("Rimshot") && namedCategories.contains("Snare"))
        namedCategories.removeString("Snare");

    if (namedCategories.size() == 1)
    {
        out.category = namedCategories[0];
        out.confidence = has({"808", "909", "kick", "bd", "snare", "snr", "sd", "hihat", "hi-hat", "hi hat", "hh", "chh", "ohh", "phh", "hhc", "hho", "hhp", "tom", "tomhi", "tomlo", "clap", "ride", "ridebell", "crash", "rimshot", "side stick", "sidestick", "cross stick", "cross-stick", "x-stick", "percussion"}) ? 0.92 : 0.82;
        return out;
    }
    if (namedCategories.size() > 1)
        return out;

    // Classification intentionally stays Unknown when the available measurements
    // do not separate likely drum families with useful confidence.
    const bool hasReliablePitch = sample.pitchConfidence >= 0.65 && sample.fundamentalHz > 0.0;
    const bool hasTomLikePitch = hasReliablePitch && sample.fundamentalHz >= 125.0 && sample.fundamentalHz <= 380.0;
    if (sample.transientCount >= 3 && sample.spectralCentroidHz >= 1800.0 && sample.spectralFlatness >= 0.20
        && sample.durationSeconds >= 0.05 && sample.durationSeconds <= 0.6 && transientRatio > 0.45)
    {
        out.category = "Clap";
        out.confidence = juce::jlimit(0.42, 0.68, 0.42 + juce::jmin(0.15, (sample.transientCount - 2) * 0.025)
            + juce::jmin(0.08, sample.spectralFlatness * 0.08) + (crest > 4.0 ? 0.03 : 0.0));
    }
    else if (hasTomLikePitch && sample.spectralCentroidHz >= 300.0 && sample.spectralCentroidHz < 1600.0
        && sample.durationSeconds >= 0.25 && sample.durationSeconds <= 1.5 && transientRatio > 0.45)
    {
        out.category = "Tom";
        out.confidence = juce::jlimit(0.38, 0.68, 0.42 + juce::jmin(0.18, transientRatio * 0.06) + (crest > 4.0 ? 0.04 : 0.0));
    }
    else
    {
        const bool hasLowKickPitch = sample.pitchConfidence < 0.65 || sample.fundamentalHz <= 120.0;
        if (sample.spectralCentroidHz > 0.0 && sample.spectralCentroidHz < 1300.0
            && sample.durationSeconds < 1.2 && transientRatio > 0.45 && hasLowKickPitch)
        {
            out.category = "Kick";
            out.confidence = juce::jlimit(0.35, 0.72, 0.62 - sample.spectralCentroidHz / 12000.0 + (transientRatio > 1.0 ? 0.04 : 0.0));
        }
        else if (sample.spectralCentroidHz >= 1800.0 && transientRatio > 0.45)
        {
            const bool noiseLikeUnpitchedHit = sample.spectralFlatness >= 0.55
                && (sample.pitchConfidence < 0.65 || sample.fundamentalHz <= 0.0);
            out.category = noiseLikeUnpitchedHit
                ? (sample.durationSeconds < 0.32 ? "Closed hi-hat" : "Open hi-hat")
                : "Snare";
            out.confidence = juce::jlimit(0.30, 0.68, 0.42 + juce::jmin(0.2, transientRatio * 0.08) + (crest > 4.0 ? 0.04 : 0.0));
        }
    }
    return out;
}
};

std::array<double, 7> normalizedFeatures(const ScannedSample& sample)
{
    const auto rms = juce::jmax(sample.rms, 0.00001);
    const auto centroid = juce::jmax(sample.spectralCentroidHz, 0.0);
    const auto pitch = sample.pitchConfidence >= 0.35 && sample.fundamentalHz > 0.0
        ? std::log2(juce::jmax(sample.fundamentalHz, 20.0)) / std::log2(22050.0) : 0.0;
    return {
        juce::jlimit(0.0, 1.0, std::log1p(juce::jmax(sample.durationSeconds, 0.0)) / std::log1p(8.0)),
        centroid > 0.0 ? juce::jlimit(0.0, 1.0, std::log2(centroid + 1.0) / std::log2(22051.0)) : 0.0,
        juce::jlimit(0.0, 1.0, sample.spectralFlatness),
        juce::jlimit(0.0, 1.0, std::log1p(juce::jmax(sample.transientStrength, 0.0) / rms) / std::log1p(100.0)),
        juce::jlimit(0.0, 1.0, static_cast<double>(juce::jmax(0, sample.transientCount)) / 12.0),
        juce::jlimit(0.0, 1.0, pitch),
        juce::jlimit(0.0, 1.0, sample.pitchConfidence)
    };
}

double featureDistance(const ScannedSample& left, const ScannedSample& right)
{
    const auto a = normalizedFeatures(left);
    const auto b = normalizedFeatures(right);
    constexpr std::array<double, 7> weights{1.0, 1.0, 0.75, 1.0, 0.5, 0.8, 0.7};
    double weightedSquares = 0.0;
    double totalWeight = 0.0;
    for (size_t index = 0; index < weights.size(); ++index)
    {
        weightedSquares += weights[index] * (a[index] - b[index]) * (a[index] - b[index]);
        totalWeight += weights[index];
    }
    const auto scalarDistance = std::sqrt(weightedSquares / totalWeight);

    const auto fingerprintDistance = [](const ScannedSample& first, const ScannedSample& second)
    {
        constexpr size_t segmentCount = 4;
        constexpr size_t bandCount = 16;
        constexpr size_t encodedLength = segmentCount * bandCount * 2;
        if (first.acousticFingerprintVersion != 1 || second.acousticFingerprintVersion != 1
            || first.acousticFingerprint.length() != static_cast<int>(encodedLength)
            || second.acousticFingerprint.length() != static_cast<int>(encodedLength))
            return -1.0;

        const auto firstText = first.acousticFingerprint.toStdString();
        const auto secondText = second.acousticFingerprint.toStdString();
        const auto decodeNibble = [](char character) -> int
        {
            if (character >= '0' && character <= '9') return static_cast<int>(character - '0');
            if (character >= 'a' && character <= 'f') return static_cast<int>(character - 'a' + 10);
            if (character >= 'A' && character <= 'F') return static_cast<int>(character - 'A' + 10);
            return -1;
        };
        std::array<std::uint8_t, segmentCount * bandCount> leftBands{};
        std::array<std::uint8_t, segmentCount * bandCount> rightBands{};
        for (size_t index = 0; index < leftBands.size(); ++index)
        {
            const auto leftHigh = decodeNibble(firstText[index * 2]);
            const auto leftLow = decodeNibble(firstText[index * 2 + 1]);
            const auto rightHigh = decodeNibble(secondText[index * 2]);
            const auto rightLow = decodeNibble(secondText[index * 2 + 1]);
            if (leftHigh < 0 || leftLow < 0 || rightHigh < 0 || rightLow < 0)
                return -1.0;
            leftBands[index] = static_cast<std::uint8_t>((leftHigh << 4) | leftLow);
            rightBands[index] = static_cast<std::uint8_t>((rightHigh << 4) | rightLow);
        }

        double segmentDivergence = 0.0;
        int validSegments = 0;
        for (size_t segment = 0; segment < segmentCount; ++segment)
        {
            double leftTotal = 0.0;
            double rightTotal = 0.0;
            for (size_t band = 0; band < bandCount; ++band)
            {
                leftTotal += leftBands[segment * bandCount + band];
                rightTotal += rightBands[segment * bandCount + band];
            }
            if (leftTotal <= 0.0 || rightTotal <= 0.0)
                continue;

            double divergence = 0.0;
            for (size_t band = 0; band < bandCount; ++band)
            {
                const auto leftProbability = leftBands[segment * bandCount + band] / leftTotal;
                const auto rightProbability = rightBands[segment * bandCount + band] / rightTotal;
                const auto midpoint = (leftProbability + rightProbability) * 0.5;
                if (leftProbability > 0.0)
                    divergence += 0.5 * leftProbability * std::log2(leftProbability / midpoint);
                if (rightProbability > 0.0)
                    divergence += 0.5 * rightProbability * std::log2(rightProbability / midpoint);
            }
            segmentDivergence += juce::jlimit(0.0, 1.0, divergence);
            ++validSegments;
        }
        return validSegments > 0 ? std::sqrt(segmentDivergence / validSegments) : -1.0;
    };

    const auto spectrumDistance = fingerprintDistance(left, right);
    return spectrumDistance >= 0.0 ? scalarDistance * 0.9 + spectrumDistance * 0.1 : scalarDistance;
}
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

ManualExampleDrumClassificationModel::ManualExampleDrumClassificationModel(std::vector<ScannedSample> trainingSamples)
    : examples(std::move(trainingSamples))
{
}

DrumClassification ManualExampleDrumClassificationModel::classify(const ScannedSample& sample) const
{
    const auto baseline = HeuristicDrumClassificationModel{}.classify(sample);
    if (baseline.confidence >= 0.75 || sample.durationSeconds <= 0.0 || sample.rms <= 0.00001
        || sample.peak <= 0.0001 || examples.empty())
        return baseline;

    static const juce::StringArray supportedCategories{
        "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Clap",
        "Tom", "Rimshot", "Ride", "Crash", "Percussion"
    };
    struct Neighbour { double distance; const ScannedSample* sample; };
    std::vector<Neighbour> neighbours;
    neighbours.reserve(examples.size());
    for (const auto& example : examples)
    {
        if (!supportedCategories.contains(example.category) || example.durationSeconds <= 0.0
            || example.rms <= 0.00001 || example.peak <= 0.0001)
            continue;
        const auto distance = featureDistance(sample, example);
        if (distance <= 0.28)
            neighbours.push_back({distance, &example});
    }
    if (neighbours.size() < 2)
        return baseline;

    std::sort(neighbours.begin(), neighbours.end(), [](const Neighbour& left, const Neighbour& right)
    {
        return left.distance < right.distance;
    });
    if (neighbours.size() > 5)
        neighbours.resize(5);

    struct Vote { double weight = 0.0, distance = 0.0; int count = 0; };
    std::map<juce::String, Vote> votes;
    double totalWeight = 0.0;
    for (const auto& neighbour : neighbours)
    {
        const auto weight = 1.0 / (0.03 + neighbour.distance);
        auto& vote = votes[neighbour.sample->category];
        vote.weight += weight;
        vote.distance += neighbour.distance;
        ++vote.count;
        totalWeight += weight;
    }
    auto winner = votes.end();
    for (auto candidate = votes.begin(); candidate != votes.end(); ++candidate)
        if (winner == votes.end() || candidate->second.weight > winner->second.weight)
            winner = candidate;
    if (winner == votes.end() || winner->second.count < 2 || totalWeight <= 0.0)
        return baseline;

    const auto agreement = winner->second.weight / totalWeight;
    const auto meanDistance = winner->second.distance / winner->second.count;
    if (agreement < 0.72 || meanDistance > 0.22)
        return baseline;

    auto result = baseline;
    result.category = winner->first;
    result.confidence = juce::jlimit(0.55, 0.74, 0.54 + agreement * 0.18 + (0.22 - meanDistance) * 0.08);
    if (!result.tags.contains("User-trained suggestion"))
        result.tags.add("User-trained suggestion");
    return result;
}
