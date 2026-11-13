#include "DefaultLibraryDiscovery.h"
#include <algorithm>
#include <unordered_set>

namespace magdrum::library
{
std::vector<juce::File> findDefaultLibraryFolders()
{
    const auto commonDocuments = juce::File::getSpecialLocation(juce::File::commonDocumentsDirectory);
    const auto commonData = juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
    const auto userDocuments = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto userMusic = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    const auto programFiles = juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);

    const std::vector<juce::File> candidates{
        commonDocuments.getChildFile("Native Instruments"),
        commonDocuments.getChildFile("Steinberg"),
        commonDocuments.getChildFile("VST Sound"),
        commonData.getChildFile("Native Instruments"),
        commonData.getChildFile("Steinberg/Content"),
        commonData.getChildFile("Spectrasonics/STEAM"),
        userDocuments.getChildFile("Native Instruments"),
        userDocuments.getChildFile("Steinberg"),
        userMusic.getChildFile("Native Instruments"),
        userMusic.getChildFile("Steinberg"),
        programFiles.getChildFile("Steinberg/Cubase 15/VST Sound")
    };

    std::vector<juce::File> found;
    std::unordered_set<std::string> uniquePaths;
    for (const auto& folder : candidates)
    {
        if (!folder.isDirectory())
            continue;
        const auto foldedPath = folder.getFullPathName().toLowerCase().toStdString();
        if (uniquePaths.insert(foldedPath).second)
            found.push_back(folder);
    }
    return found;
}

std::vector<juce::File> findDrumMidiPatternFiles(const std::vector<juce::File>& roots,
                                                 size_t maximumResults,
                                                 int maximumVisitedFiles,
                                                 const std::function<bool()>& shouldCancel)
{
    std::vector<juce::File> matches;
    if (maximumResults == 0 || maximumVisitedFiles <= 0)
        return matches;

    const char* keywords[]{"drum", "percussion", "beat", "groove", "rhythm", "pattern", "kit"};
    std::unordered_set<std::string> uniquePaths;
    int visited = 0;

    for (const auto& root : roots)
    {
        if ((shouldCancel && shouldCancel()) || matches.size() >= maximumResults || visited >= maximumVisitedFiles)
            break;
        if (!root.isDirectory())
            continue;

        for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles))
        {
            if ((shouldCancel && shouldCancel()) || matches.size() >= maximumResults || visited >= maximumVisitedFiles)
                break;
            ++visited;
            const auto file = entry.getFile();
            const auto extension = file.getFileExtension().toLowerCase();
            if (extension != ".mid" && extension != ".midi")
                continue;

            const auto fullPath = file.getFullPathName();
            const auto searchable = fullPath.toLowerCase();
            bool drumRelated = false;
            for (const auto* keyword : keywords)
                if (searchable.contains(keyword)) { drumRelated = true; break; }
            if (!drumRelated)
                continue;

            const auto foldedPath = fullPath.toLowerCase().toStdString();
            if (uniquePaths.insert(foldedPath).second)
                matches.push_back(file);
        }
    }

    std::sort(matches.begin(), matches.end(), [](const juce::File& left, const juce::File& right)
    {
        const auto folded = left.getFileName().compareIgnoreCase(right.getFileName());
        return folded == 0 ? left.getFullPathName() < right.getFullPathName() : folded < 0;
    });
    return matches;
}
}
