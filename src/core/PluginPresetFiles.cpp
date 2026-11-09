#include "PluginPresetFiles.h"
#include <algorithm>
#include <vector>

PluginPresetSearchResult findVst3PresetFiles(const juce::File& root,
                                             const std::function<bool()>& shouldCancel)
{
    PluginPresetSearchResult result;
    result.rootIsDirectory = root.isDirectory();
    if (!result.rootIsDirectory)
        return result;

    std::vector<juce::File> files;
    for (juce::RangedDirectoryIterator iterator(root, true, "*", juce::File::findFiles,
                                                 juce::File::FollowSymlinks::noCycles), end;
         iterator != end; ++iterator)
    {
        if (shouldCancel && shouldCancel())
        {
            result.cancelled = true;
            return result;
        }

        const auto file = iterator->getFile();
        if (file.getFileExtension().equalsIgnoreCase(".vstpreset"))
            files.push_back(file);
    }

    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
    {
        return left.getFullPathName().compareIgnoreCase(right.getFullPathName()) < 0;
    });
    for (const auto& file : files)
        result.files.add(file);
    return result;
}
