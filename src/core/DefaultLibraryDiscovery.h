#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace magdrum::library
{
std::vector<juce::File> findDefaultLibraryFolders();
std::vector<juce::File> findDrumMidiPatternFiles(const std::vector<juce::File>& roots,
                                                 size_t maximumResults = 200,
                                                 int maximumVisitedFiles = 500000,
                                                 const std::function<bool()>& shouldCancel = {});
}
