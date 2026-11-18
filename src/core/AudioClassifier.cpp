#include "AudioClassifier.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>

namespace
{
bool hasPercussionInstrumentCue(juce::String name)
{
    constexpr char separators[] = "-_.,;()[]{}+";
    name = name.toLowerCase();
    for (const auto* separator = separators; *separator != '\0'; ++separator)
        name = name.replaceCharacter(*separator, ' ');

    constexpr const char* cues[]{
        "conga", "congas", "bongo", "bongos", "cowbell", "shaker", "shakers", "tambourine",
        "tambourines", "agogo", "agogo bell", "agogo bells", "woodblock", "woodblocks",
        "wood block", "wood blocks", "triangle", "triangles", "clave", "claves", "guiro",
        "guiros", "güiro", "güiros", "timbale", "timbales", "cabasa", "djembe", "djembes",
        "cajon", "cajón", "castanet", "castanets", "maraca", "maracas", "vibraslap"
    };
    for (const auto* cue : cues)
        if (name.containsWholeWordIgnoreCase(cue))
            return true;
    return false;
}
juce::String drumCategoryForFolder(juce::String folderName)
{
    constexpr char separators[] = "-_.,;()[]{}+";
    folderName = folderName.toLowerCase();
    for (const auto* separator = separators; *separator != '\0'; ++separator)
        folderName = folderName.replaceCharacter(*separator, ' ');
    folderName = folderName.trim();
    while (folderName.contains("  ")) folderName = folderName.replace("  ", " ");

    static const std::array<std::pair<const char*, const char*>, 52> aliases{{
        {"kick", "Kick"}, {"kicks", "Kick"}, {"bass drum", "Kick"}, {"bass drums", "Kick"},
        {"bassdrum", "Kick"}, {"bassdrums", "Kick"}, {"bd", "Kick"}, {"808s", "Kick"},
        {"snare", "Snare"}, {"snares", "Snare"}, {"snr", "Snare"}, {"sd", "Snare"},
        {"closed hat", "Closed hi-hat"}, {"closed hats", "Closed hi-hat"},
        {"closed hihat", "Closed hi-hat"}, {"closed hihats", "Closed hi-hat"},
        {"closed hi hat", "Closed hi-hat"}, {"closed hi hats", "Closed hi-hat"},
        {"chh", "Closed hi-hat"}, {"hhc", "Closed hi-hat"},
        {"open hat", "Open hi-hat"}, {"open hats", "Open hi-hat"},
        {"open hihat", "Open hi-hat"}, {"open hihats", "Open hi-hat"},
        {"open hi hat", "Open hi-hat"}, {"open hi hats", "Open hi-hat"},
        {"ohh", "Open hi-hat"}, {"hho", "Open hi-hat"},
        {"pedal hat", "Pedal hi-hat"}, {"pedal hats", "Pedal hi-hat"},
        {"pedal hihat", "Pedal hi-hat"}, {"pedal hihats", "Pedal hi-hat"},
        {"pedal hi hat", "Pedal hi-hat"}, {"pedal hi hats", "Pedal hi-hat"},
        {"foot hat", "Pedal hi-hat"}, {"foot hats", "Pedal hi-hat"},
        {"phh", "Pedal hi-hat"}, {"hhp", "Pedal hi-hat"},
        {"clap", "Clap"}, {"claps", "Clap"}, {"handclap", "Clap"}, {"handclaps", "Clap"},
        {"tom", "Tom"}, {"toms", "Tom"}, {"floor tom", "Tom"}, {"rack tom", "Tom"}, {"htom", "Tom"}, {"ltom", "Tom"}, {"tomhi", "Tom"}, {"tomlo", "Tom"},
        {"ride", "Ride"}, {"rides", "Ride"}
    }};
    for (const auto& [alias, category] : aliases)
        if (folderName == alias) return category;

    if (folderName == "rimshot" || folderName == "rimshots" || folderName == "side stick" || folderName == "side sticks") return "Rimshot";
    if (folderName == "crash" || folderName == "crashes" || folderName == "crash cymbal" || folderName == "crash cymbals") return "Crash";
    if (folderName == "percussion" || folderName == "percussions" || folderName == "perc" || folderName == "percs"
        || folderName == "conga" || folderName == "congas" || folderName == "bongo" || folderName == "bongos"
        || folderName == "cowbell" || folderName == "shaker" || folderName == "tambourine"
        || hasPercussionInstrumentCue(folderName)) return "Percussion";
    if (folderName == "ride cymbal" || folderName == "ride cymbals") return "Ride";
    return {};
}

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

    const bool hasHiHatCue = has({"hi hat", "hihat", "hh", "closed hat", "closed hats", "closedhat",
                                  "open hat", "open hats", "openhat", "pedal hat", "pedal hats", "pedalhat",
                                  "foot hat", "foot hats", "chh", "hhc", "ohh", "hho", "phh", "hhp"});
    const bool hasClosedHiHatCue = has({"closed hat", "closed hats", "closedhat", "closed hihat",
                                        "closed hi hat", "closed hihats", "closed hi hats", "closed hh",
                                        "hh closed", "chh", "hhc"});
    const bool hasOpenHiHatCue = has({"open hat", "open hats", "openhat", "open hihat", "open hi hat",
                                      "open hihats", "open hi hats", "open hh", "hh open", "ohh", "hho"});
    const bool hasPedalHiHatCue = has({"pedal hat", "pedal hats", "pedalhat", "pedal hihat", "pedal hi hat",
                                       "pedal hihats", "pedal hi hats", "pedal hh", "hh pedal",
                                       "foot hat", "foot hats", "phh", "hhp"});
    if (hasHiHatCue)
        addTag("Hi-hat");

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
    if (hasClosedHiHatCue) namedCategories.add("Closed hi-hat");
    if (hasOpenHiHatCue) namedCategories.add("Open hi-hat");
    if (hasPedalHiHatCue) namedCategories.add("Pedal hi-hat");
    if (has({"clap", "handclap"})) namedCategories.add("Clap");
    if (has({"tom", "floor tom", "rack tom", "tomhi", "tomlo", "htom", "ltom"})) namedCategories.add("Tom");
    if (has({"rimshot", "rim shot", "side stick", "sidestick"})) namedCategories.add("Rimshot");
    if (has({"ride", "ride cymbal", "ridebell", "ride bell"})) namedCategories.add("Ride");
    if (has({"crash", "crash cymbal"})) namedCategories.add("Crash");
    if (hasPercussionInstrumentCue(name)) namedCategories.add("Percussion");

    bool categoryFromFolder = false;
    auto folder = sample.file.getParentDirectory();
    for (int depth = 0; depth < 5 && folder.getFileName().isNotEmpty(); ++depth)
    {
        const auto folderCategory = drumCategoryForFolder(folder.getFileName());
        if (folderCategory.isNotEmpty())
        {
            if (!namedCategories.contains(folderCategory)) namedCategories.add(folderCategory);
            categoryFromFolder = true;
            break;
        }
        const auto parent = folder.getParentDirectory();
        if (parent == folder) break;
        folder = parent;
    }

    if (namedCategories.contains("Rimshot") && namedCategories.contains("Snare"))
        namedCategories.removeString("Snare");

    if (namedCategories.size() == 1)
    {
        out.category = namedCategories[0];
        const bool filenameCategoryCue = has({"808", "909", "kick", "bd", "snare", "snr", "sd", "hihat", "hi-hat", "hi hat", "hh", "chh", "ohh", "phh", "hhc", "hho", "hhp", "tom", "tomhi", "tomlo", "htom", "ltom", "clap", "ride", "ridebell", "crash", "rimshot", "side stick", "sidestick", "cross stick", "cross-stick", "x-stick"}) || hasPercussionInstrumentCue(name);
        out.confidence = categoryFromFolder ? (filenameCategoryCue ? 0.97 : 0.84) : (filenameCategoryCue ? 0.92 : 0.82);
        return out;
    }
    if (namedCategories.size() > 1)
        return out;

    // Classification intentionally stays Unknown when the available measurements
    // do not separate likely drum families with useful confidence.
    const bool hasReliablePitch = sample.pitchConfidence >= 0.65 && sample.fundamentalHz > 0.0;
    const bool hasTomLikePitch = hasReliablePitch && sample.fundamentalHz >= 95.0 && sample.fundamentalHz <= 380.0;
    if (sample.transientCount >= 3 && sample.spectralCentroidHz >= 1800.0 && sample.spectralFlatness >= 0.20
        && sample.durationSeconds >= 0.05 && sample.durationSeconds <= 0.6 && transientRatio > 0.45)
    {
        out.category = "Clap";
        out.confidence = juce::jlimit(0.42, 0.68, 0.42 + juce::jmin(0.15, (sample.transientCount - 2) * 0.025)
            + juce::jmin(0.08, sample.spectralFlatness * 0.08) + (crest > 4.0 ? 0.03 : 0.0));
    }
    else if ((hasTomLikePitch || sample.pitchConfidence < 0.65)
        && sample.spectralCentroidHz >= 350.0 && sample.spectralCentroidHz < 1250.0
        && sample.durationSeconds >= 1.5 && sample.durationSeconds <= 6.0 && transientRatio > 0.45)
    {
        out.category = "Tom";
        out.confidence = juce::jlimit(0.38, 0.68, 0.42 + juce::jmin(0.18, transientRatio * 0.06) + (crest > 4.0 ? 0.04 : 0.0));
    }
    else
    {
        const bool hasLowKickPitch = sample.pitchConfidence < 0.65 || sample.fundamentalHz <= 120.0;
        // Some short low-frequency kicks have a strong, stable overtone that
        // misleads the pitch estimator. Allow a conservative spectral/transient
        // fallback without requiring that unreliable pitch estimate to be low.
        const bool hasLowKickSpectralShape = sample.spectralCentroidHz > 0.0
            && sample.spectralCentroidHz < 200.0 && sample.durationSeconds <= 2.5
            && transientRatio > 1.5;
        if (sample.spectralCentroidHz > 0.0 && sample.spectralCentroidHz < 450.0
            && sample.durationSeconds < 3.0 && transientRatio > 0.45
            && (hasLowKickPitch || hasLowKickSpectralShape))
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

double featureDistance(const ScannedSample& left, const ScannedSample& right, double acousticWeight)
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
    const auto boundedAcousticWeight = juce::jlimit(0.0, 1.0, acousticWeight);
    return spectrumDistance >= 0.0
        ? scalarDistance * (1.0 - boundedAcousticWeight) + spectrumDistance * boundedAcousticWeight
        : scalarDistance;
}

const juce::StringArray& supportedExampleCategories()
{
    static const juce::StringArray categories{
        "Kick", "Snare", "Closed hi-hat", "Open hi-hat", "Pedal hi-hat", "Clap",
        "Tom", "Rimshot", "Ride", "Crash", "Percussion"
    };
    return categories;
}

DrumClassification classifyWithExamples(const ScannedSample& sample,
                                        const std::vector<ScannedSample>& examples,
                                        double acousticWeight,
                                        const ScannedSample* excluded = nullptr)
{
    const auto baseline = HeuristicDrumClassificationModel{}.classify(sample);
    if (baseline.confidence >= 0.75 || sample.durationSeconds <= 0.0 || sample.rms <= 0.00001
        || sample.peak <= 0.0001 || examples.empty())
        return baseline;

    struct Neighbour { double distance; const ScannedSample* sample; };
    std::vector<Neighbour> neighbours;
    neighbours.reserve(examples.size());
    for (const auto& example : examples)
    {
        if (&example == excluded || !supportedExampleCategories().contains(example.category)
            || example.durationSeconds <= 0.0 || example.rms <= 0.00001 || example.peak <= 0.0001)
            continue;
        const auto distance = featureDistance(sample, example, acousticWeight);
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

double selectAcousticWeight(const std::vector<ScannedSample>& examples)
{
    std::map<juce::String, std::vector<std::size_t>> examplesByCategory;
    for (std::size_t index = 0; index < examples.size(); ++index)
    {
        const auto& sample = examples[index];
        if (supportedExampleCategories().contains(sample.category) && sample.durationSeconds > 0.0
            && sample.rms > 0.00001 && sample.peak > 0.0001)
            examplesByCategory[sample.category].push_back(index);
    }

    std::vector<std::size_t> validationIndices;
    for (const auto& [category, indices] : examplesByCategory)
    {
        juce::ignoreUnused(category);
        const auto selectedCount = juce::jmin<std::size_t>(8, indices.size());
        for (std::size_t selected = 0; selected < selectedCount; ++selected)
        {
            const auto position = selectedCount == 1 ? 0 : selected * (indices.size() - 1) / (selectedCount - 1);
            validationIndices.push_back(indices[position]);
        }
    }
    if (validationIndices.size() < 8)
        return 0.1;

    constexpr std::array<double, 3> candidates{0.1, 0.25, 0.5};
    double bestWeight = candidates.front();
    double bestMacroF1 = -1.0;
    for (const auto weight : candidates)
    {
        std::map<juce::String, std::map<juce::String, int>> confusion;
        for (const auto index : validationIndices)
        {
            auto anonymousQuery = examples[index];
            anonymousQuery.file = juce::File("anonymous-audio.wav");
            const auto prediction = classifyWithExamples(anonymousQuery, examples, weight, &examples[index]);
            ++confusion[examples[index].category][prediction.category];
        }

        double f1Total = 0.0;
        int classCount = 0;
        for (const auto& [actual, predictions] : confusion)
        {
            int truePositive = 0;
            int falsePositive = 0;
            int falseNegative = 0;
            for (const auto& [expected, outcomes] : confusion)
            {
                const auto found = outcomes.find(actual);
                const auto predictedAsActual = found != outcomes.end() ? found->second : 0;
                if (expected == actual)
                    truePositive = predictedAsActual;
                else
                    falsePositive += predictedAsActual;

                for (const auto& [prediction, count] : outcomes)
                    if (expected == actual && prediction != actual)
                        falseNegative += count;
            }
            juce::ignoreUnused(predictions);
            const auto precision = truePositive + falsePositive > 0
                ? static_cast<double>(truePositive) / (truePositive + falsePositive) : 0.0;
            const auto recallDenominator = truePositive + falseNegative;
            const auto recall = recallDenominator > 0
                ? static_cast<double>(truePositive) / recallDenominator : 0.0;
            f1Total += precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
            ++classCount;
        }
        const auto macroF1 = classCount > 0 ? f1Total / classCount : 0.0;
        if (macroF1 > bestMacroF1 + 1.0e-9)
        {
            bestMacroF1 = macroF1;
            bestWeight = weight;
        }
    }
    return bestWeight;
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
    acousticFingerprintWeight = selectAcousticWeight(examples);
}

DrumClassification ManualExampleDrumClassificationModel::classify(const ScannedSample& sample) const
{
    return classifyWithExamples(sample, examples, acousticFingerprintWeight);
}
