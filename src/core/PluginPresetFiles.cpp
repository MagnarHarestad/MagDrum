#include "PluginPresetFiles.h"
#include <algorithm>
#include <unordered_set>
#include <vector>

namespace
{
juce::String safePathSegment(juce::String value)
{
    constexpr char forbidden[] = "*?/:<>|\\";
    for (const auto* character = forbidden; *character != '\0'; ++character)
        value = value.replaceCharacter(*character, '_');
    return value.trim();
}

juce::String pathIdentity(const juce::File& file)
{
    auto path = file.getFullPathName().replaceCharacter('\\', '/');
#if JUCE_WINDOWS
    path = path.toLowerCase();
#endif
    return path;
}

juce::File applicationFolderFor(const juce::File& executableFile)
{
    auto folder = executableFile.getParentDirectory();
#if JUCE_MAC
    for (auto candidate = folder; candidate != juce::File();)
    {
        if (candidate.getFileName().endsWithIgnoreCase(".app"))
            return candidate;
        const auto parent = candidate.getParentDirectory();
        if (parent == candidate)
            break;
        candidate = parent;
    }
#endif
    return folder;
}

void addUniquePath(juce::Array<juce::File>& paths, const juce::File& file)
{
    if (file == juce::File())
        return;
    const auto normalized = pathIdentity(file);
    for (const auto& existing : paths)
        if (pathIdentity(existing) == normalized)
            return;
    paths.add(file);
}
}

juce::Array<juce::File> getStandardVst3PresetSearchRoots(const juce::String& company,
                                                         const juce::String& pluginName,
                                                         const juce::File& executableFile)
{
    juce::Array<juce::File> roots;
    const auto companyFolder = safePathSegment(company);
    const auto pluginFolder = safePathSegment(pluginName);
    if (companyFolder.isEmpty() || pluginFolder.isEmpty())
        return roots;

    const auto appendPresetPath = [&roots, &companyFolder, &pluginFolder](const juce::File& base)
    {
        addUniquePath(roots, base.getChildFile(companyFolder).getChildFile(pluginFolder));
    };

#if JUCE_WINDOWS
    appendPresetPath(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VST3 Presets"));
    appendPresetPath(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("VST3 Presets"));
    appendPresetPath(juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory).getChildFile("VST3 Presets"));
    appendPresetPath(applicationFolderFor(executableFile).getChildFile("VST3 Presets"));
#elif JUCE_MAC
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    appendPresetPath(home.getChildFile("Library/Audio/Presets"));
    appendPresetPath(juce::File("/Library/Audio/Presets"));
    appendPresetPath(juce::File("/Network/Library/Audio/Presets"));
    appendPresetPath(applicationFolderFor(executableFile).getChildFile("VST3 Presets"));
#elif JUCE_LINUX
    const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    appendPresetPath(home.getChildFile(".vst3/presets"));
    appendPresetPath(juce::File("/usr/share/vst3/presets"));
    appendPresetPath(juce::File("/usr/local/share/vst3/presets"));
    appendPresetPath(applicationFolderFor(executableFile).getChildFile("vst3/presets"));
#endif
    return roots;
}

PluginPresetSearchResult findVst3PresetFiles(const juce::File& root,
                                             const std::function<bool()>& shouldCancel)
{
    juce::Array<juce::File> roots;
    roots.add(root);
    return findVst3PresetFiles(roots, shouldCancel);
}

PluginPresetSearchResult findVst3PresetFiles(const juce::Array<juce::File>& roots,
                                             const std::function<bool()>& shouldCancel)
{
    PluginPresetSearchResult result;
    std::vector<juce::File> files;
    std::unordered_set<std::string> seenFiles;
    for (const auto& root : roots)
    {
        if (!root.isDirectory())
            continue;
        result.rootIsDirectory = true;
        std::vector<juce::File> filesInRoot;

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
            if (!file.getFileExtension().equalsIgnoreCase(".vstpreset"))
                continue;
            if (seenFiles.insert(pathIdentity(file).toStdString()).second)
                filesInRoot.push_back(file);
        }

        std::sort(filesInRoot.begin(), filesInRoot.end(), [](const auto& left, const auto& right)
        {
            return left.getFullPathName().compareIgnoreCase(right.getFullPathName()) < 0;
        });
        files.insert(files.end(), filesInRoot.begin(), filesInRoot.end());
    }

    for (const auto& file : files)
        result.files.add(file);
    return result;
}
