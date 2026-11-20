#include "PluginScanProcess.h"
#include "PluginNoteScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace
{
juce::File vst3BundleRootForPluginPath(const juce::File& pluginPath)
{
    if (pluginPath.isDirectory() && pluginPath.getFileExtension().equalsIgnoreCase(".vst3"))
        return pluginPath;
    for (auto parent = pluginPath.getParentDirectory(); parent != juce::File{};)
    {
        if (parent.isDirectory() && parent.getFileExtension().equalsIgnoreCase(".vst3"))
            return parent;
        const auto nextParent = parent.getParentDirectory();
        if (nextParent == parent)
            break;
        parent = nextParent;
    }
    return {};
}

juce::File vst3ModuleManifestFile(const juce::File& bundle)
{
    const auto resourcesManifest = bundle.getChildFile("Contents").getChildFile("Resources")
        .getChildFile("moduleinfo.json");
    if (resourcesManifest.existsAsFile()) return resourcesManifest;
    return bundle.getChildFile("Contents").getChildFile("moduleinfo.json");
}

juce::File vst3ModuleBinaryFile(const juce::File& bundle)
{
    if (bundle.existsAsFile()) return bundle;
    const auto binary = bundle.getChildFile("Contents").getChildFile("x86_64-win")
        .getChildFile(bundle.getFileName());
    return binary.existsAsFile() ? binary : juce::File{};
}
}

juce::FileSearchPath magdrum::plugins::createVst3DiscoverySearchPath(
    const juce::FileSearchPath& defaultLocations, const juce::File& additionalFolder)
{
    auto result = defaultLocations;
    if (additionalFolder.isDirectory())
        result.addIfNotAlreadyThere(additionalFolder);
    return result;
}

magdrum::plugins::Vst3SearchDirectoryCache magdrum::plugins::loadVst3SearchDirectoryCache(
    const juce::File& cacheFile)
{
    Vst3SearchDirectoryCache cache;
    if (!cacheFile.existsAsFile()) return cache;
    const auto xml = juce::XmlDocument::parse(cacheFile);
    if (xml == nullptr || xml->getTagName() != "MAGDRUM_VST3_DIRECTORY_CACHE"
        || xml->getIntAttribute("version", 0) != 1)
        return cache;

    for (auto* directory = xml->getFirstChildElement(); directory != nullptr;
         directory = directory->getNextElement())
    {
        if (directory->getTagName() != "DIRECTORY") continue;
        const auto path = directory->getStringAttribute("path");
        const auto modified = directory->getStringAttribute("modifiedMs", "-1").getLargeIntValue();
        if (path.isEmpty() || modified < 0) continue;
        Vst3SearchDirectoryCacheEntry entry;
        entry.modifiedMilliseconds = modified;
        for (auto* child = directory->getFirstChildElement(); child != nullptr;
             child = child->getNextElement())
        {
            const auto childPath = child->getStringAttribute("path");
            if (childPath.isEmpty()) continue;
            if (child->getTagName() == "CHILD") entry.childDirectories.push_back(childPath);
            else if (child->getTagName() == "MODULE") entry.moduleBundles.push_back(childPath);
        }
        cache[path.toLowerCase().toStdString()] = std::move(entry);
    }
    return cache;
}

bool magdrum::plugins::saveVst3SearchDirectoryCache(const juce::File& cacheFile,
    const Vst3SearchDirectoryCache& cache)
{
    juce::XmlElement root("MAGDRUM_VST3_DIRECTORY_CACHE");
    root.setAttribute("version", 1);
    std::vector<std::pair<std::string, const Vst3SearchDirectoryCacheEntry*>> ordered;
    ordered.reserve(cache.size());
    for (const auto& [path, entry] : cache) ordered.emplace_back(path, &entry);
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    { return left.first < right.first; });
    for (const auto& [path, entry] : ordered)
    {
        auto* directory = root.createNewChildElement("DIRECTORY");
        directory->setAttribute("path", juce::String::fromUTF8(path.c_str()));
        directory->setAttribute("modifiedMs", juce::String(entry->modifiedMilliseconds));
        for (const auto& childPath : entry->childDirectories)
            directory->createNewChildElement("CHILD")->setAttribute("path", childPath);
        for (const auto& modulePath : entry->moduleBundles)
            directory->createNewChildElement("MODULE")->setAttribute("path", modulePath);
    }
    return cacheFile.replaceWithText(root.toString());
}

magdrum::plugins::Vst3ModuleSearchResult magdrum::plugins::enumerateVst3Modules(
    const juce::FileSearchPath& searchPath, Vst3SearchDirectoryCache& cache)
{
    Vst3ModuleSearchResult result;
    std::unordered_set<std::string> visitedDirectories;
    std::unordered_set<std::string> seenModules;
    const auto addModule = [&result, &seenModules](const juce::String& path)
    {
        const auto key = path.toLowerCase().toStdString();
        if (seenModules.insert(key).second) result.modules.add(path);
    };
    std::function<void(juce::File)> visitDirectory;
    visitDirectory = [&](juce::File directory)
    {
        if (directory.isSymbolicLink())
        {
            const auto target = directory.getLinkedTarget();
            if (target.isDirectory()) directory = target;
        }
        if (!directory.isDirectory()) return;
        const auto path = directory.getFullPathName();
        const auto key = path.toLowerCase().toStdString();
        if (!visitedDirectories.insert(key).second) return;
        ++result.directoriesVisited;

        auto found = cache.find(key);
        const auto modified = directory.getLastModificationTime().toMilliseconds();
        bool cacheValid = found != cache.end() && found->second.modifiedMilliseconds == modified;
        if (cacheValid)
        {
            cacheValid = std::all_of(found->second.childDirectories.begin(),
                found->second.childDirectories.end(), [](const auto& childPath)
                { return juce::File(childPath).isDirectory(); })
                && std::all_of(found->second.moduleBundles.begin(), found->second.moduleBundles.end(),
                [](const auto& modulePath) { return juce::File(modulePath).exists(); });
        }

        if (!cacheValid)
        {
            Vst3SearchDirectoryCacheEntry entry;
            entry.modifiedMilliseconds = modified;
            ++result.directoriesEnumerated;
            for (const auto& item : juce::RangedDirectoryIterator(directory, false, "*",
                    juce::File::findFilesAndDirectories, juce::File::FollowSymlinks::yes))
            {
                const auto file = item.getFile();
                if (file.hasFileExtension(".vst3"))
                    entry.moduleBundles.push_back(file.getFullPathName());
                else if (file.isDirectory())
                    entry.childDirectories.push_back(file.getFullPathName());
            }
            found = cache.insert_or_assign(key, std::move(entry)).first;
        }
        else
        {
            ++result.directoriesReused;
        }

        const auto entry = found->second;
        for (const auto& modulePath : entry.moduleBundles)
            addModule(modulePath);
        for (const auto& childPath : entry.childDirectories)
            visitDirectory(juce::File(childPath));
    };

    for (int index = 0; index < searchPath.getNumPaths(); ++index)
        visitDirectory(searchPath[index]);

    for (auto entry = cache.begin(); entry != cache.end();)
        if (visitedDirectories.count(entry->first) == 0) entry = cache.erase(entry);
        else ++entry;
    return result;
}

bool magdrum::plugins::vst3ModuleMayBeInstrument(const juce::File& bundle)
{
    const auto metadataFile = vst3ModuleManifestFile(bundle);
    if (!metadataFile.existsAsFile())
    {
        const auto moduleName = bundle.getFileNameWithoutExtension();
        static constexpr const char* effectNameCues[]{"compressor", "imager", "equalizer", "equaliser",
            "limiter", "reverb", "delay", "dynamics", "de-esser", "deesser", "meter", "analyzer",
            "analyser", "maximizer", "maximiser", "exciter", "distortion", "phaser", "flanger", "chorus"};
        for (const auto* cue : effectNameCues)
            if (moduleName.containsWholeWordIgnoreCase(cue)) return false;
        return true;
    }

    const auto parsed = juce::JSON::parse(metadataFile.loadFileAsString());
    const auto* root = parsed.getDynamicObject();
    if (root == nullptr) return true;
    const auto* classes = root->getProperty("Classes").getArray();
    if (classes == nullptr) return true;

    bool foundAudioClass = false;
    for (const auto& classInfo : *classes)
    {
        const auto* details = classInfo.getDynamicObject();
        if (details == nullptr
            || details->getProperty("Category").toString() != "Audio Module Class")
            continue;

        foundAudioClass = true;
        const auto* categories = details->getProperty("Sub Categories").getArray();
        if (categories == nullptr) continue;
        for (const auto& category : *categories)
            if (category.toString().equalsIgnoreCase("Instrument")) return true;
    }
    return !foundAudioClass;
}

magdrum::plugins::Vst3ModuleInstrumentCache magdrum::plugins::loadVst3ModuleInstrumentCache(
    const juce::File& cacheFile)
{
    Vst3ModuleInstrumentCache cache;
    if (!cacheFile.existsAsFile()) return cache;
    const auto xml = juce::XmlDocument::parse(cacheFile);
    if (xml == nullptr || xml->getTagName() != "MAGDRUM_VST3_MODULE_FILTER_CACHE"
        || (xml->getIntAttribute("version", 0) != 1 && xml->getIntAttribute("version", 0) != 2))
        return cache;

    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->getTagName() != "MODULE") continue;
        const auto modulePath = child->getStringAttribute("path");
        const auto metadataPath = child->getStringAttribute("metadataPath");
        if (modulePath.isEmpty()) continue;
        Vst3ModuleInstrumentCacheEntry entry;
        entry.metadataPath = metadataPath;
        entry.metadataSize = child->getStringAttribute("metadataSize", "-1").getLargeIntValue();
        entry.metadataModifiedMilliseconds = child->getStringAttribute("metadataModifiedMs", "-1").getLargeIntValue();
        entry.binaryPath = child->getStringAttribute("binaryPath");
        entry.binarySize = child->getStringAttribute("binarySize", "-1").getLargeIntValue();
        entry.binaryModifiedMilliseconds = child->getStringAttribute("binaryModifiedMs", "-1").getLargeIntValue();
        entry.mayBeInstrument = child->getBoolAttribute("mayBeInstrument", true);
        const bool hasManifestCache = entry.metadataPath.isNotEmpty() && entry.metadataSize >= 0
            && entry.metadataModifiedMilliseconds >= 0;
        const bool hasBinaryCache = entry.binaryPath.isNotEmpty() && entry.binarySize >= 0
            && entry.binaryModifiedMilliseconds >= 0;
        if (!hasManifestCache && !hasBinaryCache) continue;
        cache[vst3ModuleCacheKey(modulePath).toStdString()] = std::move(entry);
    }
    return cache;
}

std::optional<bool> magdrum::plugins::cachedVst3ModuleMayBeInstrument(
    const Vst3ModuleInstrumentCache& cache, const juce::File& bundle)
{
    const auto manifest = vst3ModuleManifestFile(bundle);
    const auto found = cache.find(vst3ModuleCacheKey(bundle.getFullPathName()).toStdString());
    if (found == cache.end()) return {};
    const auto& entry = found->second;
    if (manifest.existsAsFile())
    {
        if (vst3ModuleCacheKey(entry.metadataPath) != vst3ModuleCacheKey(manifest.getFullPathName())
            || entry.metadataSize != manifest.getSize()
            || entry.metadataModifiedMilliseconds != manifest.getLastModificationTime().toMilliseconds())
            return {};
    }
    else
    {
        const auto binary = vst3ModuleBinaryFile(bundle);
        if (vst3ModuleCacheKey(entry.binaryPath) != vst3ModuleCacheKey(binary.getFullPathName())
            || !binary.existsAsFile()
            || entry.binarySize != binary.getSize()
            || entry.binaryModifiedMilliseconds != binary.getLastModificationTime().toMilliseconds())
            return {};
    }
    return entry.mayBeInstrument;
}

void magdrum::plugins::rememberVst3ModuleInstrumentStatus(
    Vst3ModuleInstrumentCache& cache, const juce::File& bundle, bool mayBeInstrument)
{
    const auto manifest = vst3ModuleManifestFile(bundle);
    Vst3ModuleInstrumentCacheEntry entry;
    if (manifest.existsAsFile())
    {
        entry.metadataPath = vst3ModuleCacheKey(manifest.getFullPathName());
        entry.metadataSize = manifest.getSize();
        entry.metadataModifiedMilliseconds = manifest.getLastModificationTime().toMilliseconds();
    }
    else
    {
        const auto binary = vst3ModuleBinaryFile(bundle);
        if (!binary.existsAsFile()) return;
        entry.binaryPath = vst3ModuleCacheKey(binary.getFullPathName());
        entry.binarySize = binary.getSize();
        entry.binaryModifiedMilliseconds = binary.getLastModificationTime().toMilliseconds();
    }
    entry.mayBeInstrument = mayBeInstrument;
    cache[vst3ModuleCacheKey(bundle.getFullPathName()).toStdString()] = std::move(entry);
}

magdrum::plugins::Vst3ModuleDiscoveryAction magdrum::plugins::classifyVst3ModuleForDiscovery(
    Vst3ModuleInstrumentCache& cache, const juce::File& bundle, bool cachedBinaryIsUpToDate)
{
    const auto cachedClassification = cachedVst3ModuleMayBeInstrument(cache, bundle);
    const bool manifestWasCached = cachedClassification.has_value();
    const bool mayBeInstrument = manifestWasCached
        ? *cachedClassification : vst3ModuleMayBeInstrument(bundle);
    if (!manifestWasCached)
        rememberVst3ModuleInstrumentStatus(cache, bundle, mayBeInstrument);
    if (!mayBeInstrument)
        return Vst3ModuleDiscoveryAction::ExcludeEffect;
    if (manifestWasCached && cachedBinaryIsUpToDate)
        return Vst3ModuleDiscoveryAction::Skip;
    return Vst3ModuleDiscoveryAction::ScanInstrument;
}

bool magdrum::plugins::saveVst3ModuleInstrumentCache(
    const juce::File& cacheFile, const Vst3ModuleInstrumentCache& cache)
{
    juce::XmlElement root("MAGDRUM_VST3_MODULE_FILTER_CACHE");
    root.setAttribute("version", 2);
    std::vector<std::pair<std::string, const Vst3ModuleInstrumentCacheEntry*>> ordered;
    ordered.reserve(cache.size());
    for (const auto& [path, entry] : cache) ordered.emplace_back(path, &entry);
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
    { return left.first < right.first; });
    for (const auto& [path, entry] : ordered)
    {
        auto* module = root.createNewChildElement("MODULE");
        module->setAttribute("path", juce::String::fromUTF8(path.c_str()));
        module->setAttribute("metadataPath", entry->metadataPath);
        module->setAttribute("metadataSize", juce::String(entry->metadataSize));
        module->setAttribute("metadataModifiedMs", juce::String(entry->metadataModifiedMilliseconds));
        module->setAttribute("binaryPath", entry->binaryPath);
        module->setAttribute("binarySize", juce::String(entry->binarySize));
        module->setAttribute("binaryModifiedMs", juce::String(entry->binaryModifiedMilliseconds));
        module->setAttribute("mayBeInstrument", entry->mayBeInstrument);
    }
    return cacheFile.replaceWithText(root.toString());
}

bool magdrum::plugins::readVst3DiscoveryProgress(const juce::File& progressFile,
    Vst3DiscoveryProgress& progress)
{
    progress = {};
    if (!progressFile.existsAsFile())
        return false;
    const auto parsed = juce::JSON::parse(progressFile.loadFileAsString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return false;

    progress.completed = juce::jmax(0, static_cast<int>(object->getProperty("completed")));
    progress.total = juce::jmax(progress.completed, static_cast<int>(object->getProperty("total")));
    progress.current = object->getProperty("current").toString();
    progress.currentPath = object->getProperty("currentPath").toString();
    if (const auto* failed = object->getProperty("failedModules").getArray())
        for (const auto& value : *failed)
        {
            const auto path = value.toString().trim();
            if (path.isNotEmpty() && !progress.failedModules.contains(path))
                progress.failedModules.add(path);
        }
    return true;
}

bool magdrum::plugins::writeVst3DiscoveryProgress(const juce::File& progressFile,
    const Vst3DiscoveryProgress& progress)
{
    if (progressFile == juce::File{})
        return false;
    auto* object = new juce::DynamicObject();
    object->setProperty("completed", juce::jmax(0, progress.completed));
    object->setProperty("total", juce::jmax(progress.completed, progress.total));
    object->setProperty("current", progress.current);
    object->setProperty("currentPath", progress.currentPath);
    juce::Array<juce::var> failed;
    for (const auto& path : progress.failedModules)
        failed.add(path);
    object->setProperty("failedModules", juce::var(std::move(failed)));
    return progressFile.replaceWithText(juce::JSON::toString(juce::var(object)));
}

bool magdrum::plugins::recordVst3DiscoveryModuleFailure(const juce::File& progressFile,
    const juce::String& modulePath)
{
    Vst3DiscoveryProgress progress;
    if (modulePath.trim().isEmpty() || !readVst3DiscoveryProgress(progressFile, progress)
        || progress.currentPath.isEmpty() || !progress.currentPath.equalsIgnoreCase(modulePath)
        || progress.failedModules.contains(modulePath))
        return false;

    progress.failedModules.add(modulePath);
    progress.completed = juce::jmin(progress.total, progress.completed + 1);
    progress.current = "skipped";
    progress.currentPath.clear();
    return writeVst3DiscoveryProgress(progressFile, progress);
}

magdrum::plugins::Vst3DiscoveryExecution magdrum::plugins::monitorVst3Discovery(
    const juce::StringArray& command, const juce::File& progressFile,
    int perModuleTimeoutMilliseconds, std::function<bool()> shouldCancel,
    int cancellationGraceMilliseconds)
{
    Vst3DiscoveryExecution result;
    if (command.isEmpty() || perModuleTimeoutMilliseconds <= 0 || cancellationGraceMilliseconds < 0)
        return result;

    juce::ChildProcess child;
    result.started = child.start(command, 0);
    if (!result.started)
        return result;

    juce::String activeModulePath;
    juce::uint32 moduleStartedAt = 0;
    while (child.isRunning())
    {
        if (shouldCancel && shouldCancel())
        {
            result.cancelled = true;
            child.kill();
            child.waitForProcessToFinish(cancellationGraceMilliseconds);
            break;
        }

        Vst3DiscoveryProgress progress;
        if (readVst3DiscoveryProgress(progressFile, progress))
        {
            if (progress.currentPath != activeModulePath)
            {
                activeModulePath = progress.currentPath;
                moduleStartedAt = activeModulePath.isNotEmpty()
                    ? juce::Time::getMillisecondCounter() : 0;
            }

            if (activeModulePath.isNotEmpty()
                && static_cast<juce::uint32>(juce::Time::getMillisecondCounter() - moduleStartedAt)
                    >= static_cast<juce::uint32>(perModuleTimeoutMilliseconds))
            {
                result.timedOut = true;
                result.timedOutModulePath = activeModulePath;
                child.kill();
                child.waitForProcessToFinish(cancellationGraceMilliseconds);
                break;
            }
        }
        child.waitForProcessToFinish(100);
    }

    if (!result.cancelled && !result.timedOut)
        result.exitCode = child.getExitCode();
    return result;
}

juce::String magdrum::plugins::vst3DiscoveryModuleForRecovery(
    const Vst3DiscoveryExecution& execution, const juce::File& progressFile)
{
    if (! execution.started || execution.cancelled || (! execution.timedOut && execution.exitCode == 0))
        return {};
    if (execution.timedOut)
        return execution.timedOutModulePath;

    Vst3DiscoveryProgress progress;
    return readVst3DiscoveryProgress(progressFile, progress) ? progress.currentPath : juce::String();
}

bool magdrum::plugins::vst3ModuleHasInstrumentDescription(
    const juce::KnownPluginList& knownPlugins, const juce::File& module)
{
    // getTypes() returns an Array by value. Keep its storage alive while the
    // iterator pair is consumed, especially in the MSVC Debug STL.
    const auto descriptions = knownPlugins.getTypes();
    return vst3ModuleHasInstrumentDescription(descriptions, module);
}

bool magdrum::plugins::vst3ModuleHasInstrumentDescription(
    const juce::Array<juce::PluginDescription>& descriptions, const juce::File& module)
{
    return std::any_of(descriptions.begin(), descriptions.end(), [&module](const auto& description)
    {
        const juce::File descriptionPath(description.fileOrIdentifier);
        return description.pluginFormatName == "VST3" && description.isInstrument
            && (descriptionPath == module || descriptionPath.isAChildOf(module));
    });
}

magdrum::plugins::CachedVst3ModuleIndex magdrum::plugins::indexCachedVst3Modules(
    const juce::Array<juce::PluginDescription>& cachedDescriptions)
{
    CachedVst3ModuleIndex decisions;
    decisions.reserve(static_cast<size_t>(cachedDescriptions.size()));
    struct ModulePaths
    {
        juce::String pluginPath;
        juce::String bundlePath;
        bool exists = false;
    };
    std::unordered_map<std::string, ModulePaths> pathsByDescription;
    pathsByDescription.reserve(static_cast<size_t>(cachedDescriptions.size()));
    for (const auto& description : cachedDescriptions)
    {
        if (description.pluginFormatName != "VST3")
            continue;

        const auto descriptionPathKey = vst3ModuleCacheKey(description.fileOrIdentifier).toStdString();
        auto [paths, inserted] = pathsByDescription.try_emplace(descriptionPathKey);
        if (inserted)
        {
            const juce::File pluginPath(description.fileOrIdentifier);
            paths->second.pluginPath = pluginPath.getFullPathName();
            paths->second.exists = pluginPath.exists();
            if (paths->second.exists)
                paths->second.bundlePath = vst3BundleRootForPluginPath(pluginPath).getFullPathName();
        }
        const auto cacheDescriptionFor = [&decisions, &description, &paths](const std::string& key)
        {
            if (key.empty()) return;
            auto& module = decisions[key];
            if (!module.hasFileModTime)
            {
                module.fileModTime = description.lastFileModTime;
                module.filePath = paths->second.pluginPath;
                module.hasFileModTime = true;
            }
            else
            {
                if (module.fileModTime != description.lastFileModTime)
                    module.cachedModTimesAgree = false;
                if (vst3ModuleCacheKey(module.filePath)
                    != vst3ModuleCacheKey(paths->second.pluginPath))
                    module.cachedFilePathsAgree = false;
            }
            module.hasPluginDescriptions = true;
            module.hasInstrumentDescription = module.hasInstrumentDescription || description.isInstrument;
        };
        cacheDescriptionFor(descriptionPathKey);
        if (paths->second.exists)
        {
            cacheDescriptionFor(vst3ModuleCacheKey(paths->second.pluginPath).toStdString());
            cacheDescriptionFor(vst3ModuleCacheKey(paths->second.bundlePath).toStdString());
        }
    }
    return decisions;
}

juce::String magdrum::plugins::vst3ModuleCacheKey(const juce::String& path)
{
    auto key = path.replaceCharacter('\\', '/');
#if JUCE_WINDOWS
    key = key.toLowerCase();
#endif
    return key;
}

bool magdrum::plugins::isCachedVst3ModuleUpToDate(const CachedVst3ModuleInfo& cached,
                                                   const juce::File& module)
{
    if (!cached.hasFileModTime || !cached.cachedModTimesAgree || !cached.cachedFilePathsAgree
        || !module.exists())
        return false;
    const juce::File cachedFile(cached.filePath);
    const bool pathMatchesModule = cachedFile == module
        || (module.isDirectory() && cachedFile.isAChildOf(module));
    return pathMatchesModule && cachedFile.exists()
        && cachedFile.getLastModificationTime() == cached.fileModTime;
}

namespace
{
juce::var readJson(const juce::File& file)
{
    return file.existsAsFile() ? juce::JSON::parse(file.loadFileAsString()) : juce::var();
}

bool writeJson(const juce::File& file, const juce::var& value)
{
    return file.replaceWithText(juce::JSON::toString(value, true));
}

bool writeCapture(const juce::File& file, const PluginNoteCapture& capture, double sampleRate)
{
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (stream == nullptr)
        return false;

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
        stream.get(), capture.sampleRate > 0.0 ? capture.sampleRate : sampleRate,
        static_cast<unsigned int>(capture.audio.getNumChannels()), 24, {}, 0));
    if (writer == nullptr)
        return false;

    stream.release();
    return writer->writeFromAudioSampleBuffer(capture.audio, 0, capture.audio.getNumSamples());
}

juce::var makeCaptureValue(const PluginNoteCapture& capture, const juce::String& fileName)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("midiNote", capture.midiNote);
    value->setProperty("velocity", capture.velocity);
    value->setProperty("takeIndex", capture.takeIndex);
    value->setProperty("channels", capture.audio.getNumChannels());
    value->setProperty("sampleCount", capture.audio.getNumSamples());
    value->setProperty("sampleRate", capture.sampleRate);
    value->setProperty("peak", capture.peak);
    value->setProperty("rms", capture.rms);
    value->setProperty("silent", capture.silent);
    value->setProperty("fileName", fileName);
    return juce::var(value);
}

bool parseCaptureValue(const juce::var& item, PluginScanProcessCapture& record)
{
    if (!item.isObject())
        return false;
    const auto* capture = item.getDynamicObject();
    record.midiNote = static_cast<int>(capture->getProperty("midiNote"));
    record.velocity = static_cast<int>(capture->getProperty("velocity"));
    record.takeIndex = static_cast<int>(capture->getProperty("takeIndex"));
    record.channels = static_cast<int>(capture->getProperty("channels"));
    record.sampleCount = static_cast<int>(capture->getProperty("sampleCount"));
    record.sampleRate = static_cast<double>(capture->getProperty("sampleRate"));
    record.peak = static_cast<double>(capture->getProperty("peak"));
    record.rms = static_cast<double>(capture->getProperty("rms"));
    record.silent = static_cast<bool>(capture->getProperty("silent"));
    record.fileName = capture->getProperty("fileName").toString();
    const auto expectedFileName = "note_" + juce::String(record.midiNote).paddedLeft('0', 3)
        + "_velocity_" + juce::String(record.velocity).paddedLeft('0', 3)
        + "_take_" + juce::String(record.takeIndex).paddedLeft('0', 3) + ".wav";
    return record.midiNote >= 0 && record.midiNote <= 127
        && record.velocity >= 1 && record.velocity <= 127
        && record.takeIndex >= 1 && record.channels >= 1 && record.channels <= 8 && record.sampleCount >= 1
        && std::isfinite(record.sampleRate) && (record.sampleRate == 0.0 || (record.sampleRate >= 8000.0 && record.sampleRate <= 192000.0))
        && std::isfinite(record.peak) && record.peak >= 0.0 && std::isfinite(record.rms) && record.rms >= 0.0
        && (record.silent ? record.fileName.isEmpty() : record.fileName == expectedFileName);
}

juce::var makeErrorManifest(const juce::String& error, juce::Array<juce::var> captures = {})
{
    auto* value = new juce::DynamicObject();
    value->setProperty("ok", false);
    value->setProperty("error", error);
    value->setProperty("cancelled", false);
    value->setProperty("captures", captures);
    return juce::var(value);
}
}

bool PluginScanProcess::readManifest(const juce::File& file, PluginScanProcessManifest& result)
{
    result = {};
    const auto value = readJson(file);
    if (!value.isObject())
        return false;

    const auto* object = value.getDynamicObject();
    result.succeeded = static_cast<bool>(object->getProperty("ok"));
    result.error = object->getProperty("error").toString();
    result.cancelled = static_cast<bool>(object->getProperty("cancelled"));
    const auto capturesValue = object->getProperty("captures");
    const auto* captures = capturesValue.getArray();
    if (captures == nullptr)
        return false;

    result.captures.reserve(static_cast<size_t>(captures->size()));
    for (const auto& item : *captures)
    {
        PluginScanProcessCapture record;
        if (!parseCaptureValue(item, record))
            return false;
        result.captures.push_back(std::move(record));
    }
    return true;
}

bool PluginScanProcess::readCaptureLog(const juce::File& file, PluginScanProcessManifest& result)
{
    result = {};
    if (!file.existsAsFile())
        return false;
    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    for (int index = 0; index < lines.size(); ++index)
    {
        if (lines[index].trim().isEmpty())
            continue;
        const auto value = juce::JSON::parse(lines[index]);
        if (!value.isObject())
        {
            bool hasLaterData = false;
            for (int later = index + 1; later < lines.size(); ++later)
                hasLaterData = hasLaterData || lines[later].trim().isNotEmpty();
            if (!hasLaterData)
                break;
            result = {};
            return false;
        }
        PluginScanProcessCapture capture;
        if (!parseCaptureValue(value, capture))
        {
            result = {};
            return false;
        }
        result.captures.push_back(std::move(capture));
    }
    if (result.captures.empty())
        return false;
    result.partial = true;
    return true;
}

PluginScanProcessExecution PluginScanProcess::monitorWorker(const juce::StringArray& command,
    const juce::File& progressFile, const juce::File& resultFile, const juce::File& cancellationFile,
    ShouldCancel shouldCancel, Progress progress, int cancellationGraceMilliseconds,
    const juce::File& captureLogFile)
{
    PluginScanProcessExecution result;
    if (command.isEmpty() || cancellationGraceMilliseconds < 0)
        return result;

    juce::ChildProcess child;
    result.started = child.start(command);
    if (!result.started)
        return result;

    bool cancellationObserved = false;
    juce::uint32 cancellationStartedAt = 0;
    int lastReportedProgress = -1;
    juce::String lastReportedPhase;
    while (child.isRunning())
    {
        if (shouldCancel && shouldCancel())
        {
            if (!cancellationFile.existsAsFile()) cancellationFile.create();
            if (!cancellationObserved)
            {
                cancellationObserved = true;
                cancellationStartedAt = juce::Time::getMillisecondCounter();
            }
            else if (static_cast<juce::uint32>(juce::Time::getMillisecondCounter() - cancellationStartedAt)
                     > static_cast<juce::uint32>(cancellationGraceMilliseconds))
            {
                child.kill();
                result.terminatedForCancellation = true;
                break;
            }
        }

        const auto progressValue = progressFile.existsAsFile() ? juce::JSON::parse(progressFile.loadFileAsString()) : juce::var();
        if (progress && progressValue.isObject())
        {
            const auto* progressObject = progressValue.getDynamicObject();
            const auto phase = progressObject->getProperty("phase").toString();
            const int completed = static_cast<int>(progressObject->getProperty("completed"));
            const int total = static_cast<int>(progressObject->getProperty("total"));
            const bool phaseChanged = phase.isNotEmpty() && phase != lastReportedPhase;
            if (phaseChanged)
            {
                lastReportedPhase = phase;
                lastReportedProgress = completed;
                progress(phase, completed, total, static_cast<int>(progressObject->getProperty("midiNote")),
                         static_cast<int>(progressObject->getProperty("velocity")));
            }
            else if (completed > lastReportedProgress && (completed % 24 == 0 || completed == total))
            {
                lastReportedProgress = completed;
                progress(phase, completed, total, static_cast<int>(progressObject->getProperty("midiNote")),
                         static_cast<int>(progressObject->getProperty("velocity")));
            }
        }
        juce::Thread::sleep(50);
    }

    if (result.terminatedForCancellation)
    {
        result.manifestRead = readCaptureLog(captureLogFile, result.manifest);
        if (result.manifestRead)
        {
            result.manifest.cancelled = true;
        }
        return result;
    }
    result.exitCode = child.getExitCode();
    result.manifestRead = readManifest(resultFile, result.manifest);
    if (!result.manifestRead || !result.manifest.succeeded)
    {
        PluginScanProcessManifest recovered;
        if (readCaptureLog(captureLogFile, recovered)
            && (!result.manifestRead || recovered.captures.size() > result.manifest.captures.size()))
        {
            result.manifest = std::move(recovered);
            result.manifest.error = result.exitCode != 0
                ? "The instrument exited unexpectedly; completed captures were recovered."
                : "The scan ended before its final manifest was written; completed captures were recovered.";
            result.manifest.cancelled = false;
            result.manifestRead = true;
        }
    }
    return result;
}

PluginHelperExecution PluginScanProcess::monitorHelper(const juce::StringArray& command,
    const juce::File& progressFile, int timeoutMilliseconds, ShouldCancel shouldCancel,
    TextProgress progress, int cancellationGraceMilliseconds)
{
    PluginHelperExecution result;
    if (command.isEmpty() || timeoutMilliseconds < 0 || cancellationGraceMilliseconds < 0)
        return result;

    juce::ChildProcess child;
    result.started = child.start(command, 0);
    if (!result.started)
        return result;

    const auto startedAt = juce::Time::getMillisecondCounter();
    juce::String lastProgress;
    while (child.isRunning())
    {
        if (shouldCancel && shouldCancel())
        {
            result.cancelled = true;
            child.kill();
            child.waitForProcessToFinish(cancellationGraceMilliseconds);
            break;
        }

        if (timeoutMilliseconds > 0
            && static_cast<juce::uint32>(juce::Time::getMillisecondCounter() - startedAt)
                >= static_cast<juce::uint32>(timeoutMilliseconds))
        {
            result.timedOut = true;
            child.kill();
            child.waitForProcessToFinish(cancellationGraceMilliseconds);
            break;
        }

        if (progress && progressFile.existsAsFile())
        {
            const auto phase = progressFile.loadFileAsString().trim();
            if (phase.isNotEmpty() && phase != lastProgress)
            {
                lastProgress = phase;
                progress(phase);
            }
        }

        child.waitForProcessToFinish(50);
    }

    if (!result.cancelled && !result.timedOut)
        result.exitCode = child.getExitCode();
    return result;
}

bool PluginScanProcess::shouldPublishNoteScanProgress(int completed, int total)
{
    return completed > 0
        && (completed == 1 || completed % 24 == 0 || (total > 0 && completed >= total));
}

int PluginScanProcess::runWorker(const juce::File& requestFile)
{
    const auto requestValue = readJson(requestFile);
    if (!requestValue.isObject())
        return 2;

    const auto* request = requestValue.getDynamicObject();
    const auto descriptionFile = juce::File(request->getProperty("descriptionFile").toString());
    const auto stateFile = juce::File(request->getProperty("stateFile").toString());
    const auto outputDirectory = juce::File(request->getProperty("outputDirectory").toString());
    const auto cancellationFile = juce::File(request->getProperty("cancellationFile").toString());
    const auto progressFile = juce::File(request->getProperty("progressFile").toString());
    const auto captureLogFile = juce::File(request->getProperty("captureLogFile").toString());
    const auto resultFile = juce::File(request->getProperty("resultFile").toString());
    const double sampleRate = request->getProperty("sampleRate");
    const int blockSize = request->getProperty("blockSize");
    if (!descriptionFile.existsAsFile() || !stateFile.existsAsFile() || !outputDirectory.isDirectory()
        || captureLogFile.getParentDirectory() != requestFile.getParentDirectory()
        || !resultFile.getParentDirectory().isDirectory())
        return 3;

    const auto xml = juce::XmlDocument::parse(descriptionFile);
    juce::PluginDescription description;
    if (xml == nullptr || !description.loadFromXml(*xml))
    {
        writeJson(resultFile, makeErrorManifest("The plugin description could not be loaded in the scanner process."));
        return 4;
    }

    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    juce::String loadError;
    const auto writeProgress = [&progressFile](const juce::String& phase, int completed, int total,
                                                int midiNote, int velocity)
    {
        auto* progress = new juce::DynamicObject();
        progress->setProperty("phase", phase);
        progress->setProperty("completed", completed);
        progress->setProperty("total", total);
        progress->setProperty("midiNote", midiNote);
        progress->setProperty("velocity", velocity);
        writeJson(progressFile, juce::var(progress));
    };
    const auto writePhase = [&writeProgress](const juce::String& phase)
    {
        writeProgress(phase, 0, 0, 0, 0);
    };
    writePhase("loading");
    auto createdPlugin = juce::MessageManager::callSync([&formats, &description, sampleRate, blockSize, &loadError]
    {
        return formats.createPluginInstance(description, sampleRate, blockSize, loadError);
    });
    auto plugin = createdPlugin ? std::move(*createdPlugin) : nullptr;
    if (plugin == nullptr)
    {
        writeJson(resultFile, makeErrorManifest("Could not load the plugin in the isolated scanner process: " + loadError));
        return 5;
    }

    writePhase("restoring");
    juce::MemoryBlock state;
    if (!stateFile.loadFileAsData(state) || state.isEmpty() || state.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        writeJson(resultFile, makeErrorManifest("The saved plugin kit state could not be read."));
        return 6;
    }
    juce::MessageManager::callSync([&plugin, &state]
    {
        plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    });

    PluginNoteScanSettings settings;
    settings.sampleRate = sampleRate;
    settings.blockSize = blockSize;
    const auto firstMidiNote = request->getProperty("firstMidiNote");
    const auto lastMidiNote = request->getProperty("lastMidiNote");
    if (!firstMidiNote.isVoid()) settings.firstMidiNote = static_cast<int>(firstMidiNote);
    if (!lastMidiNote.isVoid()) settings.lastMidiNote = static_cast<int>(lastMidiNote);
    settings.repeatCount = request->getProperty("repeatCount");
    settings.noteDurationSeconds = request->getProperty("noteDurationSeconds");
    settings.releaseSeconds = request->getProperty("releaseSeconds");
    settings.captureLengthSeconds = request->getProperty("captureLengthSeconds");
    settings.renderMode = static_cast<bool>(request->getProperty("realtimePaced"))
        ? PluginNoteRenderMode::realtimePaced : PluginNoteRenderMode::offline;
    if (static_cast<bool>(request->getProperty("audioDeviceRealtime")))
        settings.renderMode = PluginNoteRenderMode::audioDevice;
    const auto velocitiesValue = request->getProperty("velocityLevels");
    const auto* velocities = velocitiesValue.getArray();
    if (velocities == nullptr)
    {
        writeJson(resultFile, makeErrorManifest("The velocity list is missing from the scanner request."));
        return 7;
    }
    settings.velocityLevels.clear();
    for (const auto& velocity : *velocities)
        settings.velocityLevels.push_back(static_cast<int>(velocity));

    if (settings.firstMidiNote < 0 || settings.firstMidiNote > 127
        || settings.lastMidiNote < settings.firstMidiNote || settings.lastMidiNote > 127)
    {
        writeJson(resultFile, makeErrorManifest("The MIDI note range must be between 0 and 127, with the first note no greater than the last."));
        return 9;
    }

    const auto totalScans = (settings.lastMidiNote - settings.firstMidiNote + 1)
        * static_cast<int>(settings.velocityLevels.size()) * settings.repeatCount;
    writeProgress("preparing", 0, totalScans, 0,
                  settings.velocityLevels.empty() ? 0 : settings.velocityLevels.front());
    std::unique_ptr<juce::FileOutputStream> captureLog(captureLogFile.createOutputStream());
    if (captureLog == nullptr || !captureLog->openedOk())
    {
        writeJson(resultFile, makeErrorManifest("The isolated scanner could not create its recoverable capture log."));
        return 8;
    }
    PluginNoteScanner scanner;
    juce::Array<juce::var> captures;
    auto scan = scanner.scan(*plugin, settings,
        [&cancellationFile] { return cancellationFile.existsAsFile(); },
        [&writeProgress](int completed, int total, int midiNote, int velocity)
        {
            if (PluginScanProcess::shouldPublishNoteScanProgress(completed, total))
                writeProgress("scanning", completed, total, midiNote, velocity);
        },
        [&](const PluginNoteCapture& capture)
        {
            juce::String fileName;
            if (!capture.silent)
            {
                fileName = "note_" + juce::String(capture.midiNote).paddedLeft('0', 3)
                    + "_velocity_" + juce::String(capture.velocity).paddedLeft('0', 3)
                    + "_take_" + juce::String(capture.takeIndex).paddedLeft('0', 3) + ".wav";
                if (!writeCapture(outputDirectory.getChildFile(fileName), capture, sampleRate))
                    return false;
            }
            const auto captureValue = makeCaptureValue(capture, fileName);
            if (!captureLog->writeText(juce::JSON::toString(captureValue, true), false, false, nullptr)
                || !captureLog->writeText("\n", false, false, nullptr))
                return false;
            captureLog->flush();
            captures.add(captureValue);
            return true;
        });

    if (scan.error.isNotEmpty())
    {
        writeJson(resultFile, makeErrorManifest(scan.error, captures));
        return 8;
    }

    captureLog.reset();

    auto* manifest = new juce::DynamicObject();
    manifest->setProperty("ok", true);
    manifest->setProperty("error", juce::String());
    manifest->setProperty("cancelled", scan.cancelled);
    manifest->setProperty("captures", captures);
    return writeJson(resultFile, juce::var(manifest)) ? 0 : 10;
}
