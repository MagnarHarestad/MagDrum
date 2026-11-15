#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace magdrum::library
{
struct DefaultLibraryRoots
{
    juce::File commonDocuments;
    juce::File commonData;
    juce::File userDocuments;
    juce::File userMusic;
    juce::File programFiles;
};

std::vector<juce::File> findDefaultLibraryFolders();
std::vector<juce::File> findDefaultLibraryFolders(const DefaultLibraryRoots& roots);
std::vector<juce::File> findDrumMidiPatternFiles(const std::vector<juce::File>& roots,
                                                 size_t maximumResults = 200,
                                                 int maximumVisitedFiles = 500000,
                                                 const std::function<bool()>& shouldCancel = {});
}
