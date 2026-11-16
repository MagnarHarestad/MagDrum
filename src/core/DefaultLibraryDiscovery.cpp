#include "DefaultLibraryDiscovery.h"
#include <algorithm>
#include <unordered_set>

namespace magdrum::library
{
std::vector<juce::File> findDefaultLibraryFolders()
{
    auto commonDocuments = juce::File::getSpecialLocation(juce::File::commonDocumentsDirectory);
#if ! JUCE_WINDOWS && ! JUCE_MAC
    // JUCE maps this location to /opt on Linux, which is not a sample-library root.
    commonDocuments = juce::File();
#endif
    return findDefaultLibraryFolders({
        commonDocuments,
        juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory),
        juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory)
    });
}

std::vector<juce::File> findDefaultLibraryFolders(const DefaultLibraryRoots& roots)
{
    const auto& commonDocuments = roots.commonDocuments;
    const auto& commonData = roots.commonData;
    const auto& userDocuments = roots.userDocuments;
    const auto& userMusic = roots.userMusic;
    const auto& userApplicationData = roots.userApplicationData;
    const auto& programFiles = roots.programFiles;

    const std::vector<juce::File> candidates{
        // Native Access installs NI content directly under the shared Public Documents
        // directory by default, not only beneath a Native Instruments subfolder.
        commonDocuments,
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
        // Legacy/user-scoped Steinberg content is registered under Roaming AppData.
        userApplicationData.getChildFile("Steinberg/Content"),
        programFiles.getChildFile("Steinberg/Cubase 15/VST Sound"),
        programFiles.getChildFile("Steinberg/Cubase 15/Media")
    };

    std::vector<juce::File> found;
    std::unordered_set<std::string> uniquePaths;
    for (const auto& folder : candidates)
    {
        if (!folder.isDirectory())
            continue;
        const auto foldedPath = folder.getFullPathName().toLowerCase().toStdString();
        if (!uniquePaths.insert(foldedPath).second)
            continue;

        // A parent default content root already covers its known vendor folders.
        // Returning both would recursively scan the same samples multiple times.
        if (std::any_of(found.begin(), found.end(), [&folder](const juce::File& parent)
            { return folder.isAChildOf(parent); }))
            continue;

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
