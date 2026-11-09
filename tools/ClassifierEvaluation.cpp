#include "core/AudioClassifier.h"
#include "core/WavScanner.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace
{
using Confusion = std::map<std::pair<std::string, std::string>, std::uint64_t>;

struct Score
{
    std::uint64_t samples = 0;
    std::uint64_t correct = 0;
    std::uint64_t skippedWithoutLabel = 0;
    std::set<std::string> labels;
    Confusion confusion;

    void add(const std::string& expected, const juce::String& predicted)
    {
        const auto actual = predicted.toStdString();
        ++samples;
        ++confusion[{expected, actual}];
        labels.insert(expected);
        labels.insert(actual);
        if (expected == actual) ++correct;
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
    std::cout << "Usage: MagDrumClassifierEvaluation <labeled-root>\n"
                 "Place samples under category folders such as kicks/, snares/, claps/,\n"
                 "hi-hats/, open-hats/, toms/, rides/, crashes/ or percs/. Unrecognized\n"
                 "folders are skipped. Reports are local only; audio files are not copied.\n";
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
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
    scanner.scan(root, {}, {}, &scanStats, [&](const ScannedSample& sample)
    {
        const auto expected = labelForSample(sample.file, root);
        if (expected.isEmpty())
        {
            ++withFilenameCues.skippedWithoutLabel;
            ++withoutFilenameCues.skippedWithoutLabel;
            return true;
        }

        withFilenameCues.add(expected.toStdString(), classifier.classify(sample).category);
        auto anonymousSample = sample;
        anonymousSample.file = juce::File("anonymous-audio.wav");
        withoutFilenameCues.add(expected.toStdString(), classifier.classify(anonymousSample).category);
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
    return scanStats.filesAnalyzed > 0 && withFilenameCues.samples > 0 ? 0 : 1;
}
