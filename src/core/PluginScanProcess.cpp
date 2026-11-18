#include "PluginScanProcess.h"
#include "PluginNoteScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
juce::File vst3ModuleManifestFile(const juce::File& bundle)
{
    const auto resourcesManifest = bundle.getChildFile("Contents").getChildFile("Resources")
        .getChildFile("moduleinfo.json");
    if (resourcesManifest.existsAsFile()) return resourcesManifest;
    return bundle.getChildFile("Contents").getChildFile("moduleinfo.json");
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

bool magdrum::plugins::vst3ModuleMayBeInstrument(const juce::File& bundle)
{
    const auto metadataFile = vst3ModuleManifestFile(bundle);
    if (!metadataFile.existsAsFile()) return true;

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
        || xml->getIntAttribute("version", 0) != 1)
        return cache;

    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
    {
        if (child->getTagName() != "MODULE") continue;
        const auto modulePath = child->getStringAttribute("path");
        const auto metadataPath = child->getStringAttribute("metadataPath");
        if (modulePath.isEmpty() || metadataPath.isEmpty()) continue;
        Vst3ModuleInstrumentCacheEntry entry;
        entry.metadataPath = metadataPath;
        entry.metadataSize = child->getStringAttribute("metadataSize", "-1").getLargeIntValue();
        entry.metadataModifiedMilliseconds = child->getStringAttribute("metadataModifiedMs", "-1").getLargeIntValue();
        entry.mayBeInstrument = child->getBoolAttribute("mayBeInstrument", true);
        if (entry.metadataSize < 0 || entry.metadataModifiedMilliseconds < 0) continue;
        cache[modulePath.toStdString()] = std::move(entry);
    }
    return cache;
}

std::optional<bool> magdrum::plugins::cachedVst3ModuleMayBeInstrument(
    const Vst3ModuleInstrumentCache& cache, const juce::File& bundle)
{
    const auto manifest = vst3ModuleManifestFile(bundle);
    if (!manifest.existsAsFile()) return {};
    const auto found = cache.find(bundle.getFullPathName().toStdString());
    if (found == cache.end()) return {};
    const auto& entry = found->second;
    if (entry.metadataPath != manifest.getFullPathName()
        || entry.metadataSize != manifest.getSize()
        || entry.metadataModifiedMilliseconds != manifest.getLastModificationTime().toMilliseconds())
        return {};
    return entry.mayBeInstrument;
}

void magdrum::plugins::rememberVst3ModuleInstrumentStatus(
    Vst3ModuleInstrumentCache& cache, const juce::File& bundle, bool mayBeInstrument)
{
    const auto manifest = vst3ModuleManifestFile(bundle);
    if (!manifest.existsAsFile()) return;
    Vst3ModuleInstrumentCacheEntry entry;
    entry.metadataPath = manifest.getFullPathName();
    entry.metadataSize = manifest.getSize();
    entry.metadataModifiedMilliseconds = manifest.getLastModificationTime().toMilliseconds();
    entry.mayBeInstrument = mayBeInstrument;
    cache[bundle.getFullPathName().toStdString()] = std::move(entry);
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
    root.setAttribute("version", 1);
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
        module->setAttribute("mayBeInstrument", entry->mayBeInstrument);
    }
    return cacheFile.replaceWithText(root.toString());
}

magdrum::plugins::CachedVst3ModuleIndex magdrum::plugins::indexCachedVst3Modules(
    const juce::Array<juce::PluginDescription>& cachedDescriptions)
{
    CachedVst3ModuleIndex decisions;
    decisions.reserve(static_cast<size_t>(cachedDescriptions.size()));
    for (const auto& description : cachedDescriptions)
    {
        if (description.pluginFormatName != "VST3")
            continue;

        auto& module = decisions[description.fileOrIdentifier.toStdString()];
        if (!module.hasFileModTime)
        {
            module.fileModTime = description.lastFileModTime;
            module.hasFileModTime = true;
        }
        else if (module.fileModTime != description.lastFileModTime)
        {
            module.cachedModTimesAgree = false;
        }
    }
    return decisions;
}

bool magdrum::plugins::isCachedVst3ModuleUpToDate(const CachedVst3ModuleInfo& cached,
                                                   const juce::File& module)
{
    return cached.hasFileModTime && cached.cachedModTimesAgree && module.exists()
        && module.getLastModificationTime() == cached.fileModTime;
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
            const int completed = static_cast<int>(progressObject->getProperty("completed"));
            const int total = static_cast<int>(progressObject->getProperty("total"));
            if (completed > lastReportedProgress && (completed % 24 == 0 || completed == total))
            {
                lastReportedProgress = completed;
                progress(completed, total, static_cast<int>(progressObject->getProperty("midiNote")),
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
    auto plugin = formats.createPluginInstance(description, sampleRate, blockSize, loadError);
    if (plugin == nullptr)
    {
        writeJson(resultFile, makeErrorManifest("Could not load the plugin in the isolated scanner process: " + loadError));
        return 5;
    }

    juce::MemoryBlock state;
    if (!stateFile.loadFileAsData(state) || state.isEmpty() || state.getSize() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        writeJson(resultFile, makeErrorManifest("The saved plugin kit state could not be read."));
        return 6;
    }
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    PluginNoteScanSettings settings;
    settings.sampleRate = sampleRate;
    settings.blockSize = blockSize;
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
        [&progressFile](int completed, int total, int midiNote, int velocity)
        {
            auto* progress = new juce::DynamicObject();
            progress->setProperty("completed", completed);
            progress->setProperty("total", total);
            progress->setProperty("midiNote", midiNote);
            progress->setProperty("velocity", velocity);
            writeJson(progressFile, juce::var(progress));
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
