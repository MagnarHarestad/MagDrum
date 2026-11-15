#include "core/AudioClassifier.h"
#include "core/SampleClassificationReview.h"
#include "core/SampleDatabase.h"
#include "core/WavScanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Confusion = std::map<std::pair<std::string, std::string>, std::uint64_t>;

struct LabeledSample
{
    ScannedSample sample;
    std::string label;
};

struct Score
{
    std::uint64_t samples = 0;
    std::uint64_t correct = 0;
    std::uint64_t skippedWithoutLabel = 0;
    std::uint64_t aboveReviewThreshold = 0;
    std::uint64_t correctAboveReviewThreshold = 0;
    std::set<std::string> labels;
    Confusion confusion;

    void add(const std::string& expected, const DrumClassification& predicted)
    {
        const auto actual = predicted.category.toStdString();
        ++samples;
        ++confusion[{expected, actual}];
        labels.insert(expected);
        labels.insert(actual);
        if (expected == actual) ++correct;
        if (actual != "Unknown" && predicted.confidence >= sampleClassificationReviewThreshold)
        {
            ++aboveReviewThreshold;
            if (expected == actual) ++correctAboveReviewThreshold;
        }
    }

    void print(const char* title) const
    {
        std::cout << "\n" << title << "\n";
        if (samples == 0)
        {
            std::cout << "No labeled audio files were analyzed.\n";
            return;
        }

        std::cout << "Labeled samples: " << samples
                  << "  Correct: " << correct
                  << "  Accuracy: " << std::fixed << std::setprecision(1)
                  << (100.0 * static_cast<double>(correct) / static_cast<double>(samples)) << "%\n";
        std::cout << "At the app review threshold (confidence >= "
                  << std::setprecision(2) << sampleClassificationReviewThreshold << "): "
                  << aboveReviewThreshold << '/' << samples << " accepted ("
                  << std::setprecision(1)
                  << (100.0 * static_cast<double>(aboveReviewThreshold) / static_cast<double>(samples))
                  << "% coverage), accuracy among accepted: ";
        if (aboveReviewThreshold == 0)
            std::cout << "n/a\n";
        else
            std::cout << std::setprecision(1)
                      << (100.0 * static_cast<double>(correctAboveReviewThreshold)
                          / static_cast<double>(aboveReviewThreshold)) << "%\n";
        std::set<std::string> predictions;
        for (const auto& entry : confusion) predictions.insert(entry.first.second);
        std::cout << "Confusion matrix (rows=actual, columns=predicted)\n"
                  << std::left << std::setw(18) << "Actual";
        for (const auto& prediction : predictions)
            std::cout << std::right << std::setw(18) << prediction;
        std::cout << '\n';
        for (const auto& expected : labels)
        {
            const auto hasSupport = std::any_of(confusion.begin(), confusion.end(), [&](const auto& entry)
            {
                return entry.first.first == expected;
            });
            if (!hasSupport) continue;
            std::cout << std::left << std::setw(18) << expected;
            for (const auto& prediction : predictions)
            {
                const auto found = confusion.find({expected, prediction});
                const auto count = found != confusion.end() ? found->second : 0;
                std::cout << std::right << std::setw(18) << count;
            }
            std::cout << '\n';
        }
        std::cout << "Class                 Support  Precision  Recall  F1\n";

        double f1Sum = 0.0;
        std::size_t f1Classes = 0;
        for (const auto& label : labels)
        {
            std::uint64_t truePositive = 0;
            std::uint64_t falsePositive = 0;
            std::uint64_t falseNegative = 0;
            for (const auto& [pair, count] : confusion)
            {
                if (pair.first == label && pair.second == label) truePositive += count;
                else if (pair.first != label && pair.second == label) falsePositive += count;
                else if (pair.first == label && pair.second != label) falseNegative += count;
            }

            const auto support = truePositive + falseNegative;
            if (support == 0) continue;
            const double precision = truePositive + falsePositive > 0
                ? static_cast<double>(truePositive) / static_cast<double>(truePositive + falsePositive) : 0.0;
            const double recall = static_cast<double>(truePositive) / static_cast<double>(support);
            const double f1 = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall) : 0.0;
            f1Sum += f1;
            ++f1Classes;
            std::cout << std::left << std::setw(22) << label << std::right << std::setw(7) << support
                      << std::setw(11) << std::setprecision(3) << precision
                      << std::setw(9) << recall << std::setw(7) << f1 << '\n';
        }
        if (f1Classes > 0)
            std::cout << "Macro F1 (labeled classes): " << std::setprecision(3)
                      << f1Sum / static_cast<double>(f1Classes) << '\n';
    }
};

juce::String canonicalFolderName(juce::String name)
{
    name = name.toLowerCase().replaceCharacters("_-.", "   ").trim();
    while (name.contains("  ")) name = name.replace("  ", " ");
    return name;
}

juce::String labelForFolder(const juce::String& rawName)
{
    const auto name = canonicalFolderName(rawName);
    if (name == "kick" || name == "kicks" || name == "bass drum" || name == "bass drums") return "Kick";
    if (name == "snare" || name == "snares") return "Snare";
    if (name == "closed hat" || name == "closed hats" || name == "hi hat" || name == "hi hats"
        || name == "hihat" || name == "hihats" || name == "hi-hat") return "Closed hi-hat";
    if (name == "open hat" || name == "open hats" || name == "open hi hat" || name == "open hi hats") return "Open hi-hat";
    if (name == "pedal hat" || name == "pedal hats" || name == "foot hat" || name == "foot hats") return "Pedal hi-hat";
    if (name == "clap" || name == "claps" || name == "handclap" || name == "handclaps") return "Clap";
    if (name == "tom" || name == "toms" || name == "floor tom" || name == "floor toms" || name == "rack tom" || name == "rack toms") return "Tom";
    if (name == "rimshot" || name == "rimshots" || name == "side stick" || name == "side sticks") return "Rimshot";
    if (name == "ride" || name == "rides" || name == "ride cymbal" || name == "ride cymbals") return "Ride";
    if (name == "crash" || name == "crashes" || name == "crash cymbal" || name == "crash cymbals") return "Crash";
    if (name == "percussion" || name == "percs" || name == "perc" || name == "percussions") return "Percussion";
    return {};
}

juce::String labelForSample(const juce::File& file, const juce::File& root)
{
    auto folder = file.getParentDirectory();
    while (folder != root && folder.getFullPathName().isNotEmpty())
    {
        const auto label = labelForFolder(folder.getFileName());
        if (label.isNotEmpty()) return label;
        const auto parent = folder.getParentDirectory();
        if (parent == folder) break;
        folder = parent;
    }
    return {};
}

void printUsage()
{
    std::cout << "Usage: MagDrumClassifierEvaluation <labeled-root> [--feature-summary]\n"
                 "Place samples under category folders such as kicks/, snares/, claps/,\n"
                 "hi-hats/, open-hats/, toms/, rides/, crashes/ or percs/. Unrecognized\n"
                 "folders are skipped. --feature-summary prints per-category median and\n"
                 "interquartile ranges to help diagnose acoustic feature overlap. Reports\n"
                 "are local only; audio files are not copied.\n";
}

double quantile(std::vector<double> values, double fraction)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto position = fraction * static_cast<double>(values.size() - 1);
    const auto lower = static_cast<std::size_t>(position);
    const auto upper = std::min(values.size() - 1, lower + 1);
    const auto blend = position - static_cast<double>(lower);
    return values[lower] * (1.0 - blend) + values[upper] * blend;
}

void printFeatureSummary(const std::vector<LabeledSample>& samples)
{
    std::map<std::string, std::vector<const ScannedSample*>> byCategory;
    for (const auto& labeled : samples)
        byCategory[labeled.label].push_back(&labeled.sample);

    const auto range = [](const std::vector<const ScannedSample*>& group, auto select)
    {
        std::vector<double> values;
        values.reserve(group.size());
        for (const auto* sample : group)
        {
            const auto value = select(*sample);
            if (std::isfinite(value)) values.push_back(value);
        }
        if (values.empty()) return std::string("n/a");
        std::ostringstream output;
        output << std::fixed << std::setprecision(2)
               << quantile(values, 0.50) << " [" << quantile(values, 0.25)
               << "–" << quantile(values, 0.75) << ']';
        return output.str();
    };

    std::cout << "\nAcoustic feature summary (median [25th–75th percentile])\n"
                 "Values are dataset diagnostics, not classification thresholds.\n"
                 "Class | Duration (s) | Centroid (Hz) | Flatness | Transient/RMS | Attacks | Pitch (Hz) | Pitch confidence\n";
    for (const auto& [category, group] : byCategory)
    {
        std::cout << category << " | "
                  << range(group, [](const auto& s) { return s.durationSeconds; }) << " | "
                  << range(group, [](const auto& s) { return s.spectralCentroidHz; }) << " | "
                  << range(group, [](const auto& s) { return s.spectralFlatness; }) << " | "
                  << range(group, [](const auto& s) { return s.transientStrength / juce::jmax(s.rms, 0.00001); }) << " | "
                  << range(group, [](const auto& s) { return static_cast<double>(s.transientCount); }) << " | "
                  << range(group, [](const auto& s) { return s.pitchConfidence >= 0.35 ? s.fundamentalHz : std::numeric_limits<double>::quiet_NaN(); }) << " | "
                  << range(group, [](const auto& s) { return s.pitchConfidence; }) << '\n';
    }
}

bool evaluatePerceptualCandidates(const std::vector<LabeledSample>& samples)
{
    if (samples.empty()) return false;

    const auto temporaryDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("MagDrumSimilarityEvaluation-" + juce::Uuid().toString());
    if (!temporaryDirectory.createDirectory().wasOk())
    {
        std::cerr << "Could not create a temporary similarity-evaluation database directory.\n";
        return false;
    }

    bool succeeded = true;
    std::uint64_t candidateCount = 0;
    std::uint64_t sameCategoryCandidates = 0;
    std::uint64_t queriesWithCandidates = 0;
    std::uint64_t topCandidateSameCategory = 0;
    {
        SampleDatabase database;
        if (!database.open(temporaryDirectory.getChildFile("similarity.sqlite3")))
        {
            std::cerr << "Could not initialize the temporary similarity-evaluation database.\n";
            succeeded = false;
        }
        else
        {
            std::map<std::string, std::string> labelsByPath;
            for (const auto& labeled : samples)
            {
                if (!database.insertSample(labeled.sample))
                {
                    std::cerr << "Could not index evaluation metadata for "
                              << labeled.sample.file.getFullPathName() << '\n';
                    succeeded = false;
                    break;
                }
                labelsByPath.emplace(labeled.sample.file.getFullPathName().toStdString(), labeled.label);
            }

            if (succeeded)
            {
                constexpr int candidateLimit = 10;
                for (const auto& query : samples)
                {
                    const auto candidates = database.findPerceptualDuplicates(query.sample.file, 0.85, candidateLimit);
                    if (!candidates.empty())
                    {
                        ++queriesWithCandidates;
                        const auto firstLabel = labelsByPath.find(candidates.front().file.getFullPathName().toStdString());
                        if (firstLabel != labelsByPath.end() && firstLabel->second == query.label)
                            ++topCandidateSameCategory;
                    }

                    for (const auto& candidate : candidates)
                    {
                        ++candidateCount;
                        const auto label = labelsByPath.find(candidate.file.getFullPathName().toStdString());
                        if (label != labelsByPath.end() && label->second == query.label)
                            ++sameCategoryCandidates;
                    }
                }
            }
        }
    }
    temporaryDirectory.deleteRecursively();

    if (!succeeded) return false;
    std::cout << "\nPerceptual candidate category agreement (production database search, threshold 0.85, top 10)\n"
              << "Labeled queries: " << samples.size()
              << "  Queries with candidates: " << queriesWithCandidates << '\n';
    if (candidateCount == 0)
    {
        std::cout << "No near-duplicate candidates were returned.\n";
        return true;
    }

    std::cout << "Returned candidates: " << candidateCount
              << "  Same-folder-category candidates: " << sameCategoryCandidates
              << "  Category agreement: " << std::fixed << std::setprecision(1)
              << (100.0 * static_cast<double>(sameCategoryCandidates) / static_cast<double>(candidateCount)) << "%\n";
    if (queriesWithCandidates > 0)
        std::cout << "Top candidate matched the query category: " << topCandidateSameCategory << '/'
                  << queriesWithCandidates << " queries ("
                  << (100.0 * static_cast<double>(topCandidateSameCategory) / static_cast<double>(queriesWithCandidates))
                  << "%)\n";
    std::cout << "Folder-category agreement is a weak label proxy; it does not verify human-perceived similarity.\n";
    return true;
}

void evaluateManualExampleModel(const std::vector<LabeledSample>& samples, bool leaveOneKitOut)
{
    Score score;
    std::set<std::string> kits;
    for (const auto& sample : samples)
        kits.insert(sample.sample.file.getParentDirectory().getParentDirectory().getFileName().toStdString());

    for (const auto& query : samples)
    {
        std::vector<ScannedSample> examples;
        for (const auto& candidate : samples)
        {
            if (candidate.sample.file == query.sample.file) continue;
            if (leaveOneKitOut
                && candidate.sample.file.getParentDirectory().getParentDirectory()
                       == query.sample.file.getParentDirectory().getParentDirectory())
                continue;
            auto example = candidate.sample;
            example.category = candidate.label;
            examples.push_back(std::move(example));
        }

        auto anonymousQuery = query.sample;
        anonymousQuery.file = juce::File("anonymous-audio.wav");
        const auto prediction = ManualExampleDrumClassificationModel(std::move(examples)).classify(anonymousQuery);
        score.add(query.label, prediction);
    }

    const auto title = leaveOneKitOut ? "Manually labeled examples (leave one kit out)"
                                      : "Manually labeled examples (leave one sample out)";
    score.print(title);
    if (leaveOneKitOut)
    {
        std::cout << "Kits evaluated: " << kits.size()
                  << "  Training excludes every other sample from the held-out kit.\n";
    }
}
}

int main(int argc, char** argv)
{
    const bool showFeatureSummary = argc == 3 && std::string(argv[2]) == "--feature-summary";
    if (argc < 2 || argc > 3 || (argc == 3 && !showFeatureSummary))
    {
        printUsage();
        return 2;
    }

    const juce::File root(juce::String::fromUTF8(argv[1]));
    if (!root.isDirectory())
    {
        std::cerr << "Dataset root is not a directory: " << root.getFullPathName() << '\n';
        return 2;
    }

    AudioClassifier classifier;
    WavScanner scanner;
    WavScanner::Stats scanStats;
    Score withFilenameCues;
    Score withoutFilenameCues;
    std::vector<LabeledSample> labeledSamples;
    scanner.scan(root, {}, {}, &scanStats, [&](const ScannedSample& sample)
    {
        const auto expected = labelForSample(sample.file, root);
        if (expected.isEmpty())
        {
            ++withFilenameCues.skippedWithoutLabel;
            ++withoutFilenameCues.skippedWithoutLabel;
            return true;
        }

        withFilenameCues.add(expected.toStdString(), classifier.classify(sample));
        auto anonymousSample = sample;
        anonymousSample.file = juce::File("anonymous-audio.wav");
        withoutFilenameCues.add(expected.toStdString(), classifier.classify(anonymousSample));
        labeledSamples.push_back({ sample, expected.toStdString() });
        return true;
    });

    std::cout << "MagDrum local classifier evaluation\n"
              << "Root: " << root.getFullPathName() << '\n'
              << "Files analyzed: " << scanStats.filesAnalyzed
              << "  Decode errors: " << scanStats.errors
              << "  Unlabeled files skipped: " << withFilenameCues.skippedWithoutLabel << '\n'
              << "Metrics are for this dataset only. Confidence values are not calibrated.\n";
    withFilenameCues.print("Filename and audio features");
    withoutFilenameCues.print("Audio features only (filename hidden)");
    if (showFeatureSummary) printFeatureSummary(labeledSamples);
    evaluateManualExampleModel(labeledSamples, false);
    std::set<std::string> labeledKits;
    for (const auto& sample : labeledSamples)
        labeledKits.insert(sample.sample.file.getParentDirectory().getParentDirectory().getFileName().toStdString());
    if (labeledKits.size() > 1)
        evaluateManualExampleModel(labeledSamples, true);
    if (!evaluatePerceptualCandidates(labeledSamples)) return 1;
    return scanStats.filesAnalyzed > 0 && withFilenameCues.samples > 0 ? 0 : 1;
}
