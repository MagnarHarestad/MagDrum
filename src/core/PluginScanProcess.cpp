#include "PluginScanProcess.h"
#include "PluginNoteScanner.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>

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
        stream.get(), sampleRate, static_cast<unsigned int>(capture.audio.getNumChannels()), 24, {}, 0));
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
    const auto velocitiesValue = request->getProperty("velocityLevels");
    const auto* velocities = velocitiesValue.getArray();
    if (velocities == nullptr)
    {
        writeJson(resultFile, makeErrorManifest("The velocity list is missing from the scanner request."));
        return 7;
    }
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
            if (!captureLog->writeText(juce::JSON::toString(captureValue, false), false, false, nullptr)
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
