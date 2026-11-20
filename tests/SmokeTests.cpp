#include "core/SampleDatabase.h"
#include "core/WavScanner.h"
#include "core/DefaultLibraryDiscovery.h"
#include "core/AudioClassifier.h"
#include "core/MidiMapping.h"
#include "core/MidiPatternIO.h"
#include "core/SequencerGrid.h"
#include "core/SequencerPlayback.h"
#include "core/PluginNoteScanner.h"
#include "core/DeviceRealtimeRenderer.h"
#include "core/PluginScanProcess.h"
#include "core/PluginPrograms.h"
#include "core/PluginPresetFiles.h"
#include "core/PluginSourceKitRequest.h"
#include "core/PatternAudioRenderer.h"
#include "core/LoopingAudioSource.h"
#include "core/PluginVelocityLayerAnalyzer.h"
#include "core/PluginRoundRobinAnalyzer.h"
#include "core/SampleFeatureFilter.h"
#include "core/SampleSimilarityProjection.h"
#include "core/SampleClassificationReview.h"
#include "core/StructuredLogger.h"
#include <iostream>
#include <array>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <limits>
#include <utility>

class SyntheticInstrument final : public juce::AudioProcessor
{
public:
    explicit SyntheticInstrument(int outputChannelCount = 2, int activeOutputChannel = 0)
        : AudioProcessor(BusesProperties().withOutput("Test Out", channelSetFor(outputChannelCount), true)),
          outputChannels(outputChannelCount), activeOutput(activeOutputChannel) {}
    const juce::String getName() const override { return "Synthetic Test Instrument"; }
    void prepareToPlay(double rate, int) override { sampleRate = rate; phase = 0.0; if (changeProgramOnPrepare) program = 0; }
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        if (isNonRealtime()) sawOfflineProcessing = true; else sawRealtimeProcessing = true;
        for (const auto event : midi)
        {
            const auto message = event.getMessage();
            if (message.isNoteOn()) { activeNote = message.getNoteNumber(); amplitude = message.getFloatVelocity() * 0.2f; }
            else if (message.isNoteOff() && message.getNoteNumber() == activeNote) activeNote = -1;
        }
        buffer.clear();
        if (activeNote == 60) return;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float value = activeNote >= 0 ? amplitude * static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * phase / sampleRate)) : 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                if (outputChannels <= 2 || channel == activeOutput)
                    buffer.setSample(channel, i, value);
            phase += 1.0;
        }
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 2; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram(int value) override { program = juce::jlimit(0, 1, value); }
    const juce::String getProgramName(int index) override { return genericProgramNames ? "MIDI channel 0 program " + juce::String(index) : (index == 0 ? "Factory Kit A" : juce::String()); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override { destination.reset(); if (serializeProgram) destination.append(&program, sizeof(program)); }
    void setStateInformation(const void* data, int size) override { if (data != nullptr && size == static_cast<int>(sizeof(program))) std::memcpy(&program, data, sizeof(program)); }
    bool observedOfflineProcessing() const { return sawOfflineProcessing; }
    bool observedRealtimeProcessing() const { return sawRealtimeProcessing; }
    void clearRenderModeObservations() { sawOfflineProcessing = false; sawRealtimeProcessing = false; }
    void setGenericProgramNames(bool enabled) { genericProgramNames = enabled; }
    void setProgramSerializationEnabled(bool enabled) { serializeProgram = enabled; }
    void setProgramChangeOnPrepare(bool enabled) { changeProgramOnPrepare = enabled; }
private:
    static juce::AudioChannelSet channelSetFor(int channels)
    {
        if (channels == 1) return juce::AudioChannelSet::mono();
        if (channels == 2) return juce::AudioChannelSet::stereo();
        return juce::AudioChannelSet::discreteChannels(channels);
    }
    double sampleRate = 44100.0, phase = 0.0;
    float amplitude = 0.0f;
    int activeNote = -1, program = 0;
    int outputChannels = 2, activeOutput = 0;
    bool sawOfflineProcessing = false, sawRealtimeProcessing = false, serializeProgram = true, genericProgramNames = false, changeProgramOnPrepare = false;
};

class FixtureClassificationModel final : public DrumClassificationModel
{
public:
    FixtureClassificationModel(juce::String value = "Clap", double score = 0.83) : category(std::move(value)), confidence(score) {}
    DrumClassification classify(const ScannedSample&) const override
    {
        DrumClassification result; result.category = category; result.confidence = confidence; result.tags.add("Acoustic"); return result;
    }
private:
    juce::String category;
    double confidence;
};

bool runBuiltVst3WorkerFixture(const juce::File& vst3Module, const juce::File& pluginTestsExecutable,
    const juce::File& scannerExecutable, const juce::File& kickSampleFile,
    const juce::File& snareSampleFile, const juce::File& hihatSampleFile, const juce::File& root)
{
    juce::AudioPluginFormatManager formatManager;
    juce::addHeadlessDefaultFormatsToManager(formatManager);
    juce::AudioPluginFormat* vst3Format = nullptr;
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat(i)->getName() == "VST3") vst3Format = formatManager.getFormat(i);
    if (vst3Format == nullptr) { std::cerr << "VST3 format is unavailable to the test host.\n"; return false; }

    juce::OwnedArray<juce::PluginDescription> descriptions;
    vst3Format->findAllTypesForFile(descriptions, vst3Module.getFullPathName());
    if (descriptions.isEmpty()) { std::cerr << "The built VST3 module was not discovered.\n"; return false; }

    const auto requestDirectory = root.getChildFile("built-vst3-worker");
    const auto captureDirectory = requestDirectory.getChildFile("captures");
    if (!captureDirectory.createDirectory().wasOk()) return false;
    const auto descriptionFile = requestDirectory.getChildFile("plugin.xml");
    const auto stateFile = requestDirectory.getChildFile("state.bin");
    const auto requestFile = requestDirectory.getChildFile("request.json");
    const auto progressFile = requestDirectory.getChildFile("progress.json");
    const auto resultFile = requestDirectory.getChildFile("result.json");
    const auto cancellationFile = requestDirectory.getChildFile("cancel");
    const auto captureLogFile = requestDirectory.getChildFile("captures.jsonl");
    if (!descriptions[0]->createXml()->writeTo(descriptionFile)) return false;

    juce::StringArray stateCommand;
    stateCommand.add(pluginTestsExecutable.getFullPathName());
    stateCommand.add("--magdrum-test-write-plugin-state");
    stateCommand.add(kickSampleFile.getFullPathName());
    stateCommand.add(snareSampleFile.getFullPathName());
    stateCommand.add(hihatSampleFile.getFullPathName());
    stateCommand.add(stateFile.getFullPathName());
    juce::ChildProcess stateWriter;
    if (!stateWriter.start(stateCommand) || !stateWriter.waitForProcessToFinish(30000)
        || stateWriter.getExitCode() != 0 || !stateFile.existsAsFile())
    { std::cerr << "Could not create a host-format VST3 state fixture.\n"; return false; }

    juce::String pluginLoadError;
    auto loadedPlugin = formatManager.createPluginInstance(*descriptions[0], 44100.0, 512, pluginLoadError);
    if (loadedPlugin == nullptr)
    { std::cerr << "Could not instantiate the built VST3 for its tail-length check: " << pluginLoadError << "\n"; return false; }
    if (loadedPlugin->getTailLengthSeconds() != 0.0)
    { std::cerr << "A fresh MagDrum VST3 reported a sample tail before any sample was restored.\n"; return false; }
    juce::MemoryBlock hostState;
    if (!stateFile.loadFileAsData(hostState) || hostState.isEmpty())
    { std::cerr << "Could not read the host-format VST3 state fixture.\n"; return false; }
    loadedPlugin->setStateInformation(hostState.getData(), static_cast<int>(hostState.getSize()));
    constexpr double expectedTailSeconds = 4096.0 / 44100.0;
    const auto tailDeadline = juce::Time::getMillisecondCounter() + 5000u;
    while (std::abs(loadedPlugin->getTailLengthSeconds() - expectedTailSeconds) > 0.0001
        && juce::Time::getMillisecondCounter() < tailDeadline)
        juce::Thread::sleep(1);
    if (std::abs(loadedPlugin->getTailLengthSeconds() - expectedTailSeconds) > 0.0001)
    {
        std::cerr << "The built VST3 did not report the restored sample's tail duration: "
                  << loadedPlugin->getTailLengthSeconds() << " seconds.\n";
        return false;
    }

#if JUCE_WINDOWS
    juce::StringArray hostSequencerCommand;
    hostSequencerCommand.add(scannerExecutable.getFullPathName());
    hostSequencerCommand.add("--magdrum-test-host-sequencer");
    hostSequencerCommand.add(vst3Module.getFullPathName());
    hostSequencerCommand.add(stateFile.getFullPathName());
    juce::ChildProcess hostSequencerProcess;
    if (!hostSequencerProcess.start(hostSequencerCommand)
        || !hostSequencerProcess.waitForProcessToFinish(30000)
        || hostSequencerProcess.getExitCode() != 0)
    {
        std::cerr << "The exact built VST3 failed its isolated host-transport sequencer fixture (exit code "
                  << hostSequencerProcess.getExitCode() << ").\n";
        return false;
    }
#endif

    auto* request = new juce::DynamicObject();
    request->setProperty("descriptionFile", descriptionFile.getFullPathName());
    request->setProperty("stateFile", stateFile.getFullPathName());
    request->setProperty("outputDirectory", captureDirectory.getFullPathName());
    request->setProperty("cancellationFile", cancellationFile.getFullPathName());
    request->setProperty("progressFile", progressFile.getFullPathName());
    request->setProperty("captureLogFile", captureLogFile.getFullPathName());
    request->setProperty("resultFile", resultFile.getFullPathName());
    request->setProperty("sampleRate", 44100.0);
    request->setProperty("blockSize", 128);
    request->setProperty("repeatCount", 1);
    request->setProperty("noteDurationSeconds", 0.02);
    request->setProperty("releaseSeconds", 0.01);
    request->setProperty("captureLengthSeconds", 0.03);
    request->setProperty("realtimePaced", false);
    request->setProperty("audioDeviceRealtime", false);
    juce::Array<juce::var> velocities;
    velocities.add(100);
    request->setProperty("velocityLevels", velocities);
    if (!requestFile.replaceWithText(juce::JSON::toString(juce::var(request), true))) return false;

    juce::StringArray workerCommand;
    workerCommand.add(scannerExecutable.getFullPathName());
    workerCommand.add("--magdrum-plugin-scan-worker");
    workerCommand.add(requestFile.getFullPathName());
    const auto execution = PluginScanProcess::monitorWorker(workerCommand, progressFile, resultFile,
        cancellationFile, {}, {}, 30000, captureLogFile);
    if (!execution.started || !execution.manifestRead || !execution.manifest.succeeded
        || execution.manifest.error.isNotEmpty() || execution.manifest.captures.size() != 128)
    {
        std::cerr << "The isolated VST3 worker failed: " << execution.manifest.error
                  << " captures=" << execution.manifest.captures.size() << " exit=" << execution.exitCode << "\n";
        return false;
    }

    for (const auto& capture : execution.manifest.captures)
    {
        if (capture.midiNote != 36) continue;
        if (capture.silent || capture.peak <= 0.0 || capture.rms <= 0.0 || capture.sampleRate != 44100.0
            || !captureDirectory.getChildFile(capture.fileName).existsAsFile())
        {
            std::cerr << "The restored built VST3 did not render its sample: peak=" << capture.peak
                      << " rms=" << capture.rms << " silent=" << capture.silent << "\n";
            requestDirectory.deleteRecursively();
            return false;
        }
        requestDirectory.deleteRecursively();
        return true;
    }
    std::cerr << "The isolated worker did not return the sample's MIDI note.\n";
    return false;
}

int runSmokeTests(int argc,char** argv)
{
    const auto variableMeter=magdrum::sequencer::buildBarMap(4,4,3,{{3.0,5,4}});
    const auto boundaryMeter=magdrum::sequencer::buildBarMap(4,4,3,{{4.0,3,4}});
    const auto offGridMeter=magdrum::sequencer::buildBarMap(4,4,3,{{3.1,5,4}});
    const auto duplicateMeter=magdrum::sequencer::buildBarMap(4,4,2,{{3.0,1,1},{3.0,5,4}});
    const auto distantMeter=magdrum::sequencer::buildBarMap(4,4,1,{{8.1,5,4}});
    const auto nonFiniteMeter=magdrum::sequencer::buildBarMap(4,4,2,{{std::numeric_limits<double>::quiet_NaN(),5,4}});
    const auto infiniteMeter=magdrum::sequencer::buildBarMap(4,4,2,{{std::numeric_limits<double>::infinity(),5,4}});
    const auto eighthGrid=magdrum::sequencer::buildBarMap(4,4,1,{},2);
    const auto thirtySecondGrid=magdrum::sequencer::buildBarMap(4,4,16,{},8);
    const auto thirtySecondMeter=magdrum::sequencer::buildBarMap(4,4,2,{{0.125,3,4}},8);
    if (magdrum::sequencer::stepsPerBar(3,4) != 12
        || magdrum::sequencer::stepsPerBar(5,4) != 20
        || magdrum::sequencer::stepsPerBar(7,8) != 14
        || magdrum::sequencer::stepsPerBar(12,8) != 24
        || magdrum::sequencer::stepsPerBar(8,4) != 32
        || magdrum::sequencer::stepsPerBar(4,4,2) != 8
        || magdrum::sequencer::stepsPerBar(4,4,8) != 32
        || magdrum::sequencer::stepsPerBar(5,4,8) != 0
        || magdrum::sequencer::stepsPerBar(9,4) != 0
        || magdrum::sequencer::stepsForBars(3,4,4) != 48
        || magdrum::sequencer::stepsForBars(12,8,2) != 48
        || magdrum::sequencer::stepsForBars(8,4,16) != 512
        || magdrum::sequencer::stepsForBars(4,4,16,8) != 512
        || magdrum::sequencer::stepsForBars(4,4,0) != 0
        || magdrum::sequencer::stepsForBars(4,4,17) != 0
        || magdrum::sequencer::maximumSteps != 512
        || !magdrum::sequencer::isPositionOnGridStep(0.25, 1)
        || !magdrum::sequencer::isPositionOnGridStep(0.125,1,8)
        || magdrum::sequencer::isPositionOnGridStep(0.125,1,4)
        || !magdrum::sequencer::isPositionOnGridStep(0.25 + magdrum::sequencer::gridBeatTolerance, 1)
        || magdrum::sequencer::isPositionOnGridStep(0.10, 0)
        || magdrum::sequencer::isPositionOnGridStep(0.25 + 2.0 * magdrum::sequencer::gridBeatTolerance, 1)
        || magdrum::sequencer::isPositionOnGridStep(-0.25, 0)
        || magdrum::sequencer::isPositionOnGridStep(std::numeric_limits<double>::quiet_NaN(), 0)
        || magdrum::sequencer::isPositionOnGridStep(0.25, 1, 0)
        || !variableMeter.valid || variableMeter.barCount != 3 || variableMeter.totalSteps != 52
        || variableMeter.bars[0].startStep != 0 || variableMeter.bars[0].stepCount != 12
        || variableMeter.bars[1].startStep != 12 || variableMeter.bars[1].stepCount != 20
        || variableMeter.bars[2].startStep != 32 || variableMeter.bars[2].startBeat != 8.0
        || variableMeter.lengthBeats != 13.0
        || !boundaryMeter.valid || boundaryMeter.bars[0].stepCount != 16
        || boundaryMeter.bars[1].startStep != 16 || boundaryMeter.bars[1].stepCount != 12
        || offGridMeter.valid
        || !duplicateMeter.valid || duplicateMeter.bars[1].numerator != 5
        || !distantMeter.valid || distantMeter.lengthBeats != 4.0
        || nonFiniteMeter.valid || infiniteMeter.valid
        || !eighthGrid.valid || eighthGrid.totalSteps!=8 || eighthGrid.subdivisionsPerBeat!=2
        || !thirtySecondGrid.valid || thirtySecondGrid.totalSteps!=512 || thirtySecondGrid.subdivisionsPerBeat!=8
        || !thirtySecondMeter.valid || thirtySecondMeter.bars[0].stepCount!=1 || thirtySecondMeter.bars[1].startStep!=1)
        return 230;
    const std::vector<magdrum::sequencer::PlaybackEvent> playbackEvents{{0.0,36,100},{0.25,38,90},{0.5,42,80}};
    std::vector<std::pair<int,int>> scheduledHits;
    magdrum::sequencer::HostPlaybackCursor firstPlaybackBlock(playbackEvents,1.0,0.0,120.0,40.0);
    for(int sample=0;sample<6;++sample)firstPlaybackBlock.triggerAtSample([&](const auto& event){scheduledHits.emplace_back(sample,event.midiNote);});
    if(!firstPlaybackBlock.isValid()||scheduledHits!=std::vector<std::pair<int,int>>{{0,36},{5,38}})return 243;
    scheduledHits.clear();
    magdrum::sequencer::HostPlaybackCursor secondPlaybackBlock(playbackEvents,1.0,0.3,120.0,40.0);
    for(int sample=0;sample<6;++sample)secondPlaybackBlock.triggerAtSample([&](const auto& event){scheduledHits.emplace_back(sample,event.midiNote);});
    if(scheduledHits!=std::vector<std::pair<int,int>>{{4,42}})return 244;
    scheduledHits.clear();
    const std::vector<magdrum::sequencer::PlaybackEvent> loopEvents{{0.0,36,100},{0.25,38,90}};
    magdrum::sequencer::HostPlaybackCursor loopPlayback(loopEvents,0.5,0.4,120.0,40.0);
    for(int sample=0;sample<8;++sample)loopPlayback.triggerAtSample([&](const auto& event){scheduledHits.emplace_back(sample,event.midiNote);});
    if(scheduledHits!=std::vector<std::pair<int,int>>{{2,36},{7,38}})return 245;
    scheduledHits.clear();
    magdrum::sequencer::HostPlaybackCursor prerollPlayback(loopEvents,0.5,-0.1,120.0,40.0);
    for(int sample=0;sample<4;++sample)prerollPlayback.triggerAtSample([&](const auto& event){scheduledHits.emplace_back(sample,event.midiNote);});
    if(scheduledHits!=std::vector<std::pair<int,int>>{{2,36}})return 246;
    magdrum::sequencer::HostPlaybackCursor invalidPlayback(playbackEvents,0.0,0.0,120.0,40.0);
    if(invalidPlayback.isValid())return 247;
    if(argc==2&&std::strcmp(argv[1],"--magdrum-test-crash-fixture")==0)std::_Exit(1);
    if(argc==3&&std::strcmp(argv[1],"--magdrum-test-partial-crash-fixture")==0){const auto log=juce::File(argv[2]);log.replaceWithText(R"({"midiNote":36,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"sampleRate":44100,"peak":0.5,"rms":0.2,"silent":false,"fileName":"note_036_velocity_100_take_001.wav"})"+juce::String("\n"));std::_Exit(1);}
    if(!needsClassificationReview(false,"Unknown",0.99)||!needsClassificationReview(false,"Kick",0.74)||needsClassificationReview(false,"Kick",0.75)||needsClassificationReview(true,"Unknown",0.0)||!needsClassificationReview(false,"Snare",std::numeric_limits<double>::quiet_NaN()))return 128;
    if(!matchesSampleFeatureFilter(SampleFeatureFilter::veryShort,0.099,500.0)||matchesSampleFeatureFilter(SampleFeatureFilter::veryShort,0.1,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::shortSample,0.1,500.0)||matchesSampleFeatureFilter(SampleFeatureFilter::shortSample,0.5,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::mediumSample,0.5,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::longSample,2.0,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::dark,1.0,999.9)||matchesSampleFeatureFilter(SampleFeatureFilter::dark,1.0,1000.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::midBrightness,1.0,1000.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::bright,1.0,3000.0)||matchesSampleFeatureFilter(SampleFeatureFilter::bright,1.0,0.0))return 110;
    const auto logLine=StructuredLogger::formatEvent(StructuredLogger::Level::warning,"test.event",{{"detail","quoted \"text\"\nline"}},"2026-01-02T03:04:05Z");const auto parsedLog=juce::JSON::parse(logLine);if(!parsedLog.isObject()||parsedLog.getDynamicObject()->getProperty("level").toString()!="warning"||parsedLog.getDynamicObject()->getProperty("event").toString()!="test.event"||parsedLog.getDynamicObject()->getProperty("detail").toString()!="quoted \"text\"\nline")return 114;
    const PluginSourceKitRequest sourceKitRequest{"VST3|Magnar|Instrument|UUID", "Återkallat kit"};const auto parsedSourceKitRequest=PluginSourceKitRequest::fromVar(juce::JSON::parse(juce::JSON::toString(sourceKitRequest.toVar(),false)));if(!parsedSourceKitRequest||parsedSourceKitRequest->pluginIdentifier!=sourceKitRequest.pluginIdentifier||parsedSourceKitRequest->kitName!=sourceKitRequest.kitName)return 240;const PluginSourceKitRequest selectedInstrumentRequest{"VST3|Native Instruments|Battery 4|UUID",{}};const auto parsedSelectedInstrumentRequest=PluginSourceKitRequest::fromVar(juce::JSON::parse(juce::JSON::toString(selectedInstrumentRequest.toVar(),false)));if(!parsedSelectedInstrumentRequest||parsedSelectedInstrumentRequest->pluginIdentifier!=selectedInstrumentRequest.pluginIdentifier||parsedSelectedInstrumentRequest->kitName.isNotEmpty())return 243;if(PluginSourceKitRequest::fromVar(juce::JSON::parse("{\"kitName\":\"missing identifier\"}")))return 241;auto oversizedSourceKitRequest=sourceKitRequest;oversizedSourceKitRequest.pluginIdentifier=juce::String::repeatedString("x",4097);if(oversizedSourceKitRequest.isValid())return 242;
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrumScannerTest");
    root.deleteRecursively();
    if(!root.createDirectory().wasOk())return 1;
    const auto helperProgressFile = root.getChildFile("unresponsive-helper-progress.txt");
    const juce::StringArray unresponsiveHelperCommand{
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
        "--magdrum-test-unresponsive-helper", helperProgressFile.getFullPathName()};
    juce::String observedHelperPhase;
    const auto timedOutHelper = PluginScanProcess::monitorHelper(unresponsiveHelperCommand,
        helperProgressFile, 300, {}, [&observedHelperPhase](const juce::String& phase)
        {
            observedHelperPhase = phase;
        });
    if (!timedOutHelper.started || !timedOutHelper.timedOut || timedOutHelper.cancelled
        || observedHelperPhase != "loading")
    {
        std::cerr << "An unresponsive instrument helper was not stopped at its timeout.\n";
        return 290;
    }

    const auto cancelledHelperProgressFile = root.getChildFile("cancelled-helper-progress.txt");
    std::atomic<bool> cancelUnresponsiveHelper{false};
    const juce::StringArray cancelledHelperCommand{
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
        "--magdrum-test-unresponsive-helper", cancelledHelperProgressFile.getFullPathName()};
    const auto cancelledHelper = PluginScanProcess::monitorHelper(cancelledHelperCommand,
        cancelledHelperProgressFile, 5000,
        [&cancelUnresponsiveHelper] { return cancelUnresponsiveHelper.load(); },
        [&cancelUnresponsiveHelper](const juce::String&) { cancelUnresponsiveHelper.store(true); });
    if (!cancelledHelper.started || cancelledHelper.timedOut || !cancelledHelper.cancelled)
    {
        std::cerr << "An unresponsive instrument helper did not stop on cancellation.\n";
        return 291;
    }

    const auto timeoutDiscoveryProgressFile = root.getChildFile("vst3-discovery-timeout-progress.json");
    const auto firstDiscoveryModule = root.getChildFile("slow-first-instrument.vst3");
    const auto secondDiscoveryModule = root.getChildFile("unresponsive-second-instrument.vst3");
    const juce::StringArray discoveryCommand{
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName(),
        "--magdrum-test-vst3-discovery-timeout-fixture", timeoutDiscoveryProgressFile.getFullPathName(),
        firstDiscoveryModule.getFullPathName(), secondDiscoveryModule.getFullPathName()};
    const auto discoveryTimeout = magdrum::plugins::monitorVst3Discovery(discoveryCommand,
        timeoutDiscoveryProgressFile, 300, {}, 1000);
    magdrum::plugins::Vst3DiscoveryProgress timedOutProgress;
    if (!discoveryTimeout.started || !discoveryTimeout.timedOut || discoveryTimeout.cancelled
        || discoveryTimeout.timedOutModulePath != secondDiscoveryModule.getFullPathName()
        || !magdrum::plugins::recordVst3DiscoveryModuleFailure(timeoutDiscoveryProgressFile,
            discoveryTimeout.timedOutModulePath)
        || !magdrum::plugins::readVst3DiscoveryProgress(timeoutDiscoveryProgressFile, timedOutProgress)
        || timedOutProgress.completed != 1 || timedOutProgress.failedModules.size() != 1
        || !timedOutProgress.failedModules[0].equalsIgnoreCase(secondDiscoveryModule.getFullPathName()))
    {
        std::cerr << "VST3 discovery did not time out and isolate only the stalled instrument.\n";
        return 306;
    }

    const auto defaultPluginRootA=root.getChildFile("default-vst3-a");
    const auto defaultPluginRootB=root.getChildFile("default-vst3-b");
    const auto customPluginRoot=root.getChildFile("custom-vst3-root");
    if(!defaultPluginRootA.createDirectory().wasOk()||!defaultPluginRootB.createDirectory().wasOk()
        ||!customPluginRoot.createDirectory().wasOk())return 207;
    const auto defaultPluginPath=defaultPluginRootA.getFullPathName()+";"+defaultPluginRootB.getFullPathName();
    const juce::FileSearchPath defaultPluginSearch(defaultPluginPath);
    const auto customPluginSearch=magdrum::plugins::createVst3DiscoverySearchPath(defaultPluginSearch,customPluginRoot);
    if(customPluginSearch.getNumPaths()!=3||customPluginSearch[2]!=customPluginRoot)return 208;
    const juce::FileSearchPath defaultsWithCustom(defaultPluginPath+";"+customPluginRoot.getFullPathName());
    const auto duplicatePluginSearch=magdrum::plugins::createVst3DiscoverySearchPath(defaultsWithCustom,customPluginRoot);
    if(duplicatePluginSearch.getNumPaths()!=3)return 209;
    const auto effectBundle = customPluginRoot.getChildFile("WavesEffect.vst3");
    const auto instrumentBundle = customPluginRoot.getChildFile("DrumInstrument.vst3");
    const auto mixedBundle = customPluginRoot.getChildFile("MultiClassInstrument.vst3");
    const auto legacyBundle = customPluginRoot.getChildFile("LegacyInstrument.vst3");
    const auto legacyEffectBundle = customPluginRoot.getChildFile("Ozone 11 Imager.vst3");
    if (!effectBundle.createDirectory().wasOk() || !instrumentBundle.createDirectory().wasOk()
        || !mixedBundle.createDirectory().wasOk() || !legacyBundle.createDirectory().wasOk()
        || !legacyEffectBundle.createDirectory().wasOk()) return 215;
    const auto effectManifest = effectBundle.getChildFile("Contents/Resources/moduleinfo.json");
    const auto instrumentManifest = instrumentBundle.getChildFile("Contents/Resources/moduleinfo.json");
    const auto mixedManifest = mixedBundle.getChildFile("Contents/moduleinfo.json");
    if (!effectManifest.getParentDirectory().createDirectory().wasOk()
        || !instrumentManifest.getParentDirectory().createDirectory().wasOk()
        || !mixedManifest.getParentDirectory().createDirectory().wasOk()) return 216;
    if (!effectManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Fx","Dynamics"]}]})")
        || !instrumentManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Instrument"]}]})")
        || !mixedManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Fx"]},{"Category":"Audio Module Class","Sub Categories":["Instrument"]}]})")) return 217;
    if (magdrum::plugins::vst3ModuleMayBeInstrument(effectBundle)
        || !magdrum::plugins::vst3ModuleMayBeInstrument(instrumentBundle)
        || !magdrum::plugins::vst3ModuleMayBeInstrument(mixedBundle)
        || !magdrum::plugins::vst3ModuleMayBeInstrument(legacyBundle)
        || magdrum::plugins::vst3ModuleMayBeInstrument(legacyEffectBundle))
    {
        std::cerr << "VST3 discovery did not filter known effects while retaining instruments and legacy modules.\n";
        return 218;
    }
    const auto moduleFilterCacheFile = root.getChildFile("vst3-module-filter-cache.xml");
    const auto legacyModuleBinary = legacyBundle.getChildFile("Contents/x86_64-win/LegacyInstrument.vst3");
    if (!legacyModuleBinary.getParentDirectory().createDirectory().wasOk()
        || !legacyModuleBinary.replaceWithText("test legacy module")) return 220;
    magdrum::plugins::Vst3ModuleInstrumentCache moduleFilterCache;
    magdrum::plugins::rememberVst3ModuleInstrumentStatus(moduleFilterCache, effectBundle, false);
    magdrum::plugins::rememberVst3ModuleInstrumentStatus(moduleFilterCache, instrumentBundle, true);
    magdrum::plugins::rememberVst3ModuleInstrumentStatus(moduleFilterCache, legacyBundle, false);
    if (magdrum::plugins::cachedVst3ModuleMayBeInstrument(moduleFilterCache, effectBundle) != std::optional<bool>(false)
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(moduleFilterCache, instrumentBundle) != std::optional<bool>(true)
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(moduleFilterCache, legacyBundle) != std::optional<bool>(false)
        || !magdrum::plugins::saveVst3ModuleInstrumentCache(moduleFilterCacheFile, moduleFilterCache)) return 220;
    const auto restoredModuleFilterCache = magdrum::plugins::loadVst3ModuleInstrumentCache(moduleFilterCacheFile);
    if (magdrum::plugins::cachedVst3ModuleMayBeInstrument(restoredModuleFilterCache, effectBundle) != std::optional<bool>(false)
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(restoredModuleFilterCache, instrumentBundle) != std::optional<bool>(true)
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(restoredModuleFilterCache, legacyBundle) != std::optional<bool>(false))
        return 221;
    if (!effectManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Fx","Dynamics","Updated"]}]})")
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(restoredModuleFilterCache, effectBundle).has_value()) return 222;
    auto timestampInvalidationCache = restoredModuleFilterCache;
    magdrum::plugins::rememberVst3ModuleInstrumentStatus(timestampInvalidationCache, effectBundle, false);
    if (!effectManifest.setLastModificationTime(effectManifest.getLastModificationTime()
            + juce::RelativeTime::seconds(5))
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(timestampInvalidationCache, effectBundle).has_value())
        return 223;
    if (!legacyModuleBinary.setLastModificationTime(legacyModuleBinary.getLastModificationTime()
            + juce::RelativeTime::seconds(5))
        || magdrum::plugins::cachedVst3ModuleMayBeInstrument(restoredModuleFilterCache, legacyBundle).has_value()) return 223;
    auto classificationCache = magdrum::plugins::Vst3ModuleInstrumentCache{};
    magdrum::plugins::rememberVst3ModuleInstrumentStatus(classificationCache, effectBundle, false);
    if (magdrum::plugins::classifyVst3ModuleForDiscovery(classificationCache, effectBundle, true)
            != magdrum::plugins::Vst3ModuleDiscoveryAction::ExcludeEffect
        || !effectManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Instrument"]}]})")
        || magdrum::plugins::classifyVst3ModuleForDiscovery(classificationCache, effectBundle, true)
            != magdrum::plugins::Vst3ModuleDiscoveryAction::ScanInstrument
        || magdrum::plugins::classifyVst3ModuleForDiscovery(classificationCache, effectBundle, true)
            != magdrum::plugins::Vst3ModuleDiscoveryAction::Skip
        || !effectManifest.replaceWithText(R"({"Classes":[{"Category":"Audio Module Class","Sub Categories":["Fx"]}]})")
        || magdrum::plugins::classifyVst3ModuleForDiscovery(classificationCache, effectBundle, true)
            != magdrum::plugins::Vst3ModuleDiscoveryAction::ExcludeEffect)
        return 224;
    if (!mixedManifest.replaceWithText("not valid module metadata")
        || !magdrum::plugins::vst3ModuleMayBeInstrument(mixedBundle)) return 219;
    juce::Array<juce::PluginDescription> cachedPluginDescriptions;
    juce::PluginDescription cachedEffect;cachedEffect.fileOrIdentifier="effect-module";cachedEffect.pluginFormatName="VST3";cachedEffect.isInstrument=false;cachedPluginDescriptions.add(cachedEffect);
    juce::PluginDescription cachedInstrument;cachedInstrument.fileOrIdentifier="instrument-module";cachedInstrument.pluginFormatName="VST3";cachedInstrument.isInstrument=true;cachedPluginDescriptions.add(cachedInstrument);
    juce::PluginDescription mixedEffect; mixedEffect.fileOrIdentifier="multi-class-module";mixedEffect.pluginFormatName="VST3";mixedEffect.isInstrument=false;cachedPluginDescriptions.add(mixedEffect);
    juce::PluginDescription mixedInstrument=mixedEffect;mixedInstrument.isInstrument=true;cachedPluginDescriptions.add(mixedInstrument);
    juce::PluginDescription otherFormat=cachedInstrument;otherFormat.fileOrIdentifier="other-format";otherFormat.pluginFormatName="AudioUnit";cachedPluginDescriptions.add(otherFormat);
    juce::Array<juce::PluginDescription> discoveryDescriptions;
    for(int index=0;index<4096;++index)
    {
        juce::PluginDescription unrelated;unrelated.fileOrIdentifier="unrelated-module-"+juce::String(index);unrelated.pluginFormatName="VST3";unrelated.isInstrument=(index%2)==0;discoveryDescriptions.add(unrelated);
    }
    juce::PluginDescription bundledEffect;bundledEffect.fileOrIdentifier=instrumentBundle.getChildFile("Contents/x86_64-win/DrumInstrument.vst3").getFullPathName();bundledEffect.pluginFormatName="VST3";discoveryDescriptions.add(bundledEffect);
    juce::PluginDescription bundledWrongFormat=bundledEffect;bundledWrongFormat.pluginFormatName="AudioUnit";bundledWrongFormat.isInstrument=true;discoveryDescriptions.add(bundledWrongFormat);
    juce::PluginDescription discoveredInstrument=bundledEffect;discoveredInstrument.isInstrument=true;discoveryDescriptions.add(discoveredInstrument);
    if(!magdrum::plugins::vst3ModuleHasInstrumentDescription(discoveryDescriptions,instrumentBundle))return 253;
    discoveryDescriptions.remove(discoveryDescriptions.size()-1);
    if(magdrum::plugins::vst3ModuleHasInstrumentDescription(discoveryDescriptions,instrumentBundle))return 254;
    juce::KnownPluginList knownPluginList;
    for(int index=0;index<4096;++index)
    {
        juce::PluginDescription unrelated;
        unrelated.name="unrelated-plugin-"+juce::String(index);
        unrelated.fileOrIdentifier="unrelated-module-"+juce::String(index);
        unrelated.pluginFormatName="VST3";
        unrelated.isInstrument=(index%2)==0;
        knownPluginList.addType(unrelated);
    }
    juce::PluginDescription knownInstrument;
    knownInstrument.name="discovered-drum-instrument";
    knownInstrument.fileOrIdentifier=instrumentBundle.getChildFile("Contents/x86_64-win/DrumInstrument.vst3").getFullPathName();
    knownInstrument.pluginFormatName="VST3";
    knownInstrument.isInstrument=true;
    knownPluginList.addType(knownInstrument);
    if(!magdrum::plugins::vst3ModuleHasInstrumentDescription(knownPluginList,instrumentBundle))return 255;
    knownPluginList.removeType(knownInstrument);
    if(magdrum::plugins::vst3ModuleHasInstrumentDescription(knownPluginList,instrumentBundle))return 256;
    for(int index=0;index<4096;++index)
    {
        juce::PluginDescription bulk;bulk.fileOrIdentifier="bulk-module-"+juce::String(index);bulk.pluginFormatName="VST3";bulk.isInstrument=(index%2)==0;cachedPluginDescriptions.add(bulk);
    }
    const auto cachedModuleIndex=magdrum::plugins::indexCachedVst3Modules(cachedPluginDescriptions);
    if(cachedModuleIndex.size()!=4099||cachedModuleIndex.count("other-format")!=0
        ||!cachedModuleIndex.at("multi-class-module").hasFileModTime
        ||!cachedModuleIndex.at("multi-class-module").cachedModTimesAgree
        ||!cachedModuleIndex.at("multi-class-module").hasPluginDescriptions
        ||!cachedModuleIndex.at("multi-class-module").hasInstrumentDescription
        ||!cachedModuleIndex.at("effect-module").hasPluginDescriptions
        ||cachedModuleIndex.at("effect-module").hasInstrumentDescription
        ||!cachedModuleIndex.at("instrument-module").hasInstrumentDescription)return 210;
    const auto cachedVst3File = root.getChildFile("cached-module.vst3");
    if(!cachedVst3File.replaceWithText("test module"))return 211;
    juce::Array<juce::PluginDescription> timestampedDescriptions;
    juce::PluginDescription timestampedInstrument;timestampedInstrument.fileOrIdentifier=cachedVst3File.getFullPathName();timestampedInstrument.pluginFormatName="VST3";timestampedInstrument.isInstrument=true;timestampedInstrument.lastFileModTime=cachedVst3File.getLastModificationTime();timestampedDescriptions.add(timestampedInstrument);
    const auto timestampedIndex=magdrum::plugins::indexCachedVst3Modules(timestampedDescriptions);
    if(!magdrum::plugins::isCachedVst3ModuleUpToDate(timestampedIndex.at(cachedVst3File.getFullPathName().toStdString()),cachedVst3File))return 212;
    timestampedInstrument.lastFileModTime=juce::Time();timestampedDescriptions.add(timestampedInstrument);
    const auto conflictingTimestampIndex=magdrum::plugins::indexCachedVst3Modules(timestampedDescriptions);
    if(magdrum::plugins::isCachedVst3ModuleUpToDate(conflictingTimestampIndex.at(cachedVst3File.getFullPathName().toStdString()),cachedVst3File))return 213;
    auto staleTimestamp=timestampedIndex.at(cachedVst3File.getFullPathName().toStdString());staleTimestamp.fileModTime=juce::Time();
    if(magdrum::plugins::isCachedVst3ModuleUpToDate(staleTimestamp,cachedVst3File))return 214;
    const auto cachedVst3Bundle = root.getChildFile("cached-bundle.vst3");
    const auto cachedVst3BundleBinary = cachedVst3Bundle.getChildFile("Contents/x86_64-win/cached-bundle.vst3");
    if (!cachedVst3BundleBinary.getParentDirectory().createDirectory().wasOk()
        || !cachedVst3BundleBinary.replaceWithText("test VST3 bundle binary")) return 214;
    juce::PluginDescription bundledInstrument;
    bundledInstrument.fileOrIdentifier = cachedVst3BundleBinary.getFullPathName();
    bundledInstrument.pluginFormatName = "VST3";
    bundledInstrument.isInstrument = true;
    bundledInstrument.lastFileModTime = cachedVst3BundleBinary.getLastModificationTime();
    juce::Array<juce::PluginDescription> bundledDescriptions;
    bundledDescriptions.add(bundledInstrument);
    const auto bundledIndex = magdrum::plugins::indexCachedVst3Modules(bundledDescriptions);
    const auto cachedBundleEntry = bundledIndex.find(cachedVst3Bundle.getFullPathName().toStdString());
    if (cachedBundleEntry == bundledIndex.end()
        || !magdrum::plugins::isCachedVst3ModuleUpToDate(cachedBundleEntry->second, cachedVst3Bundle)) return 214;
    if (!cachedVst3BundleBinary.setLastModificationTime(cachedVst3BundleBinary.getLastModificationTime()
            + juce::RelativeTime::seconds(5))
        || magdrum::plugins::isCachedVst3ModuleUpToDate(cachedBundleEntry->second, cachedVst3Bundle)) return 214;
    const auto discoveryProgressFile = root.getChildFile("vst3-discovery-progress.json");
    const auto crashedModulePath = root.getChildFile("Komplete Kontrol.vst3").getFullPathName();
    magdrum::plugins::Vst3DiscoveryProgress discoveryProgress;
    discoveryProgress.total = 3;
    discoveryProgress.current = "Komplete Kontrol";
    discoveryProgress.currentPath = crashedModulePath;
    if (!magdrum::plugins::writeVst3DiscoveryProgress(discoveryProgressFile, discoveryProgress)) return 300;
    magdrum::plugins::Vst3DiscoveryProgress recoveredDiscoveryProgress;
    if (!magdrum::plugins::readVst3DiscoveryProgress(discoveryProgressFile, recoveredDiscoveryProgress)
        || recoveredDiscoveryProgress.completed != 0 || recoveredDiscoveryProgress.total != 3
        || recoveredDiscoveryProgress.currentPath != crashedModulePath) return 301;
    if (!magdrum::plugins::recordVst3DiscoveryModuleFailure(discoveryProgressFile,
            crashedModulePath.toUpperCase())) return 302;
    if (!magdrum::plugins::readVst3DiscoveryProgress(discoveryProgressFile, recoveredDiscoveryProgress)
        || recoveredDiscoveryProgress.completed != 1 || recoveredDiscoveryProgress.total != 3
        || recoveredDiscoveryProgress.failedModules.size() != 1
        || !recoveredDiscoveryProgress.failedModules[0].equalsIgnoreCase(crashedModulePath)
        || recoveredDiscoveryProgress.currentPath.isNotEmpty()) return 303;
    if (magdrum::plugins::recordVst3DiscoveryModuleFailure(discoveryProgressFile, crashedModulePath)) return 304;
    recoveredDiscoveryProgress.current = "Omnisphere";
    recoveredDiscoveryProgress.currentPath = root.getChildFile("Omnisphere.vst3").getFullPathName();
    if (!magdrum::plugins::writeVst3DiscoveryProgress(discoveryProgressFile, recoveredDiscoveryProgress)
        || !magdrum::plugins::recordVst3DiscoveryModuleFailure(discoveryProgressFile,
            recoveredDiscoveryProgress.currentPath)
        || !magdrum::plugins::readVst3DiscoveryProgress(discoveryProgressFile, recoveredDiscoveryProgress)
        || recoveredDiscoveryProgress.completed != 2 || recoveredDiscoveryProgress.failedModules.size() != 2) return 305;
    const auto midiFixture=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("LibraryDiscoveryFixture-"+juce::Uuid().toString());
    midiFixture.deleteRecursively();
    const auto midiRoot=midiFixture.getChildFile("midi-discovery");
    const auto drumFolder=midiRoot.getChildFile("Native Instruments").getChildFile("Drums");
    const auto beatFolder=midiRoot.getChildFile("Groove Patterns");
    const auto otherFolder=midiRoot.getChildFile("Scales");
    if(!drumFolder.createDirectory().wasOk()||!beatFolder.createDirectory().wasOk()||!otherFolder.createDirectory().wasOk())return 201;
    if(!drumFolder.getChildFile("Kit_01.mid").replaceWithText("fixture")||!drumFolder.getChildFile("Fill.MIDI").replaceWithText("fixture")||!beatFolder.getChildFile("Breakbeat.mid").replaceWithText("fixture")||!otherFolder.getChildFile("MajorScale.mid").replaceWithText("fixture"))return 202;
    const auto discoveredMidi=magdrum::library::findDrumMidiPatternFiles({midiRoot});
    if(discoveredMidi.size()!=3||std::none_of(discoveredMidi.begin(),discoveredMidi.end(),[](const juce::File& file){return file.getFileName()=="Kit_01.mid";})||std::none_of(discoveredMidi.begin(),discoveredMidi.end(),[](const juce::File& file){return file.getFileName()=="Fill.MIDI";})||std::none_of(discoveredMidi.begin(),discoveredMidi.end(),[](const juce::File& file){return file.getFileName()=="Breakbeat.mid";}))return 203;
    if(magdrum::library::findDrumMidiPatternFiles({midiRoot},1).size()!=1||!magdrum::library::findDrumMidiPatternFiles({midiRoot},200,500000,[]{return true;}).empty())return 204;
    const auto defaultFolders=magdrum::library::findDefaultLibraryFolders();
    for(size_t i=0;i<defaultFolders.size();++i){if(!defaultFolders[i].isDirectory())return 205;for(size_t j=i+1;j<defaultFolders.size();++j)if(defaultFolders[i].getFullPathName().compareIgnoreCase(defaultFolders[j].getFullPathName())==0)return 206;}
#if JUCE_LINUX
    const auto linuxCommonDocuments=juce::File::getSpecialLocation(juce::File::commonDocumentsDirectory);
    if(std::any_of(defaultFolders.begin(),defaultFolders.end(),[&](const juce::File& folder){return folder.getFullPathName().compareIgnoreCase(linuxCommonDocuments.getFullPathName())==0;}))return 247;
#endif
    const auto defaultLibraryFixture=root.getChildFile("default-library-roots");
    magdrum::library::DefaultLibraryRoots fixtureRoots{defaultLibraryFixture.getChildFile("CommonDocuments"),defaultLibraryFixture.getChildFile("CommonData"),defaultLibraryFixture.getChildFile("UserDocuments"),defaultLibraryFixture.getChildFile("UserMusic"),defaultLibraryFixture.getChildFile("UserAppData"),defaultLibraryFixture.getChildFile("ProgramFiles")};
    const std::vector<juce::File> expectedLibraryFolders{
        fixtureRoots.commonDocuments,fixtureRoots.commonDocuments.getChildFile("Native Instruments"),fixtureRoots.commonDocuments.getChildFile("Steinberg"),fixtureRoots.commonDocuments.getChildFile("VST Sound"),
        fixtureRoots.commonData.getChildFile("Native Instruments"),fixtureRoots.commonData.getChildFile("Steinberg/Content"),fixtureRoots.commonData.getChildFile("Spectrasonics/STEAM"),
        fixtureRoots.userDocuments.getChildFile("Native Instruments"),fixtureRoots.userDocuments.getChildFile("Steinberg"),fixtureRoots.userMusic.getChildFile("Native Instruments"),fixtureRoots.userMusic.getChildFile("Steinberg"),
        fixtureRoots.userApplicationData.getChildFile("Steinberg/Content"),
        fixtureRoots.programFiles.getChildFile("Steinberg/Cubase 15/VST Sound"),fixtureRoots.programFiles.getChildFile("Steinberg/Cubase 15/Media")
    };
    for(const auto& folder:expectedLibraryFolders)if(!folder.createDirectory().wasOk())return 243;
    const auto perUserSteinbergMidi=fixtureRoots.userApplicationData.getChildFile("Steinberg/Content/Groove MIDI/Factory Drum Groove.mid");
    if(!perUserSteinbergMidi.getParentDirectory().createDirectory().wasOk()||!perUserSteinbergMidi.replaceWithText("fixture"))return 248;
    const auto fixtureLibraryFolders=magdrum::library::findDefaultLibraryFolders(fixtureRoots);
    const auto perUserLibraryMidi=magdrum::library::findDrumMidiPatternFiles(fixtureLibraryFolders);
    if(fixtureLibraryFolders.size()!=11||std::none_of(fixtureLibraryFolders.begin(),fixtureLibraryFolders.end(),[&](const juce::File& found){return found.getFullPathName().compareIgnoreCase(fixtureRoots.commonDocuments.getFullPathName())==0;})||std::none_of(fixtureLibraryFolders.begin(),fixtureLibraryFolders.end(),[&](const juce::File& found){return found.getFullPathName().compareIgnoreCase(fixtureRoots.userApplicationData.getChildFile("Steinberg/Content").getFullPathName())==0;})||std::none_of(perUserLibraryMidi.begin(),perUserLibraryMidi.end(),[&](const juce::File& found){return found==perUserSteinbergMidi;})||std::any_of(fixtureLibraryFolders.begin(),fixtureLibraryFolders.end(),[&](const juce::File& found){return found.isAChildOf(fixtureRoots.commonDocuments);}))return 244;
    fixtureRoots.userMusic=fixtureRoots.commonDocuments;
    const auto deduplicatedLibraryFolders=magdrum::library::findDefaultLibraryFolders(fixtureRoots);
    if(deduplicatedLibraryFolders.size()!=9)return 245;
    midiFixture.deleteRecursively();
    juce::StringArray crashCommand;crashCommand.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());crashCommand.add("--magdrum-test-crash-fixture");
    const auto crashExecution=PluginScanProcess::monitorWorker(crashCommand,root.getChildFile("crash-progress.json"),root.getChildFile("crash-result.json"),root.getChildFile("crash-cancel"));
    if(!crashExecution.started||crashExecution.manifestRead||crashExecution.terminatedForCancellation||crashExecution.exitCode==0)return 144;
    juce::StringArray partialCrashCommand;partialCrashCommand.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());partialCrashCommand.add("--magdrum-test-partial-crash-fixture");const auto partialCaptureLog=root.getChildFile("partial-captures.jsonl");partialCrashCommand.add(partialCaptureLog.getFullPathName());
    const auto partialCrashExecution=PluginScanProcess::monitorWorker(partialCrashCommand,root.getChildFile("partial-progress.json"),root.getChildFile("partial-result.json"),root.getChildFile("partial-cancel"),{}, {},10000,partialCaptureLog);
    if(!partialCrashExecution.started||!partialCrashExecution.manifestRead||!partialCrashExecution.manifest.partial||partialCrashExecution.manifest.succeeded||partialCrashExecution.manifest.captures.size()!=1||partialCrashExecution.exitCode==0)return 180;
    const auto phaseProgressFile = root.getChildFile("phase-progress.json");
    const auto phaseResultFile = root.getChildFile("phase-result.json");
    juce::StringArray phaseCommand;
    phaseCommand.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());
    phaseCommand.add("--magdrum-test-plugin-scan-progress-worker");
    phaseCommand.add(phaseProgressFile.getFullPathName());
    phaseCommand.add(phaseResultFile.getFullPathName());
    juce::StringArray reportedPhases;
    juce::Array<int> reportedCaptureCounts;
    const auto phaseExecution = PluginScanProcess::monitorWorker(phaseCommand, phaseProgressFile,
        phaseResultFile, root.getChildFile("phase-cancel"), {},
        [&reportedPhases, &reportedCaptureCounts](const juce::String& phase, int completed, int, int, int)
        {
            if (reportedPhases.isEmpty() || reportedPhases[reportedPhases.size() - 1] != phase)
                reportedPhases.add(phase);
            reportedCaptureCounts.add(completed);
        }, 10000);
    if (!phaseExecution.started || !phaseExecution.manifestRead || !phaseExecution.manifest.succeeded
        || reportedPhases.size() != 4 || reportedPhases[0] != "loading" || reportedPhases[1] != "restoring"
        || reportedPhases[2] != "preparing" || reportedPhases[3] != "scanning"
        || !reportedCaptureCounts.contains(24) || !reportedCaptureCounts.contains(128))
    {
        std::cerr << "Plugin scan phases reported:";
        for (const auto& phase : reportedPhases) std::cerr << " " << phase;
        std::cerr << " counts:";
        for (const auto count : reportedCaptureCounts) std::cerr << " " << count;
        std::cerr << "\n";
        return 290;
    }
    if (!PluginScanProcess::shouldPublishNoteScanProgress(1, 128)
        || PluginScanProcess::shouldPublishNoteScanProgress(23, 128)
        || !PluginScanProcess::shouldPublishNoteScanProgress(24, 128)
        || !PluginScanProcess::shouldPublishNoteScanProgress(128, 128)
        || !PluginScanProcess::shouldPublishNoteScanProgress(127, 127)) return 298;
    const auto workerManifestFile=root.getChildFile("worker-result.json");
    if(!workerManifestFile.replaceWithText(R"({"ok":true,"error":"","cancelled":false,"captures":[{"midiNote":36,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"sampleRate":44100,"peak":0.5,"rms":0.2,"silent":false,"fileName":"note_036_velocity_100_take_001.wav"},{"midiNote":60,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"sampleRate":44100,"peak":0.0,"rms":0.0,"silent":true,"fileName":""}]})"))return 131;
    PluginScanProcessManifest workerManifest;
    if(!PluginScanProcess::readManifest(workerManifestFile,workerManifest)||!workerManifest.succeeded||workerManifest.error.isNotEmpty()||workerManifest.cancelled||workerManifest.captures.size()!=2||workerManifest.captures[0].midiNote!=36||workerManifest.captures[0].fileName!="note_036_velocity_100_take_001.wav"||workerManifest.captures[0].sampleRate!=44100||workerManifest.captures[1].midiNote!=60||!workerManifest.captures[1].silent)return 132;
    if(!workerManifestFile.replaceWithText(R"({"ok":true,"error":"","cancelled":false,"captures":[{"midiNote":36,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"peak":0.5,"rms":0.2,"silent":false,"fileName":"note_036_velocity_100_take_001.wav"}]})")||!PluginScanProcess::readManifest(workerManifestFile,workerManifest)||workerManifest.captures.size()!=1||workerManifest.captures[0].sampleRate!=0.0)return 186;
    if(!workerManifestFile.replaceWithText(R"({"ok":true,"error":"","cancelled":false,"captures":[{"midiNote":128,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"sampleRate":44100,"peak":0.5,"rms":0.2,"silent":false,"fileName":"bad.wav"}]})")||PluginScanProcess::readManifest(workerManifestFile,workerManifest))return 133;
    const auto presetFolder=root.getChildFile("presets");if(!presetFolder.createDirectory().wasOk())return 124;const auto presetA=presetFolder.getChildFile("A Kit.vstpreset");const auto presetB=presetFolder.getChildFile("nested").getChildFile("B Kit.VSTPRESET");if(!presetB.getParentDirectory().createDirectory().wasOk()||!presetA.replaceWithText("fixture")||!presetB.replaceWithText("fixture")||!presetFolder.getChildFile("ignore.fxp").replaceWithText("fixture"))return 125;const auto foundPresets=findVst3PresetFiles(presetFolder);if(!foundPresets.rootIsDirectory||foundPresets.cancelled||foundPresets.files.size()!=2||foundPresets.files[0].getFileName()!="A Kit.vstpreset"||foundPresets.files[1].getFileName()!="B Kit.VSTPRESET")return 126;const auto cancelledPresetSearch=findVst3PresetFiles(presetFolder,[]{return true;});if(!cancelledPresetSearch.cancelled||!cancelledPresetSearch.files.isEmpty())return 127;
    const auto presetApp=root.getChildFile("preset-app");if(!presetApp.createDirectory().wasOk())return 136;const auto standardRoots=getStandardVst3PresetSearchRoots("MagDrum Test","Kit:Fixture",presetApp.getChildFile("MagDrum.exe"));if(standardRoots.isEmpty())return 137;
#if JUCE_WINDOWS
    const auto expectedUserPresetRoot=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VST3 Presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
    if(standardRoots.isEmpty()||standardRoots[0]!=expectedUserPresetRoot)return 138;
    const auto appPresetRoot=presetApp.getChildFile("VST3 Presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
#elif JUCE_MAC
    const auto expectedUserPresetRoot=juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Audio/Presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
    if(standardRoots.isEmpty()||standardRoots[0]!=expectedUserPresetRoot)return 138;
    const auto appPresetRoot=presetApp.getChildFile("VST3 Presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
#elif JUCE_LINUX
    const auto expectedUserPresetRoot=juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".vst3/presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
    if(standardRoots.isEmpty()||standardRoots[0]!=expectedUserPresetRoot)return 138;
    const auto appPresetRoot=presetApp.getChildFile("vst3/presets").getChildFile("MagDrum Test").getChildFile("Kit_Fixture");
#endif
    if(!appPresetRoot.createDirectory().wasOk()||!appPresetRoot.getChildFile("Standard.vstpreset").replaceWithText("fixture"))return 139;auto duplicateSearchRoots=standardRoots;duplicateSearchRoots.add(appPresetRoot);const auto standardPresets=findVst3PresetFiles(duplicateSearchRoots);if(!standardPresets.rootIsDirectory||standardPresets.cancelled||standardPresets.files.size()!=1||standardPresets.files[0].getFileName()!="Standard.vstpreset")return 140;const auto cancelledStandardSearch=findVst3PresetFiles(standardRoots,[]{return true;});if(!cancelledStandardSearch.cancelled)return 141;
    const auto priorityFirst=root.getChildFile("preset-priority-first"),prioritySecond=root.getChildFile("preset-priority-second");if(!priorityFirst.createDirectory().wasOk()||!prioritySecond.createDirectory().wasOk()||!priorityFirst.getChildFile("Zulu.vstpreset").replaceWithText("first")||!prioritySecond.getChildFile("Alpha.vstpreset").replaceWithText("second"))return 142;juce::Array<juce::File> priorityRoots;priorityRoots.add(priorityFirst);priorityRoots.add(prioritySecond);const auto prioritySearch=findVst3PresetFiles(priorityRoots);if(prioritySearch.files.size()!=2||prioritySearch.files[0].getFileName()!="Zulu.vstpreset"||prioritySearch.files[1].getFileName()!="Alpha.vstpreset")return 143;
    auto waveFile=root.getChildFile("kick.wav");
    juce::WavAudioFormat wav;
    const auto writeToneFixture=[&wav](const juce::File& file,double frequency)
    {
        auto output=file.createOutputStream();
        if(!output)return false;
        std::unique_ptr<juce::AudioFormatWriter> toneWriter(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));
        if(!toneWriter)return false;
        output.release();
        std::array<float,4096> tone{};
        for(size_t index=0;index<tone.size();++index)
            tone[index]=0.5f*static_cast<float>(std::sin(2.0*juce::MathConstants<double>::pi*frequency*index/44100.0));
        const float* channels[]{tone.data()};
        return toneWriter->writeFromFloatArrays(channels,1,static_cast<int>(tone.size()));
    };
    if(!writeToneFixture(waveFile,440.0))return 4;
    if(argc==4)
    {
        const auto hostSampleDirectory=root.getSiblingFile(root.getFileName()+"-host-samples");
        if(!hostSampleDirectory.createDirectory().wasOk())return 4;
        const auto snareSampleFile=hostSampleDirectory.getChildFile("snare.wav");
        const auto hihatSampleFile=hostSampleDirectory.getChildFile("closed-hihat.wav");
        if(!writeToneFixture(snareSampleFile,220.0)||!writeToneFixture(hihatSampleFile,1760.0))return 4;
        const bool hostFixturePassed=runBuiltVst3WorkerFixture(juce::File(argv[1]),juce::File(argv[2]),
            juce::File(argv[3]),waveFile,snareSampleFile,hihatSampleFile,root);
        hostSampleDirectory.deleteRecursively();
        if(!hostFixturePassed)return 188;
    }

    WavScanner scanner;
    WavScanner::Stats initialScanStats;
    int scanProgressCallbacks=0, scanProgressCompleted=-1, scanProgressTotal=-1;
    auto scanned=scanner.scan(root,{}, {},&initialScanStats,{}, {},{}, {},
        [&](const juce::File&,int completed,int total){++scanProgressCallbacks;scanProgressCompleted=completed;scanProgressTotal=total;});
    if(scanned.size()!=1||scanned.getFirst().file!=waveFile)return 5;
    if(initialScanStats.filesConsidered!=1||initialScanStats.filesAnalyzed!=1||initialScanStats.errors!=0)return 44;
    if(scanProgressCallbacks<2||scanProgressCompleted!=1||scanProgressTotal!=1)return 286;
    if(scanned.getFirst().channels!=1||scanned.getFirst().sampleRate!=44100.0)return 6;
    if(scanned.getFirst().format!="WAV"||scanned.getFirst().bitDepth!=16||scanned.getFirst().fingerprint.isEmpty())return 12;
    if(scanned.getFirst().acousticFingerprint.length()!=128)return 56;
    const auto encodeFingerprint=[](const std::array<std::uint8_t,64>& values){constexpr char digits[]="0123456789abcdef";juce::String encoded;for(const auto value:values){encoded+=digits[value>>4];encoded+=digits[value&0x0f];}return encoded;};
    std::array<std::uint8_t,64> lowSpectrum{},nearbyLowSpectrum{},highSpectrum{};
    for(size_t segment=0;segment<4;++segment){lowSpectrum[segment*16+2]=220;lowSpectrum[segment*16+3]=35;nearbyLowSpectrum[segment*16+2]=215;nearbyLowSpectrum[segment*16+3]=40;highSpectrum[segment*16+12]=220;highSpectrum[segment*16+13]=35;}
    const auto similarityPoints=magdrum::projectAcousticFingerprints({encodeFingerprint(lowSpectrum),encodeFingerprint(nearbyLowSpectrum),encodeFingerprint(highSpectrum),juce::String("invalid")});
    const auto pointDistance=[](const magdrum::SampleSimilarityPoint& first,const magdrum::SampleSimilarityPoint& second){return std::hypot(first.x-second.x,first.y-second.y);};
    if(similarityPoints.size()!=4||!similarityPoints[0].hasAcousticFingerprint||!similarityPoints[1].hasAcousticFingerprint||!similarityPoints[2].hasAcousticFingerprint||similarityPoints[3].hasAcousticFingerprint||pointDistance(similarityPoints[0],similarityPoints[1])>0.1f||pointDistance(similarityPoints[0],similarityPoints[2])<0.5f)return 227;
    const auto representativeIndices=magdrum::representativeSampleIndices(100000);if(representativeIndices.size()!=3000||representativeIndices.front()!=0||representativeIndices.back()!=99999||!std::is_sorted(representativeIndices.begin(),representativeIndices.end()))return 228;
    if (!magdrum::isSampleSimilarityPointVisible(0.25f, 0.75f, 2.0f, 0.25f, 0.25f)
        || magdrum::isSampleSimilarityPointVisible(0.24f, 0.75f, 2.0f, 0.25f, 0.25f)
        || magdrum::isSampleSimilarityPointVisible(0.50f, 0.76f, 2.0f, 0.25f, 0.25f)
        || magdrum::isSampleSimilarityPointVisible(0.50f, 0.50f, 0.0f, 0.25f, 0.25f)) return 288;
    magdrum::SampleSimilarityViewport mapViewport;
    mapViewport.zoomAt(1.0f, 0.37f, 0.62f);
    const auto anchoredMapPoint = mapViewport.transform(0.37f, 1.0f - 0.62f);
    if (mapViewport.zoom <= 1.0f || std::abs(anchoredMapPoint.x - 0.37f) > 0.0001f
        || std::abs(anchoredMapPoint.y - 0.62f) > 0.0001f) return 289;
    const auto zoomedMapPoint = mapViewport.transform(0.8f, 0.2f);
    mapViewport.panFrom(mapViewport.panX, mapViewport.panY, 100000.0f, -100000.0f, 500.0f, 300.0f);
    if (mapViewport.panX < 0.0f || mapViewport.panY < 0.0f
        || mapViewport.panX > 1.0f - 1.0f / mapViewport.zoom
        || mapViewport.panY > 1.0f - 1.0f / mapViewport.zoom
        || mapViewport.contains(0.0f, 1.0f) || !mapViewport.contains(0.0f, 0.0f)) return 290;
    mapViewport.panFrom(mapViewport.panX, mapViewport.panY, -100000.0f, 100000.0f, 500.0f, 300.0f);
    if (mapViewport.panX < 0.0f || mapViewport.panY < 0.0f
        || mapViewport.panX > 1.0f - 1.0f / mapViewport.zoom
        || mapViewport.panY > 1.0f - 1.0f / mapViewport.zoom
        || !mapViewport.contains(1.0f, 1.0f) || !std::isfinite(zoomedMapPoint.x) || !std::isfinite(zoomedMapPoint.y)) return 291;
    if(scanned.getFirst().fundamentalHz<418.0||scanned.getFirst().fundamentalHz>462.0||scanned.getFirst().pitchConfidence<0.65)return 43;
    if(scanned.getFirst().peak<0.45||scanned.getFirst().rms<0.30||scanned.getFirst().spectralCentroidHz<300||scanned.getFirst().spectralCentroidHz>600||scanned.getFirst().spectralFlatness>0.1){std::cerr<<"Feature check: peak="<<scanned.getFirst().peak<<" rms="<<scanned.getFirst().rms<<" centroid="<<scanned.getFirst().spectralCentroidHz<<" flatness="<<scanned.getFirst().spectralFlatness<<"\n";return 18;}
    if(!scanner.scan(root,[]{return true;}).isEmpty())return 7;

    const auto formatRoot=root.getSiblingFile("MagDrumAdditionalFormatsTest");formatRoot.deleteRecursively();if(!formatRoot.createDirectory().wasOk())return 144;
    juce::AudioFormatManager testFormats;testFormats.registerBasicFormats();
    auto* aiffFormat=testFormats.findFormatForFileExtension("aiff");auto* flacFormat=testFormats.findFormatForFileExtension("flac");
    if(aiffFormat==nullptr||flacFormat==nullptr)return 145;
    const auto writeFormatFixture=[](juce::AudioFormat& format,const juce::File& file)
    {
        std::unique_ptr<juce::OutputStream> output(file.createOutputStream());if(!output)return false;
        std::unique_ptr<juce::AudioFormatWriter> formatWriter(format.createWriterFor(output,juce::AudioFormatWriterOptions().withSampleRate(44100.0).withNumChannels(1).withBitsPerSample(16)));
        if(!formatWriter)return false;
        std::array<float,4096> audio{};for(size_t index=0;index<audio.size();++index)audio[index]=0.4f*static_cast<float>(std::sin(2.0*juce::MathConstants<double>::pi*330.0*index/44100.0));
        const float* channels[]{audio.data()};const bool wrote=formatWriter->writeFromFloatArrays(channels,1,static_cast<int>(audio.size()));formatWriter.reset();return wrote&&file.existsAsFile();
    };
    if(!writeFormatFixture(*aiffFormat,formatRoot.getChildFile("fixture.aiff"))||!writeFormatFixture(*flacFormat,formatRoot.getChildFile("fixture.flac")))return 146;
    juce::WavAudioFormat additionalWavFormat;if(!writeFormatFixture(additionalWavFormat,formatRoot.getChildFile("fixture.wav")))return 146;
    if(!formatRoot.getChildFile("factory.vstsound").replaceWithText("container fixture")||!formatRoot.getChildFile("kick.ncw").replaceWithText("compressed sample fixture"))return 146;
    WavScanner::Stats formatScanStats;const auto additionalFormatSamples=scanner.scan(formatRoot,{}, {},&formatScanStats);
    juce::StringArray scannedFormats;for(const auto& formatSample:additionalFormatSamples){scannedFormats.addIfNotAlreadyThere(formatSample.format);if(formatSample.channels!=1||formatSample.sampleRate!=44100.0||formatSample.durationSeconds<=0.0||formatSample.peak<0.35||formatSample.acousticFingerprint.length()!=128)return 147;}
    if(additionalFormatSamples.size()!=3||formatScanStats.filesConsidered!=3||formatScanStats.filesAnalyzed!=3||formatScanStats.errors!=0||formatScanStats.steinbergVstSoundContainers!=1||formatScanStats.kontaktNcwFiles!=1||!scannedFormats.contains("WAV")||!scannedFormats.contains("AIFF")||!scannedFormats.contains("FLAC"))return 148;

    const auto classificationTagRoot=root.getSiblingFile("MagDrumClassificationTagTest");classificationTagRoot.deleteRecursively();if(!classificationTagRoot.createDirectory().wasOk())return 149;
    const auto classifiedFile=classificationTagRoot.getChildFile("808_Open_HiHat_deep.wav");if(!waveFile.copyFileTo(classifiedFile))return 150;
    const auto classifiedSamples=scanner.scan(classificationTagRoot);if(classifiedSamples.size()!=1||classifiedSamples.getFirst().category!="Open hi-hat"||!classifiedSamples.getFirst().classificationTags.contains("808")||!classifiedSamples.getFirst().classificationTags.contains("Electronic")||!classifiedSamples.getFirst().classificationTags.contains("Deep"))return 151;
    const auto batteryClassificationRoot=root.getSiblingFile("MagDrumBatteryClassificationTest");batteryClassificationRoot.deleteRecursively();if(!batteryClassificationRoot.createDirectory().wasOk())return 310;
    const auto batteryHatDirectory=batteryClassificationRoot.getChildFile("Samples/Drums/HiHat"),batteryCymbalDirectory=batteryClassificationRoot.getChildFile("Samples/Drums/Cymbal");
    if(!batteryHatDirectory.createDirectory().wasOk()||!batteryCymbalDirectory.createDirectory().wasOk())return 311;
    if(!waveFile.copyFileTo(batteryHatDirectory.getChildFile("ClosedHH 606 1.wav"))
        ||!waveFile.copyFileTo(batteryCymbalDirectory.getChildFile("China Gong V1.wav"))
        ||!waveFile.copyFileTo(batteryCymbalDirectory.getChildFile("China Gothik.wav")))return 312;
    const auto batteryClassifiedSamples=scanner.scan(batteryClassificationRoot);std::map<std::string,std::string> batteryCategories;
    for(const auto& sample:batteryClassifiedSamples)batteryCategories.emplace(sample.file.getFileName().toStdString(),sample.category.toStdString());
    if(batteryCategories.size()!=3||batteryCategories["ClosedHH 606 1.wav"]!="Closed hi-hat"
        ||batteryCategories["China Gong V1.wav"]!="Percussion"||batteryCategories["China Gothik.wav"]!="Crash")return 313;
    SampleDatabase classificationTagDb;if(!classificationTagDb.open(classificationTagRoot.getChildFile("classification-tags.sqlite3"))||!classificationTagDb.insertSample(classifiedSamples.getFirst()))return 152;
    const auto storedClassificationTags=classificationTagDb.getClassificationTags(classifiedFile);if(!storedClassificationTags.contains("808")||!storedClassificationTags.contains("Electronic")||!storedClassificationTags.contains("Deep"))return 153;
    SampleBrowseFilter classificationTagFilter;classificationTagFilter.query="Electronic";const auto classifierTagSearch=classificationTagDb.searchSampleBrowseRecords(classificationTagFilter,10,0);if(classifierTagSearch.records.size()!=1||!classifierTagSearch.records[0].classificationTags.contains("Electronic"))return 154;
    if(classificationTagDb.searchSampleBrowseRecords("Deep",juce::String(),10,0).size()!=1)return 155;
    juce::StringArray manualTags;manualTags.add("Reviewed");if(!classificationTagDb.setUserTags(classifiedFile,manualTags)||!classificationTagDb.setUserCategory(classifiedFile,"Snare"))return 156;
    const auto contentFilterRoot=root.getSiblingFile("MagDrumContentFilterTest");contentFilterRoot.deleteRecursively();if(!contentFilterRoot.createDirectory().wasOk())return 243;
    SampleDatabase contentFilterDb;if(!contentFilterDb.open(contentFilterRoot.getChildFile("content-filter.sqlite3")))return 243;
    const auto addContentFilterFixture=[&](const juce::String& name,const juce::String& category,double duration,int transientCount,const juce::String& tag)
    {
        const auto file=contentFilterRoot.getChildFile(name);
        if(!file.getParentDirectory().isDirectory()&&!file.getParentDirectory().createDirectory().wasOk())return false;
        if(!waveFile.copyFileTo(file))return false;
        auto sample=classifiedSamples.getFirst();sample.file=file;sample.category=category;sample.durationSeconds=duration;sample.transientCount=transientCount;sample.classificationTags.clear();
        if(tag.isNotEmpty())sample.classificationTags.add(tag);
        return contentFilterDb.insertSample(sample);
    };
    if(!addContentFilterFixture("single-kick.wav","Kick",0.2,1,{})
        ||!addContentFilterFixture("bass-drum-one-shot.wav","Kick",0.2,1,{})
        ||!addContentFilterFixture("categorized-rimshot.wav","Rimshot",0.25,1,{})
        ||!addContentFilterFixture("side-stick-one-shot.wav","Unknown",0.25,1,{})
        ||!addContentFilterFixture("drum-break-loop.wav","Unknown",8.0,16,"Loop")
        ||!addContentFilterFixture("bass-drum-loop.wav","Unknown",8.0,16,"Loop")
        ||!addContentFilterFixture("bass-note-one-shot.wav","Unknown",0.5,1,{})
        ||!addContentFilterFixture("bass-beat-loop.wav","Unknown",4.0,8,"Loop")
        ||!addContentFilterFixture("synth-pad-one-shot.wav","Unknown",2.4,1,{})
        ||!addContentFilterFixture("synth-pad-loop.wav","Unknown",4.0,8,"Loop")
        ||!addContentFilterFixture("synth-bass-loop.wav","Unknown",4.0,8,"Loop")
        ||!addContentFilterFixture("orchestral-phrase-loop.wav","Unknown",4.0,8,"Loop")
        ||!addContentFilterFixture("multi-sampled-loop.wav","Unknown",4.0,8,"Loop")
        ||!addContentFilterFixture("Beat Agent\\kick-agent-fixture.wav","Kick",0.2,1,{}))return 243;
    const auto addUserTaggedContentFixture=[&](const juce::String& name,const juce::String& tag)
    {
        const auto file=contentFilterRoot.getChildFile(name);
        if(!waveFile.copyFileTo(file))return false;
        auto sample=classifiedSamples.getFirst();sample.file=file;sample.category="Unknown";
        sample.durationSeconds=0.2;sample.transientCount=1;sample.classificationTags.clear();
        return contentFilterDb.insertSample(sample)&&contentFilterDb.setUserTags(file,juce::StringArray{tag});
    };
    if(!addUserTaggedContentFixture("neutral-a.wav","Drum loop")
        ||!addUserTaggedContentFixture("neutral-b.wav","Bass beat"))return 245;
    const auto hasContentFixture=[&](int contentKind,const juce::String& expected)
    {
        SampleBrowseFilter filter;filter.contentKind=contentKind;
        const auto page=contentFilterDb.searchSampleBrowseRecords(filter,20,0);
        return std::any_of(page.records.begin(),page.records.end(),[&expected](const auto& sample){return sample.file.getFileName()==expected;});
    };
    const auto expectContentFixture = [&hasContentFixture](int kind, const juce::String& name, bool expected)
    {
        const bool actual = hasContentFixture(kind, name);
        if (actual != expected) std::cerr << "Content filter " << kind << " expected " << name << " present=" << expected << " actual=" << actual << '\n';
        return actual == expected;
    };
    if(!expectContentFixture(1,"single-kick.wav",true)||!expectContentFixture(1,"bass-drum-one-shot.wav",true)
        ||!expectContentFixture(1,"categorized-rimshot.wav",true)||!expectContentFixture(1,"side-stick-one-shot.wav",true)
        ||!expectContentFixture(1,"drum-break-loop.wav",false)||!expectContentFixture(1,"neutral-a.wav",false)
        ||!expectContentFixture(1,"kick-agent-fixture.wav",true)||!expectContentFixture(2,"kick-agent-fixture.wav",false)
        ||!expectContentFixture(2,"neutral-a.wav",true)
        ||!expectContentFixture(2,"categorized-rimshot.wav",false)||!expectContentFixture(2,"side-stick-one-shot.wav",false)
        ||!expectContentFixture(2,"drum-break-loop.wav",true)||!expectContentFixture(2,"bass-drum-loop.wav",true)||!expectContentFixture(2,"single-kick.wav",false)
        ||!expectContentFixture(3,"bass-note-one-shot.wav",true)||!expectContentFixture(3,"bass-beat-loop.wav",false)
        ||!expectContentFixture(3,"bass-drum-one-shot.wav",false)||!expectContentFixture(3,"bass-drum-loop.wav",false)
        ||!expectContentFixture(3,"neutral-b.wav",false)
        ||!expectContentFixture(4,"bass-beat-loop.wav",true)||!expectContentFixture(4,"synth-bass-loop.wav",true)
        ||!expectContentFixture(4,"bass-note-one-shot.wav",false)||!expectContentFixture(4,"drum-break-loop.wav",false)
        ||!expectContentFixture(4,"neutral-b.wav",true)
        ||!expectContentFixture(5,"synth-pad-one-shot.wav",true)||!expectContentFixture(5,"synth-pad-loop.wav",false)
        ||!expectContentFixture(6,"synth-pad-loop.wav",true)||!expectContentFixture(7,"orchestral-phrase-loop.wav",true)
        ||!expectContentFixture(7,"multi-sampled-loop.wav",true))
    {
        for (int kind = 1; kind <= 7; ++kind)
        {
            SampleBrowseFilter debugFilter; debugFilter.contentKind = kind;
            const auto page = contentFilterDb.searchSampleBrowseRecords(debugFilter, 20, 0);
            std::cerr << "Content filter " << kind << ':';
            for (const auto& record : page.records) std::cerr << ' ' << record.file.getFileName();
            std::cerr << '\n';
        }
        return 244;
    }
    auto updatedClassification=classifiedSamples.getFirst();updatedClassification.classificationTags.clear();updatedClassification.classificationTags.add("Bright");const auto preservedManualTags=classificationTagDb.getUserTags(classifiedFile);if(!classificationTagDb.insertSample(updatedClassification)||classificationTagDb.getClassificationTags(classifiedFile).size()!=1||!classificationTagDb.getClassificationTags(classifiedFile).contains("Bright")||preservedManualTags.size()!=1||!preservedManualTags.contains("Reviewed")||classificationTagDb.getCategory(classifiedFile)!="Snare")return 157;
    classificationTagFilter.query="Electronic";if(!classificationTagDb.searchSampleBrowseRecords(classificationTagFilter,10,0).records.isEmpty())return 158;classificationTagFilter.query="Reviewed";if(classificationTagDb.searchSampleBrowseRecords(classificationTagFilter,10,0).records.size()!=1)return 159;
    auto priorClassifierVersion=updatedClassification;priorClassifierVersion.analysisVersion=currentSampleAnalysisVersion-1;priorClassifierVersion.classificationTags.clear();if(!classificationTagDb.insertSample(priorClassifierVersion)||classificationTagDb.isSampleUnchanged(classifiedFile,static_cast<std::int64_t>(classifiedFile.getSize()),classifiedFile.getLastModificationTime().toMilliseconds()))return 171;
    WavScanner::Stats refreshedClassificationStats;const auto refreshedClassificationScan=scanner.scan(classificationTagRoot,{},[&classificationTagDb](const juce::File& file,std::int64_t size,std::int64_t modified){return classificationTagDb.isSampleUnchanged(file,size,modified);},&refreshedClassificationStats,[&classificationTagDb](const ScannedSample& sample){return classificationTagDb.insertSample(sample);});
    const auto refreshedManualTags=classificationTagDb.getUserTags(classifiedFile);if(!refreshedClassificationScan.isEmpty()||refreshedClassificationStats.filesAnalyzed!=1||refreshedClassificationStats.filesSkippedUnchanged!=0||!classificationTagDb.getClassificationTags(classifiedFile).contains("Deep")||!refreshedManualTags.contains("Reviewed")||classificationTagDb.getCategory(classifiedFile)!="Snare")return 172;

    const auto acousticRoot=root.getSiblingFile("MagDrumAcousticFingerprintTest");acousticRoot.deleteRecursively();if(!acousticRoot.createDirectory().wasOk())return 57;
    const auto writeTone=[&](const juce::File& file,double frequency,float amplitude){auto output=file.createOutputStream();if(!output)return false;std::unique_ptr<juce::AudioFormatWriter> toneWriter(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));if(!toneWriter)return false;output.release();float tone[4096]{};for(int i=0;i<4096;++i)tone[i]=amplitude*std::sin(2.0*juce::MathConstants<double>::pi*frequency*i/44100.0);const float* data[]{tone};return toneWriter->writeFromFloatArrays(data,1,4096);};
    const auto writeNoiseHit=[&](const juce::File& file,double duration,double decaySeconds){const int count=static_cast<int>(std::round(duration*44100.0));std::vector<float> audio(static_cast<size_t>(count));std::uint32_t random=0x12345678u;for(int i=0;i<count;++i){random=random*1664525u+1013904223u;const float noise=static_cast<float>((static_cast<double>(random)/4294967295.0)*2.0-1.0);const double envelope=std::exp(-(static_cast<double>(i)/44100.0)/decaySeconds);audio[static_cast<size_t>(i)]=static_cast<float>(noise*0.8*envelope);}auto output=file.createOutputStream();if(!output)return false;std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));if(!writer)return false;output.release();const float* data[]{audio.data()};return writer->writeFromFloatArrays(data,1,count);};
    const auto writeTomHit=[&](const juce::File& file,double duration,double fundamental){const int count=static_cast<int>(std::round(duration*44100.0));std::vector<float> audio(static_cast<size_t>(count));for(int i=0;i<count;++i){const double time=static_cast<double>(i)/44100.0;const double envelope=std::exp(-time/0.34)*(1.0-std::exp(-time/0.002));const double tone=0.5*std::sin(2.0*juce::MathConstants<double>::pi*fundamental*time)+0.3*std::sin(2.0*juce::MathConstants<double>::pi*fundamental*2.0*time)+0.2*std::sin(2.0*juce::MathConstants<double>::pi*fundamental*3.0*time);audio[static_cast<size_t>(i)]=static_cast<float>(0.8*envelope*tone);}auto output=file.createOutputStream();if(!output)return false;std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));if(!writer)return false;output.release();const float* data[]{audio.data()};return writer->writeFromFloatArrays(data,1,count);};
    const auto writeTransientPattern=[&](const juce::File& file,double duration,int pulseCount,double pulseInterval,double pulseWidth){const int count=static_cast<int>(std::round(duration*44100.0));std::vector<float> audio(static_cast<size_t>(count));std::uint32_t random=0x9e3779b9u;for(int i=0;i<count;++i){const double time=static_cast<double>(i)/44100.0;const int pulse=static_cast<int>(std::floor(time/pulseInterval));const double withinPulse=time-pulse*pulseInterval;const bool active=pulse<pulseCount&&withinPulse<pulseWidth;random=random*1664525u+1013904223u;const double noise=(static_cast<double>(random)/4294967295.0)*2.0-1.0;audio[static_cast<size_t>(i)]=active?static_cast<float>(0.8*noise):0.0f;}auto output=file.createOutputStream();if(!output)return false;std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));if(!writer)return false;output.release();const float* data[]{audio.data()};return writer->writeFromFloatArrays(data,1,count);};
    const auto writeClapHit=[&](const juce::File& file,double duration){return writeTransientPattern(file,duration,4,0.024,0.008);};
    const auto noiseClassificationRoot=root.getSiblingFile("MagDrumNoiseClassificationTest");noiseClassificationRoot.deleteRecursively();if(!noiseClassificationRoot.createDirectory().wasOk())return 198;
    const auto shortNoiseFile=noiseClassificationRoot.getChildFile("transient-short.wav"),longNoiseFile=noiseClassificationRoot.getChildFile("transient-long.wav");if(!writeNoiseHit(shortNoiseFile,0.18,0.055)||!writeNoiseHit(longNoiseFile,0.6,0.22))return 199;
    const auto noiseSamples=scanner.scan(noiseClassificationRoot);if(noiseSamples.size()!=2)return 200;
    bool foundShortNoise=false,foundLongNoise=false;for(const auto& noiseSample:noiseSamples){if(noiseSample.spectralFlatness<0.55||noiseSample.spectralCentroidHz<1800.0||noiseSample.transientStrength<noiseSample.rms*0.45)return 201;if(noiseSample.file==shortNoiseFile)foundShortNoise=noiseSample.category=="Closed hi-hat";if(noiseSample.file==longNoiseFile)foundLongNoise=noiseSample.category=="Open hi-hat";}
    if(!foundShortNoise||!foundLongNoise)return 202;
    SampleDatabase noiseDb;if(!noiseDb.open(noiseClassificationRoot.getChildFile("samples.sqlite3")))return 203;for(const auto& noiseSample:noiseSamples)if(!noiseDb.insertSample(noiseSample))return 204;
    sqlite3* flatnessDb=nullptr;if(sqlite3_open(noiseClassificationRoot.getChildFile("samples.sqlite3").getFullPathName().toRawUTF8(),&flatnessDb)!=SQLITE_OK)return 205;sqlite3_stmt* flatnessQuery=nullptr;if(sqlite3_prepare_v2(flatnessDb,"SELECT count(*),min(spectral_flatness),max(spectral_flatness) FROM samples WHERE analysis_version=?",-1,&flatnessQuery,nullptr)!=SQLITE_OK)return 206;sqlite3_bind_int(flatnessQuery,1,currentSampleAnalysisVersion);const bool flatnessPersisted=sqlite3_step(flatnessQuery)==SQLITE_ROW&&sqlite3_column_int(flatnessQuery,0)==2&&sqlite3_column_double(flatnessQuery,1)>=0.55;sqlite3_finalize(flatnessQuery);sqlite3_close(flatnessDb);if(!flatnessPersisted)return 207;
    const auto tomRoot=root.getSiblingFile("MagDrumTomClassificationTest");tomRoot.deleteRecursively();if(!tomRoot.createDirectory().wasOk())return 210;const auto tomFile=tomRoot.getChildFile("pitched-tom.wav");if(!writeTomHit(tomFile,0.55,180.0))return 211;const auto tomSamples=scanner.scan(tomRoot);if(tomSamples.size()!=1)return 212;const auto& scannedTom=tomSamples.getFirst();if(scannedTom.pitchConfidence<0.65||scannedTom.fundamentalHz<125.0||scannedTom.fundamentalHz>380.0||scannedTom.spectralCentroidHz<300.0||scannedTom.spectralCentroidHz>=1600.0||scannedTom.transientStrength<scannedTom.rms*0.45||scannedTom.category!="Tom")return 213;
    const auto acousticArticulationRoot=root.getSiblingFile("MagDrumArticulationCandidateTest");acousticArticulationRoot.deleteRecursively();if(!acousticArticulationRoot.createDirectory().wasOk())return 214;const auto flamFile=acousticArticulationRoot.getChildFile("two-stroke.wav"),rollFile=acousticArticulationRoot.getChildFile("six-stroke.wav"),singleFile=acousticArticulationRoot.getChildFile("single-hit.wav");if(!writeTransientPattern(flamFile,0.24,2,0.04,0.008)||!writeTransientPattern(rollFile,0.40,6,0.05,0.008)||!writeTransientPattern(singleFile,0.18,1,0.04,0.008))return 215;const auto articulationSamples=scanner.scan(acousticArticulationRoot);if(articulationSamples.size()!=3)return 216;bool foundFlam=false,foundRoll=false,foundNoArticulation=false;for(const auto& sample:articulationSamples){const bool hasFlam=sample.classificationTags.contains("Articulation candidate: Flam"),hasRoll=sample.classificationTags.contains("Articulation candidate: Roll");if(sample.file==flamFile)foundFlam=sample.transientCount==2&&hasFlam&&!hasRoll;if(sample.file==rollFile)foundRoll=sample.transientCount==6&&hasRoll&&!hasFlam;if(sample.file==singleFile)foundNoArticulation=sample.transientCount==1&&!hasFlam&&!hasRoll;}if(!foundFlam||!foundRoll||!foundNoArticulation){for(const auto& sample:articulationSamples)std::cerr<<sample.file.getFileName()<<" attacks="<<sample.transientCount<<" flam="<<sample.classificationTags.contains("Articulation candidate: Flam")<<" roll="<<sample.classificationTags.contains("Articulation candidate: Roll")<<"\n";return 217;}SampleDatabase acousticArticulationDb;if(!acousticArticulationDb.open(acousticArticulationRoot.getChildFile("articulation.sqlite3")))return 218;for(const auto& sample:articulationSamples)if(!acousticArticulationDb.insertSample(sample))return 219;
    const auto clapRoot=root.getSiblingFile("MagDrumClapClassificationTest");clapRoot.deleteRecursively();if(!clapRoot.createDirectory().wasOk())return 221;const auto clapFile=clapRoot.getChildFile("four-pulse-hit.wav");if(!writeClapHit(clapFile,0.24))return 222;const auto clapSamples=scanner.scan(clapRoot);if(clapSamples.size()!=1)return 223;const auto& scannedClap=clapSamples.getFirst();if(scannedClap.transientCount<3||scannedClap.spectralCentroidHz<1800.0||scannedClap.spectralFlatness<0.20||scannedClap.classificationTags.contains("Articulation candidate: Roll")||scannedClap.category!="Clap"||scannedClap.classificationConfidence>=0.75){std::cerr<<"Clap fixture: attacks="<<scannedClap.transientCount<<", centroid="<<scannedClap.spectralCentroidHz<<", flatness="<<scannedClap.spectralFlatness<<", transient="<<scannedClap.transientStrength<<", rms="<<scannedClap.rms<<", category="<<scannedClap.category<<", confidence="<<scannedClap.classificationConfidence<<"\n";return 224;}SampleDatabase clapDb;if(!clapDb.open(clapRoot.getChildFile("clap.sqlite3"))||!clapDb.insertSample(scannedClap))return 225;sqlite3* clapConnection=nullptr;if(sqlite3_open(clapRoot.getChildFile("clap.sqlite3").getFullPathName().toRawUTF8(),&clapConnection)!=SQLITE_OK)return 226;sqlite3_stmt* clapMeasurementQuery=nullptr;if(sqlite3_prepare_v2(clapConnection,"SELECT transient_count,category FROM samples WHERE path=?",-1,&clapMeasurementQuery,nullptr)!=SQLITE_OK)return 227;sqlite3_bind_text(clapMeasurementQuery,1,clapFile.getFullPathName().toRawUTF8(),-1,SQLITE_TRANSIENT);const bool clapMeasurementStored=sqlite3_step(clapMeasurementQuery)==SQLITE_ROW&&sqlite3_column_int(clapMeasurementQuery,0)>=3&&std::strcmp(reinterpret_cast<const char*>(sqlite3_column_text(clapMeasurementQuery,1)),"Clap")==0;sqlite3_finalize(clapMeasurementQuery);sqlite3_close(clapConnection);if(!clapMeasurementStored)return 228;
    const auto gainFile=acousticRoot.getChildFile("gain.wav"),pitchFile=acousticRoot.getChildFile("pitch.wav");
    if(!writeTone(gainFile,440.0,0.25f)||!writeTone(pitchFile,880.0,0.5f))return 58;
    const auto acousticScans=scanner.scan(acousticRoot);if(acousticScans.size()!=2)return 59;
    ScannedSample gainSample,pitchSample;for(const auto& item:acousticScans){if(item.file==gainFile)gainSample=item;else if(item.file==pitchFile)pitchSample=item;}
    if(gainSample.acousticFingerprint!=scanned.getFirst().acousticFingerprint||gainSample.fingerprint==scanned.getFirst().fingerprint)return 60;
    if(pitchSample.acousticFingerprint==scanned.getFirst().acousticFingerprint)return 61;

    const auto multiWindowRoot=root.getSiblingFile("MagDrumMultiWindowAnalysisTest");multiWindowRoot.deleteRecursively();if(!multiWindowRoot.createDirectory().wasOk())return 79;
    const auto changingFile=multiWindowRoot.getChildFile("changing-spectrum.wav");auto changingOutput=changingFile.createOutputStream();if(!changingOutput)return 80;std::unique_ptr<juce::AudioFormatWriter> changingWriter(wav.createWriterFor(changingOutput.get(),44100.0,1u,16,{},0));if(!changingWriter)return 81;changingOutput.release();std::vector<float> changingTone(16384);const double changingFrequencies[]{440.0,880.0,1760.0,3520.0};for(size_t segment=0;segment<4;++segment)for(int i=0;i<4096;++i)changingTone[segment*4096+static_cast<size_t>(i)]=0.4f*static_cast<float>(std::sin(2.0*juce::MathConstants<double>::pi*changingFrequencies[segment]*i/44100.0));const float* changingData[]{changingTone.data()};if(!changingWriter->writeFromFloatArrays(changingData,1,static_cast<int>(changingTone.size())))return 82;changingWriter.reset();
    const auto multiWindowScans=scanner.scan(multiWindowRoot);if(multiWindowScans.size()!=1||multiWindowScans.getFirst().analysisVersion!=currentSampleAnalysisVersion||multiWindowScans.getFirst().spectralCentroidHz<1300.0||multiWindowScans.getFirst().spectralCentroidHz>2000.0)return 83;

    const auto dbFile=root.getChildFile("index.sqlite3");
    const auto legacyDbFile=root.getChildFile("legacy.sqlite3");sqlite3* legacy=nullptr;
    if(sqlite3_open(legacyDbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 36;
    const auto legacyPath=waveFile.getFullPathName();const auto legacySql=
        "CREATE TABLE samples(path TEXT PRIMARY KEY,name TEXT NOT NULL,duration REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,size_bytes INTEGER NOT NULL DEFAULT 0,modified_time_ms INTEGER NOT NULL DEFAULT 0); "
        "INSERT INTO samples(path,name,duration,channels,sample_rate,size_bytes,modified_time_ms) VALUES('"+legacyPath.replace("'","''")+"','kick.wav',0.1,1,44100,"+juce::String(static_cast<juce::int64>(waveFile.getSize()))+","+juce::String(waveFile.getLastModificationTime().toMilliseconds())+"); "
        "CREATE TABLE schema_info(singleton INTEGER PRIMARY KEY CHECK(singleton=1),version INTEGER NOT NULL); INSERT INTO schema_info(singleton,version) VALUES(1,7); "
        "CREATE TABLE plugin_sounds(id INTEGER PRIMARY KEY,kit_id INTEGER NOT NULL,midi_note INTEGER NOT NULL,velocity INTEGER NOT NULL,articulation TEXT NOT NULL DEFAULT '',silent INTEGER NOT NULL DEFAULT 0,preview_path TEXT NOT NULL DEFAULT '',duration REAL NOT NULL DEFAULT 0,sample_rate REAL NOT NULL DEFAULT 0,channels INTEGER NOT NULL DEFAULT 0,peak REAL NOT NULL DEFAULT 0,rms REAL NOT NULL DEFAULT 0,spectral_centroid REAL NOT NULL DEFAULT 0,transient_strength REAL NOT NULL DEFAULT 0,fundamental_hz REAL NOT NULL DEFAULT 0,pitch_confidence REAL NOT NULL DEFAULT 0,fingerprint TEXT NOT NULL DEFAULT '',category TEXT NOT NULL DEFAULT 'Unknown',classification_confidence REAL NOT NULL DEFAULT 0,UNIQUE(kit_id,midi_note,velocity,articulation)); "
        "INSERT INTO plugin_sounds(kit_id,midi_note,velocity) VALUES(1,36,100);"
        "CREATE TABLE sample_scan_history(id INTEGER PRIMARY KEY,root_path TEXT NOT NULL,started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,finished_at TEXT,status TEXT NOT NULL DEFAULT 'running',files_considered INTEGER NOT NULL DEFAULT 0,files_skipped INTEGER NOT NULL DEFAULT 0,files_indexed INTEGER NOT NULL DEFAULT 0,errors INTEGER NOT NULL DEFAULT 0);"
        "INSERT INTO sample_scan_history(root_path,status) VALUES('"+root.getFullPathName().replace("'","''")+"','completed');";
    const bool legacyCreated=sqlite3_exec(legacy,legacySql.toRawUTF8(),nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(legacy);if(!legacyCreated)return 37;
    SampleDatabase migrated;if(!migrated.open(legacyDbFile)||migrated.getAllPaths().size()!=1||!migrated.setUserCategory(waveFile,"Kick"))return 38;const auto migratedBrowse=migrated.getSampleBrowseRecords();if(migratedBrowse.size()!=1||migratedBrowse.getFirst().sourceType!="folder"||migratedBrowse.getFirst().sourceIdentifier!=root.getFullPathName())return 109;const auto migratedSearch=migrated.searchSampleBrowseRecords(waveFile.getFileName(),juce::String(),10,0);if(migratedSearch.size()!=1||migratedSearch.getFirst().file!=waveFile)return 223;
    if(migrated.isSampleUnchanged(waveFile,static_cast<std::int64_t>(waveFile.getSize()),waveFile.getLastModificationTime().toMilliseconds()))return 63;
    if(sqlite3_open(legacyDbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 90;sqlite3_stmt* migratedColumns=nullptr;if(sqlite3_prepare_v2(legacy,"PRAGMA table_info(plugin_sounds)",-1,&migratedColumns,nullptr)!=SQLITE_OK)return 91;bool hasAcousticFingerprint=false,hasPluginSpectralFlatness=false,hasPluginTransientCount=false;while(sqlite3_step(migratedColumns)==SQLITE_ROW){const auto* columnName=reinterpret_cast<const char*>(sqlite3_column_text(migratedColumns,1));if(columnName!=nullptr&&std::strcmp(columnName,"acoustic_fingerprint")==0)hasAcousticFingerprint=true;if(columnName!=nullptr&&std::strcmp(columnName,"spectral_flatness")==0)hasPluginSpectralFlatness=true;if(columnName!=nullptr&&std::strcmp(columnName,"transient_count")==0)hasPluginTransientCount=true;}sqlite3_finalize(migratedColumns);if(!hasAcousticFingerprint||!hasPluginSpectralFlatness||!hasPluginTransientCount)return 92;
    sqlite3_stmt* migratedVersion=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT version FROM schema_info WHERE singleton=1",-1,&migratedVersion,nullptr)!=SQLITE_OK)return 93;const bool schemaUpgraded=sqlite3_step(migratedVersion)==SQLITE_ROW&&sqlite3_column_int(migratedVersion,0)==19;sqlite3_finalize(migratedVersion);sqlite3_close(legacy);if(!schemaUpgraded)return 94;
    SampleDatabase db;
    if(!db.open(dbFile))return 8;
    const auto resumeRoot=root.getSiblingFile("MagDrumInterruptedScanTest");resumeRoot.deleteRecursively();if(!resumeRoot.createDirectory().wasOk())return 170;
    if(!waveFile.copyFileTo(resumeRoot.getChildFile("01-first.wav"))||!waveFile.copyFileTo(resumeRoot.getChildFile("02-second.wav")))return 171;
    const auto resumeDbFile=resumeRoot.getChildFile("resume.sqlite3");
    {
        SampleDatabase firstRunDb;if(!firstRunDb.open(resumeDbFile))return 172;
        const auto resumeSource=firstRunDb.ensureSampleSource("folder",resumeRoot.getFullPathName(),resumeRoot.getFileName(),resumeRoot.getFullPathName());if(!resumeSource)return 173;
        const auto firstRunScan=firstRunDb.beginSampleScan(resumeRoot);if(!firstRunScan)return 174;
        int checksAfterFirstSample=0;std::int64_t firstRunIndexed=0;WavScanner::Stats firstRunStats;
        scanner.scan(resumeRoot,[&]{return firstRunIndexed>0&&++checksAfterFirstSample>=3;},{},&firstRunStats,[&](const ScannedSample& item){if(!firstRunDb.insertSample(item,*resumeSource))return false;++firstRunIndexed;return true;},{},[&](const juce::File& file){return firstRunDb.updateSampleScanCheckpoint(*firstRunScan,file);});
        if(!firstRunStats.cancelled||firstRunStats.filesAnalyzed!=1||firstRunIndexed!=1||firstRunDb.getAllPaths().size()!=1)return 175;
    }
    SampleDatabase resumedDb;if(!resumedDb.open(resumeDbFile)||resumedDb.markRunningSampleScansInterrupted()!=1)return 176;
    const auto resumedHistory=resumedDb.getRecentSampleScans(1);if(resumedHistory.size()!=1||resumedHistory.getFirst().status!="interrupted")return 177;
    const auto checkpoint=resumedDb.getResumableSampleScanPath(resumeRoot);if(checkpoint!=resumeRoot.getChildFile("01-first.wav").getFullPathName())return 180;
    const auto resumeSource=resumedDb.ensureSampleSource("folder",resumeRoot.getFullPathName(),resumeRoot.getFileName(),resumeRoot.getFullPathName());const auto resumedScan=resumeSource?resumedDb.beginSampleScan(resumeRoot,checkpoint):std::optional<std::int64_t>();if(!resumeSource||!resumedScan)return 178;
    WavScanner::Stats resumedStats;std::int64_t resumedIndexed=0;
    scanner.scan(resumeRoot,{},[&](const juce::File& file,std::int64_t size,std::int64_t modified){return resumedDb.isSampleUnchanged(file,size,modified);},&resumedStats,[&](const ScannedSample& item){if(!resumedDb.insertSample(item,*resumeSource))return false;++resumedIndexed;return true;},checkpoint,[&](const juce::File& file){return resumedDb.updateSampleScanCheckpoint(*resumedScan,file);});
    if(resumedStats.filesConsidered!=1||resumedStats.filesSkippedUnchanged!=0||resumedStats.filesAnalyzed!=1||resumedStats.errors!=0||resumedIndexed!=1||resumedDb.getAllPaths().size()!=2)return 179;
    PluginInstrumentRecord instrument{"vst3:fixture:drums","VST3","Fixture Drums","MagDrum Test","Instrument",root.getFullPathName(),"1.0"};
    if(!db.upsertPluginInstrument(instrument)||!db.upsertPluginInstrument(instrument))return 31;
    const auto instruments=db.getPluginInstruments();
    if(instruments.size()!=1||instruments.getFirst().identifier!=instrument.identifier||instruments.getFirst().name!=instrument.name)return 32;
    juce::MemoryBlock pluginState;const std::uint8_t stateBytes[]{1,2,3};pluginState.append(stateBytes,sizeof(stateBytes));
    const auto kitId=db.savePluginKit(instrument.identifier,"Test Kit",pluginState);
    if(!kitId||db.savePluginKit(instrument.identifier,"Test Kit",pluginState)!=kitId)return 33;
    if(db.getPluginKitNames().size()!=1||db.getPluginKitNames()[0]!="Fixture Drums / Test Kit")return 158;
    const auto kitsForInstrument=db.getPluginKitNames(instrument.identifier);if(kitsForInstrument.size()!=1||kitsForInstrument[0]!="Test Kit"||!db.getPluginKitNames("vst3:missing:instrument").isEmpty())return 218;
    if(db.getPluginKitId(instrument.identifier,"Test Kit")!=kitId||db.getPluginKitId(instrument.identifier,"Missing Kit"))return 141;
    const auto restoredKitState=db.loadPluginKitState(instrument.identifier,"Test Kit");if(!restoredKitState||restoredKitState->getSize()!=sizeof(stateBytes)||std::memcmp(restoredKitState->getData(),stateBytes,sizeof(stateBytes))!=0)return 42;
    PluginSoundRecord generatedSound;generatedSound.midiNote=36;generatedSound.velocity=100;generatedSound.articulation="Kick";generatedSound.previewPath=waveFile.getFullPathName();generatedSound.duration=0.4;generatedSound.sampleRate=44100;generatedSound.channels=2;generatedSound.peak=0.8;generatedSound.rms=0.2;generatedSound.spectralFlatness=0.73;generatedSound.transientCount=4;generatedSound.category="Kick";generatedSound.confidence=0.6;
    if(!db.upsertPluginSound(*kitId,generatedSound)||!db.upsertPluginSound(*kitId,generatedSound))return 34;
    if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 209;sqlite3_stmt* pluginFlatnessQuery=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT spectral_flatness,transient_count FROM plugin_sounds WHERE kit_id=? AND midi_note=36",-1,&pluginFlatnessQuery,nullptr)!=SQLITE_OK)return 210;sqlite3_bind_int64(pluginFlatnessQuery,1,*kitId);const bool pluginFlatnessStored=sqlite3_step(pluginFlatnessQuery)==SQLITE_ROW&&std::abs(sqlite3_column_double(pluginFlatnessQuery,0)-0.73)<0.0001&&sqlite3_column_int(pluginFlatnessQuery,1)==4;sqlite3_finalize(pluginFlatnessQuery);sqlite3_close(legacy);if(!pluginFlatnessStored)return 211;
    generatedSound.midiNote=128;if(db.upsertPluginSound(*kitId,generatedSound))return 35;
    juce::String lowFingerprint,highFingerprint;for(int segment=0;segment<4;++segment)for(int band=0;band<16;++band){lowFingerprint+=band==0?"ff":"00";highFingerprint+=band==15?"ff":"00";}
    PluginSoundRecord lowVelocity;lowVelocity.midiNote=38;lowVelocity.velocity=40;lowVelocity.acousticFingerprint=lowFingerprint;PluginSoundRecord highVelocity=lowVelocity;highVelocity.velocity=88;highVelocity.acousticFingerprint=highFingerprint;
    PluginVelocityLayerAnalyzer layerAnalyzer;const auto layerCandidates=layerAnalyzer.analyze({lowVelocity,highVelocity});if(layerCandidates.size()!=1||layerCandidates.front().midiNote!=38||layerCandidates.front().lowerVelocity!=40||layerCandidates.front().upperVelocity!=88||layerCandidates.front().spectralDistance<0.99)return 85;
    lowVelocity.articulation="Test articulation";highVelocity.articulation=lowVelocity.articulation;if(!db.upsertPluginSound(*kitId,lowVelocity)||!db.upsertPluginSound(*kitId,highVelocity))return 86;
    if(!db.replacePluginVelocityLayerCandidates(*kitId,layerCandidates))return 87;const auto storedLayerCandidates=db.getPluginVelocityLayerCandidates(*kitId);if(storedLayerCandidates.size()!=1||storedLayerCandidates.front().algorithmVersion!=PluginVelocityLayerAnalyzer::algorithmVersion)return 88;const auto recalledLayerCandidates=db.getPluginVelocityLayerCandidates(instrument.identifier,"Test Kit");if(recalledLayerCandidates.size()!=1||recalledLayerCandidates.front().upperVelocity!=88)return 89;
    const std::vector<PluginConfirmedVelocityLayerRange> confirmedLayers{{38,1,40},{38,41,127}};if(!db.replaceConfirmedPluginVelocityLayers(*kitId,confirmedLayers))return 135;const auto recalledLayers=db.getConfirmedPluginVelocityLayers(instrument.identifier,"Test Kit");if(recalledLayers.size()!=2||recalledLayers[0].minimumVelocity!=1||recalledLayers[0].maximumVelocity!=40||recalledLayers[1].minimumVelocity!=41||recalledLayers[1].maximumVelocity!=127)return 136;const auto overlappingLayers=std::vector<PluginConfirmedVelocityLayerRange>{{38,1,60},{38,60,127}};if(db.replaceConfirmedPluginVelocityLayers(*kitId,overlappingLayers)||db.getConfirmedPluginVelocityLayers(instrument.identifier,"Test Kit").size()!=2)return 137;
    PluginRoundRobinTake rrTakeA;rrTakeA.midiNote=36;rrTakeA.velocity=100;rrTakeA.takeIndex=1;rrTakeA.previewPath=waveFile.getFullPathName();rrTakeA.acousticFingerprint=lowFingerprint;PluginRoundRobinTake rrTakeB=rrTakeA;rrTakeB.takeIndex=2;rrTakeB.acousticFingerprint=highFingerprint;const auto rrCandidates=PluginRoundRobinAnalyzer{}.analyze({rrTakeA,rrTakeB});if(rrCandidates.size()!=1||rrCandidates.front().distinctVariants!=2||rrCandidates.front().takeCount!=2)return 100;const auto identicalRr=PluginRoundRobinAnalyzer{}.analyze({rrTakeA,rrTakeA});if(!identicalRr.empty())return 101;if(!db.replacePluginRoundRobinData(*kitId,{rrTakeA,rrTakeB},rrCandidates))return 102;const auto storedRr=db.getPluginRoundRobinCandidates(instrument.identifier,"Test Kit");if(storedRr.size()!=1||storedRr.front().takeCount!=2||storedRr.front().algorithmVersion!=PluginRoundRobinAnalyzer::algorithmVersion)return 103;if(!db.confirmPluginRoundRobinGroup(*kitId,36,100))return 138;const auto confirmedRr=db.getConfirmedPluginRoundRobinGroups(instrument.identifier,"Test Kit");if(confirmedRr.size()!=1||confirmedRr.front().midiNote!=36||confirmedRr.front().velocity!=100||confirmedRr.front().takeIndices!=std::vector<int>{1,2})return 139;if(db.confirmPluginRoundRobinGroup(*kitId,36,99)||!db.removeConfirmedPluginRoundRobinGroup(*kitId,36,100)||!db.getConfirmedPluginRoundRobinGroups(instrument.identifier,"Test Kit").empty())return 140;if(!db.confirmPluginRoundRobinGroup(*kitId,36,100)||!db.replacePluginRoundRobinData(*kitId,{},{} )||!db.getPluginRoundRobinCandidates(instrument.identifier,"Test Kit").empty()||!db.getConfirmedPluginRoundRobinGroups(instrument.identifier,"Test Kit").empty())return 104;
    if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 95;sqlite3_stmt* storedFingerprintQuery=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT count(*) FROM plugin_sounds WHERE kit_id=? AND midi_note=38 AND acoustic_fingerprint<>''",-1,&storedFingerprintQuery,nullptr)!=SQLITE_OK)return 96;sqlite3_bind_int64(storedFingerprintQuery,1,*kitId);const bool fingerprintsStored=sqlite3_step(storedFingerprintQuery)==SQLITE_ROW&&sqlite3_column_int(storedFingerprintQuery,0)==2;sqlite3_finalize(storedFingerprintQuery);sqlite3_close(legacy);if(!fingerprintsStored)return 97;
    auto invalidLayer=layerCandidates.front();invalidLayer.upperVelocity=128;if(db.replacePluginVelocityLayerCandidates(*kitId,{invalidLayer})||db.getPluginVelocityLayerCandidates(*kitId).size()!=1)return 98;
    if(!db.replacePluginVelocityLayerCandidates(*kitId,{})||!db.getPluginVelocityLayerCandidates(*kitId).empty())return 99;
    const auto& sample=scanned.getFirst();
    if(!db.insertSample(sample))return 9;
    if(!db.insertSample(sample))return 10;
    const auto initialBrowseRecords=db.getSampleBrowseRecords();if(initialBrowseRecords.size()!=1||std::abs(initialBrowseRecords.getFirst().classificationConfidence-sample.classificationConfidence)>0.0001||initialBrowseRecords.getFirst().manuallyClassified||needsClassificationReview(initialBrowseRecords.getFirst().manuallyClassified,initialBrowseRecords.getFirst().category,initialBrowseRecords.getFirst().classificationConfidence))return 129;
    const auto folderSourceId=db.ensureSampleSource("folder",root.getFullPathName(),root.getFileName(),root.getFullPathName());if(!folderSourceId||!db.assignSamplesToSource(root,*folderSourceId))return 107;const auto pluginSourceId=db.ensureSampleSource("plugin",instrument.identifier,instrument.name);if(!pluginSourceId||db.getSampleSources().size()!=2)return 108;
    const auto browseRecords=db.searchSampleBrowseRecords(SampleBrowseFilter{},10,0).records;if(browseRecords.size()!=1||browseRecords.getFirst().file!=sample.file||browseRecords.getFirst().durationSeconds<=0.0||browseRecords.getFirst().category.isEmpty()||browseRecords.getFirst().sourceType!="folder"||browseRecords.getFirst().sourceName!=root.getFileName()||browseRecords.getFirst().acousticFingerprint!=sample.acousticFingerprint)return 68;
    const auto pagedBrowseRoot=root.getSiblingFile("MagDrumPagedBrowseTest");pagedBrowseRoot.deleteRecursively();if(!pagedBrowseRoot.createDirectory().wasOk())return 145;SampleDatabase pagedBrowseDb;if(!pagedBrowseDb.open(pagedBrowseRoot.getChildFile("paged-browse.sqlite3")))return 145;
    const juce::StringArray browseNames{"Alpha.wav","Beta.wav","Delta.wav","Gamma.wav"};
    for(const auto& name:browseNames){const auto file=pagedBrowseRoot.getChildFile(name);if(!waveFile.copyFileTo(file))return 146;auto browseSample=sample;browseSample.file=file;browseSample.durationSeconds=name=="Alpha.wav"?0.05:name=="Beta.wav"?0.2:name=="Delta.wav"?0.8:2.5;browseSample.fileSizeBytes=file.getSize();browseSample.modifiedTimeMillis=file.getLastModificationTime().toMilliseconds();if(!pagedBrowseDb.insertSample(browseSample))return 147;}
    if(!pagedBrowseDb.setUserCategory(pagedBrowseRoot.getChildFile("Alpha.wav"),"Kick")||!pagedBrowseDb.setUserCategory(pagedBrowseRoot.getChildFile("Beta.wav"),"Snare")||!pagedBrowseDb.setUserCategory(pagedBrowseRoot.getChildFile("Delta.wav"),"Unknown")||!pagedBrowseDb.setUserCategory(pagedBrowseRoot.getChildFile("Gamma.wav"),"Kick"))return 148;
    juce::StringArray browseTags;browseTags.add("metal kit");if(!pagedBrowseDb.setUserTags(pagedBrowseRoot.getChildFile("Beta.wav"),browseTags))return 151;
    const auto browseSourceId=pagedBrowseDb.ensureSampleSource("folder",pagedBrowseRoot.getFullPathName(),"Paged Browse",pagedBrowseRoot.getFullPathName());if(!browseSourceId||!pagedBrowseDb.assignSamplesToSource(pagedBrowseRoot,*browseSourceId))return 152;
    const auto browsePageOne=pagedBrowseDb.searchSampleBrowseRecords({}, {}, 2, 0),browsePageTwo=pagedBrowseDb.searchSampleBrowseRecords({}, {}, 2, 2);if(browsePageOne.size()!=2||browsePageTwo.size()!=2||browsePageOne[0].file.getFileName()!="Alpha.wav"||browsePageOne[1].file.getFileName()!="Beta.wav"||browsePageTwo[0].file.getFileName()!="Delta.wav"||browsePageTwo[1].file.getFileName()!="Gamma.wav")return 149;
    const auto filteredBrowsePage=pagedBrowseDb.searchSampleBrowseRecords("a","kick",10,0);const auto emptyBrowsePage=pagedBrowseDb.searchSampleBrowseRecords({}, {}, 0, 0);if(filteredBrowsePage.size()!=2||filteredBrowsePage[0].file.getFileName()!="Alpha.wav"||filteredBrowsePage[1].file.getFileName()!="Gamma.wav"||!emptyBrowsePage.isEmpty()){std::cerr<<"Paged browse filter failed: size="<<filteredBrowsePage.size();for(const auto& item:filteredBrowsePage)std::cerr<<" ["<<item.file.getFileName()<<","<<item.category<<"]";std::cerr<<" zero-limit="<<emptyBrowsePage.size()<<"\n";return 150;}
    SampleBrowseFilter databasePageFilter;auto databasePageOne=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,2,0);auto databasePageTwo=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,2,2);if(databasePageOne.records.size()!=2||!databasePageOne.hasMore||databasePageTwo.records.size()!=2||databasePageTwo.hasMore||databasePageOne.records[0].file.getFileName()!="Alpha.wav"||databasePageTwo.records[1].file.getFileName()!="Gamma.wav")return 153;
    databasePageFilter.category="Kick";const auto kickDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(kickDatabasePage.records.size()!=2||kickDatabasePage.records[0].file.getFileName()!="Alpha.wav"||kickDatabasePage.records[1].file.getFileName()!="Gamma.wav")return 154;
    databasePageFilter.category.clear();databasePageFilter.query="metal";const auto tagDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(tagDatabasePage.records.size()!=1||tagDatabasePage.records[0].file.getFileName()!="Beta.wav")return 155;
    databasePageFilter.query="Alpha";const auto indexedNamePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(indexedNamePage.records.size()!=1||indexedNamePage.records[0].file.getFileName()!="Alpha.wav")return 218;
    databasePageFilter.query="Paged Browse";const auto indexedSourcePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(indexedSourcePage.records.size()!=4||indexedSourcePage.records[0].sourceName!="Paged Browse")return 219;
    databasePageFilter.query="a";const auto shortQueryPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(shortQueryPage.records.size()!=4)return 220;
    databasePageFilter.query.clear();databasePageFilter.sourceType="folder";databasePageFilter.sourceIdentifier=pagedBrowseRoot.getFullPathName();const auto sourceDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(sourceDatabasePage.records.size()!=4||sourceDatabasePage.records[0].sourceType!="folder")return 156;
    databasePageFilter.sourceType.clear();databasePageFilter.sourceIdentifier.clear();databasePageFilter.feature=2;const auto shortDurationPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(shortDurationPage.records.size()!=1||shortDurationPage.records[0].file.getFileName()!="Alpha.wav")return 159;
    databasePageFilter.feature=5;databasePageFilter.sort=3;const auto longDurationPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(longDurationPage.records.size()!=1||longDurationPage.records[0].file.getFileName()!="Gamma.wav")return 160;
    auto reviewSample=sample;reviewSample.file=pagedBrowseRoot.getChildFile("NeedsReview.wav");reviewSample.category="Unknown";reviewSample.classificationConfidence=0.2;reviewSample.fileSizeBytes=waveFile.getSize();reviewSample.modifiedTimeMillis=waveFile.getLastModificationTime().toMilliseconds();if(!waveFile.copyFileTo(reviewSample.file)||!pagedBrowseDb.insertSample(reviewSample))return 161;
    databasePageFilter.feature=1;databasePageFilter.sort=1;databasePageFilter.needsReview=true;const auto reviewDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(reviewDatabasePage.records.size()!=1||reviewDatabasePage.records[0].file.getFileName()!="NeedsReview.wav"||reviewDatabasePage.records[0].manuallyClassified)return 162;
    databasePageFilter.needsReview=false;databasePageFilter.sort=3;const auto durationSortedPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,2,0);if(durationSortedPage.records.size()!=2||durationSortedPage.records[0].file.getFileName()!="Gamma.wav"||durationSortedPage.records[1].file.getFileName()!="Delta.wav"||!durationSortedPage.hasMore)return 163;
    const auto largeBrowseFile=pagedBrowseRoot.getSiblingFile("MagDrumLargeBrowseTest.sqlite3");largeBrowseFile.deleteFile();SampleDatabase largeBrowseDb;if(!largeBrowseDb.open(largeBrowseFile))return 164;sqlite3* largeFixture=nullptr;if(sqlite3_open(largeBrowseFile.getFullPathName().toRawUTF8(),&largeFixture)!=SQLITE_OK)return 165;
    sqlite3_stmt* browseIndices=nullptr;if(sqlite3_prepare_v2(largeFixture,"SELECT count(*) FROM sqlite_master WHERE type='index' AND name IN ('samples_name_path','samples_duration_name_path','samples_centroid_name_path')",-1,&browseIndices,nullptr)!=SQLITE_OK)return 214;const bool hasBrowseIndices=sqlite3_step(browseIndices)==SQLITE_ROW&&sqlite3_column_int(browseIndices,0)==3;sqlite3_finalize(browseIndices);if(!hasBrowseIndices)return 215;
    sqlite3_stmt* nameBrowsePlan=nullptr;if(sqlite3_prepare_v2(largeFixture,"EXPLAIN QUERY PLAN SELECT path FROM samples ORDER BY name COLLATE NOCASE,path COLLATE NOCASE LIMIT 251",-1,&nameBrowsePlan,nullptr)!=SQLITE_OK)return 216;bool namePlanUsesIndex=false;while(sqlite3_step(nameBrowsePlan)==SQLITE_ROW){const auto* detail=reinterpret_cast<const char*>(sqlite3_column_text(nameBrowsePlan,3));if(detail!=nullptr&&std::strstr(detail,"samples_name_path")!=nullptr)namePlanUsesIndex=true;}sqlite3_finalize(nameBrowsePlan);if(!namePlanUsesIndex)return 217;
    if(sqlite3_exec(largeFixture,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK)return 166;sqlite3_stmt* largeInsert=nullptr;if(sqlite3_prepare_v2(largeFixture,"INSERT INTO samples(path,name,duration,channels,sample_rate,spectral_centroid,category,classification_confidence) VALUES(?,?,0.2,1,44100,1800,?,0.8)",-1,&largeInsert,nullptr)!=SQLITE_OK)return 167;bool largeInsertOk=true;for(int index=0;index<250000;++index){const auto name="Sample "+juce::String(index).paddedLeft('0',6);const auto path="/magdrum-large-fixture/"+name+".wav";sqlite3_reset(largeInsert);sqlite3_clear_bindings(largeInsert);sqlite3_bind_text(largeInsert,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(largeInsert,2,name.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(largeInsert,3,index%2==0?"Kick":"Snare",-1,SQLITE_STATIC);if(sqlite3_step(largeInsert)!=SQLITE_DONE){largeInsertOk=false;break;}}sqlite3_finalize(largeInsert);const bool largeCommit=sqlite3_exec(largeFixture,largeInsertOk?"COMMIT":"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK;if(!largeInsertOk||!largeCommit){sqlite3_close(largeFixture);return 168;}SampleBrowseFilter largeFilter;const auto largeFirst=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,0),largeMiddle=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,124750),largeLast=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,249750);if(largeFirst.records.size()!=250||!largeFirst.hasMore||largeFirst.records[0].file.getFileName()!="Sample 000000.wav"||largeMiddle.records.size()!=250||largeMiddle.records[0].file.getFileName()!="Sample 124750.wav"||largeLast.records.size()!=250||largeLast.hasMore||largeLast.records[0].file.getFileName()!="Sample 249750.wav"||largeLast.records[249].file.getFileName()!="Sample 249999.wav")return 169;const auto representativeMap=largeBrowseDb.searchSampleMapRecords(largeFilter,25);if(representativeMap.records.size()!=25||representativeMap.hasMore||representativeMap.records.getFirst().file.getFileName()!="Sample 000000.wav"||representativeMap.records[12].file.getFileName().startsWith("Sample 0")||representativeMap.records.getLast().file.getFileName()!="Sample 249999.wav")return 262;
    largeFilter.contentKind=1;const auto largeDrumHits=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,0);if(largeDrumHits.records.size()!=250||!largeDrumHits.hasMore||largeDrumHits.records.getFirst().category!="Kick")return 263;largeFilter.contentKind=0;
    sqlite3_stmt* ftsSearchPlan=nullptr;if(sqlite3_prepare_v2(largeFixture,"EXPLAIN QUERY PLAN SELECT rowid FROM sample_search_fts WHERE sample_search_fts MATCH '\"Sample 12475\"'",-1,&ftsSearchPlan,nullptr)!=SQLITE_OK){sqlite3_close(largeFixture);return 224;}bool usesFtsIndex=false;while(sqlite3_step(ftsSearchPlan)==SQLITE_ROW){const auto* detail=reinterpret_cast<const char*>(sqlite3_column_text(ftsSearchPlan,3));if(detail!=nullptr&&std::strstr(detail,"VIRTUAL TABLE INDEX")!=nullptr)usesFtsIndex=true;}sqlite3_finalize(ftsSearchPlan);sqlite3_close(largeFixture);if(!usesFtsIndex)return 225;
    largeFilter.query="Sample 12475";const auto indexedLargeSearch=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,0);if(indexedLargeSearch.records.size()!=10||indexedLargeSearch.hasMore||indexedLargeSearch.records[0].file.getFileName()!="Sample 124750.wav"||indexedLargeSearch.records[9].file.getFileName()!="Sample 124759.wav")return 221;
    const auto pluginBrowse=db.getPluginSoundBrowseRecords();if(pluginBrowse.size()!=1||pluginBrowse.getFirst().previewFile!=sample.file||pluginBrowse.getFirst().pluginIdentifier!=instrument.identifier||pluginBrowse.getFirst().pluginName!=instrument.name||pluginBrowse.getFirst().kitName!="Test Kit"||pluginBrowse.getFirst().midiNote!=36||pluginBrowse.getFirst().velocity!=100)return 69;
    SampleBrowseFilter pluginPageFilter;pluginPageFilter.pluginName=instrument.name;pluginPageFilter.kitName="Test Kit";const auto pluginDatabasePage=db.searchSampleBrowseRecords(pluginPageFilter,10,0);if(pluginDatabasePage.records.size()!=1||pluginDatabasePage.records[0].pluginIdentifier!=instrument.identifier||pluginDatabasePage.records[0].midiNote!=36||pluginDatabasePage.records[0].velocity!=100)return 157;
    pluginPageFilter.pluginName.clear();pluginPageFilter.kitName.clear();pluginPageFilter.query=instrument.name;const auto indexedPluginSearch=db.searchSampleBrowseRecords(pluginPageFilter,10,0);if(indexedPluginSearch.records.size()!=1||indexedPluginSearch.records[0].pluginIdentifier!=instrument.identifier)return 222;
    auto renamedInstrument=instrument;renamedInstrument.name="Fixture Drums Renamed";if(!db.upsertPluginInstrument(renamedInstrument))return 250;pluginPageFilter.query=renamedInstrument.name;const auto renamedInstrumentSearch=db.searchSampleBrowseRecords(pluginPageFilter,10,0);if(renamedInstrumentSearch.records.size()!=1||renamedInstrumentSearch.records[0].pluginIdentifier!=instrument.identifier)return 251;if(!db.upsertPluginInstrument(instrument))return 253;
    sqlite3* renamedKitConnection=nullptr;if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&renamedKitConnection)!=SQLITE_OK)return 254;sqlite3_stmt* renameKitStatement=nullptr;if(sqlite3_prepare_v2(renamedKitConnection,"UPDATE plugin_kits SET name=? WHERE id=?",-1,&renameKitStatement,nullptr)!=SQLITE_OK){sqlite3_close(renamedKitConnection);return 255;}sqlite3_bind_text(renameKitStatement,1,"Renamed Search Kit",-1,SQLITE_STATIC);sqlite3_bind_int64(renameKitStatement,2,*kitId);const bool kitRenamed=sqlite3_step(renameKitStatement)==SQLITE_DONE&&sqlite3_changes(renamedKitConnection)==1;sqlite3_finalize(renameKitStatement);sqlite3_close(renamedKitConnection);if(!kitRenamed)return 256;pluginPageFilter.query="Renamed Search Kit";const auto renamedKitSearch=db.searchSampleBrowseRecords(pluginPageFilter,10,0);if(renamedKitSearch.records.size()!=1||renamedKitSearch.records[0].pluginIdentifier!=instrument.identifier)return 257;pluginPageFilter.query="Test Kit";if(!db.searchSampleBrowseRecords(pluginPageFilter,10,0).records.isEmpty())return 258;
    sqlite3* restoredKitConnection=nullptr;if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&restoredKitConnection)!=SQLITE_OK)return 259;sqlite3_stmt* restoreKitStatement=nullptr;if(sqlite3_prepare_v2(restoredKitConnection,"UPDATE plugin_kits SET name=? WHERE id=?",-1,&restoreKitStatement,nullptr)!=SQLITE_OK){sqlite3_close(restoredKitConnection);return 260;}sqlite3_bind_text(restoreKitStatement,1,"Test Kit",-1,SQLITE_STATIC);sqlite3_bind_int64(restoreKitStatement,2,*kitId);const bool kitRestored=sqlite3_step(restoreKitStatement)==SQLITE_DONE&&sqlite3_changes(restoredKitConnection)==1;sqlite3_finalize(restoreKitStatement);sqlite3_close(restoredKitConnection);if(!kitRestored)return 261;
    if(!db.isSampleUnchanged(sample.file,sample.fileSizeBytes,sample.modifiedTimeMillis))return 45;
    auto staleAnalysis=sample;staleAnalysis.analysisVersion=currentSampleAnalysisVersion-1;if(!db.insertSample(staleAnalysis)||db.isSampleUnchanged(sample.file,sample.fileSizeBytes,sample.modifiedTimeMillis)||!db.insertSample(sample)||!db.isSampleUnchanged(sample.file,sample.fileSizeBytes,sample.modifiedTimeMillis))return 84;
    WavScanner::Stats incrementalStats;
    const auto skippedScan=scanner.scan(root,{},[&db](const juce::File& file,std::int64_t size,std::int64_t modified){return db.isSampleUnchanged(file,size,modified);},&incrementalStats);
    if(!skippedScan.isEmpty()||incrementalStats.filesSkippedUnchanged!=1||incrementalStats.filesAnalyzed!=0)return 46;
    const auto scanSession=db.beginSampleScan(root);
    if(!scanSession||!db.finishSampleScan(*scanSession,"completed",1,1,0,0))return 47;
    const auto scanHistory=db.getRecentSampleScans(5);
    if(scanHistory.size()!=1||scanHistory.getFirst().status!="completed"||scanHistory.getFirst().filesSkipped!=1)return 48;
    const auto unfinishedScan=db.beginSampleScan(root);
    if(!unfinishedScan||db.markRunningSampleScansInterrupted()!=1)return 49;
    const auto interruptedHistory=db.getRecentSampleScans(5);
    if(interruptedHistory.size()!=2||interruptedHistory.getFirst().status!="interrupted")return 50;
    auto paths=db.getAllPaths();
    if(paths.size()!=1||paths.getFirst()!=waveFile)return 11;
    if(db.searchPaths("kick",10).size()!=1||!db.searchPaths("missing",10).isEmpty())return 30;
    if(!db.insertSample(gainSample)||!db.insertSample(pitchSample))return 62;
    ScannedSample kickLike; kickLike.durationSeconds=.5; kickLike.rms=.2; kickLike.peak=.8; kickLike.spectralCentroidHz=200; kickLike.transientStrength=.12; kickLike.fundamentalHz=90; kickLike.pitchConfidence=.8;
    auto prediction=AudioClassifier{}.classify(kickLike);
    if(prediction.category!="Kick"||prediction.confidence<=0.0||prediction.confidence>1.0)return 13;
    auto longKickLike=kickLike;longKickLike.durationSeconds=2.1;longKickLike.spectralCentroidHz=200.0;
    if(AudioClassifier{}.classify(longKickLike).category!="Kick")return 243;
    auto harmonicKickLike=kickLike;harmonicKickLike.durationSeconds=1.0;harmonicKickLike.spectralCentroidHz=165.0;harmonicKickLike.transientStrength=0.4;harmonicKickLike.fundamentalHz=1002.0;harmonicKickLike.pitchConfidence=0.95;
    const auto harmonicKickPrediction=AudioClassifier{}.classify(harmonicKickLike);
    if(harmonicKickPrediction.category!="Kick"||harmonicKickPrediction.confidence<0.4||harmonicKickPrediction.confidence>=0.75)return 260;
    auto tonalPercussion=kickLike;tonalPercussion.spectralCentroidHz=2500.0;tonalPercussion.spectralFlatness=0.3;tonalPercussion.fundamentalHz=180.0;
    if(AudioClassifier{}.classify(tonalPercussion).category!="Snare")return 208;
    auto tomLike=kickLike;tomLike.durationSeconds=2.6;tomLike.fundamentalHz=180.0;tomLike.spectralCentroidHz=700.0;const auto tomPrediction=AudioClassifier{}.classify(tomLike);
    if(tomPrediction.category!="Tom"||tomPrediction.confidence<=0.0||tomPrediction.confidence>=0.75)return 209;
    auto longLowTomLike=kickLike;longLowTomLike.durationSeconds=2.6;longLowTomLike.spectralCentroidHz=810.0;
    longLowTomLike.fundamentalHz=117.0;longLowTomLike.pitchConfidence=0.70;longLowTomLike.transientStrength=0.82;
    const auto longLowTomPrediction=AudioClassifier{}.classify(longLowTomLike);
    if(longLowTomPrediction.category!="Tom"||longLowTomPrediction.confidence>=0.75)return 244;
    auto longUnpitchedTomLike=longLowTomLike;longUnpitchedTomLike.pitchConfidence=0.0;longUnpitchedTomLike.fundamentalHz=0.0;
    if(AudioClassifier{}.classify(longUnpitchedTomLike).category!="Tom")return 245;
    if(!prediction.tags.contains("Dark"))return 193;
    auto brightFeatureSample=kickLike;brightFeatureSample.spectralCentroidHz=3500.0;
    if(!AudioClassifier{}.classify(brightFeatureSample).tags.contains("Bright"))return 194;
    auto loudShortSample=kickLike;loudShortSample.durationSeconds=0.1;loudShortSample.rms=0.3;loudShortSample.bitDepth=0;
    if(!AudioClassifier{}.classify(loudShortSample).tags.contains("Short"))return 195;
    auto descriptiveTagSample=kickLike;descriptiveTagSample.file=juce::File("Distorted_909_Snare.wav");
    const auto descriptiveTags=AudioClassifier{}.classify(descriptiveTagSample);
    if(descriptiveTags.category!="Snare"||!descriptiveTags.tags.contains("Distorted")||!descriptiveTags.tags.contains("909"))return 196;
    const std::array<std::pair<const char*, const char*>, 14> sfzNameClasses{{
        {"hhc_vl1_rr1.flac", "Closed hi-hat"}, {"hho_vl1_rr1.flac", "Open hi-hat"},
        {"hhp_vl1_rr1.flac", "Pedal hi-hat"}, {"tomhi_vl1_rr1.flac", "Tom"},
        {"tomlo_vl1_rr1.flac", "Tom"}, {"ridebell_vl1_rr1.flac", "Ride"},
        {"sidestick_vl1_rr1.flac", "Rimshot"}, {"kick_vl1_rr1.flac", "Kick"},
        {"001_kickmic_hh_closed_vl1_rr1.flac", "Closed hi-hat"},
        {"001_kickmic_hh_open_vl1_rr1.flac", "Open hi-hat"},
        {"001_kickmic_hh_pedal_vl1_rr1.flac", "Pedal hi-hat"},
        {"hh_closed_vl1_rr1.flac", "Closed hi-hat"},
        {"hh_open_vl1_rr1.flac", "Open hi-hat"},
        {"hh_pedal_vl1_rr1.flac", "Pedal hi-hat"}}};
    for (const auto& [filename, expectedCategory] : sfzNameClasses)
    {
        auto mappedName = kickLike; mappedName.file = juce::File(filename);
        const auto mapped = AudioClassifier{}.classify(mappedName);
        if (mapped.category != expectedCategory || mapped.confidence < 0.9)
        {
            std::cerr << "SFZ family cue failed: " << filename << " => " << mapped.category
                      << " confidence=" << mapped.confidence << " expected=" << expectedCategory << '\n';
            return 215;
        }
        if (std::strcmp(filename, "ridebell_vl1_rr1.flac") == 0 && !mapped.tags.contains("Articulation: Bell")) return 216;
    }
    for (const auto* genericHatName : {"HiHat.wav", "HH.wav", "Hi-Hat_01.wav"})
    {
        ScannedSample ambiguousHat;
        ambiguousHat.file = juce::File(genericHatName);
        const auto result = AudioClassifier{}.classify(ambiguousHat);
        if (result.category != "Unknown" || !result.tags.contains("Hi-hat"))
        {
            std::cerr << "Ambiguous hi-hat name was assigned a specific family: " << genericHatName
                      << " => " << result.category << '\n';
            return 263;
        }
    }
    struct AudioOnlyFamilyFixture
    {
        const char* name;
        double duration;
        double centroid;
        double flatness;
        double transientRatio;
        int transientCount;
        const char* expected;
    };
    constexpr std::array<AudioOnlyFamilyFixture, 7> audioOnlyCymbalFixtures{{
        {"shorter low-brightness noise", 0.70, 5175.0, 0.38, 2.36, 1, "Closed hi-hat"},
        {"long open noise", 9.17, 5749.0, 0.38, 3.77, 2, "Open hi-hat"},
        {"mid-length low-brightness noise", 1.97, 3168.0, 0.32, 3.31, 2, "Pedal hi-hat"},
        {"short ultra-bright noise", 0.25, 11559.0, 0.66, 1.64, 1, "Closed hi-hat"},
        {"short bright noise", 0.50, 8273.0, 0.62, 1.56, 1, "Open hi-hat"},
        {"long single-hit cymbal", 12.02, 4754.0, 0.40, 4.44, 1, "Crash"},
        {"sustained repeating cymbal", 6.82, 6436.0, 0.44, 2.73, 10, "Ride"}}};
    for (const auto& fixture : audioOnlyCymbalFixtures)
    {
        ScannedSample unlabelled;
        unlabelled.file = juce::File("neutral-audio-only-fixture.wav");
        unlabelled.durationSeconds = fixture.duration;
        unlabelled.rms = 0.2;
        unlabelled.peak = 0.8;
        unlabelled.spectralCentroidHz = fixture.centroid;
        unlabelled.spectralFlatness = fixture.flatness;
        unlabelled.transientStrength = fixture.transientRatio * unlabelled.rms;
        unlabelled.transientCount = fixture.transientCount;
        const auto audioOnly = AudioClassifier{}.classify(unlabelled);
        if (audioOnly.category != fixture.expected || audioOnly.confidence >= 0.75)
        {
            std::cerr << "Audio-only low-confidence family fixture failed for " << fixture.name
                      << ": " << audioOnly.category << " confidence=" << audioOnly.confidence
                      << " expected=" << fixture.expected << '\n';
            return 265;
        }
    }
    ScannedSample audioOnlyBell;
    audioOnlyBell.file = juce::File("neutral-pitched-hit.wav");
    audioOnlyBell.durationSeconds = 1.13;
    audioOnlyBell.rms = 0.2;
    audioOnlyBell.peak = 0.8;
    audioOnlyBell.spectralCentroidHz = 5701.0;
    audioOnlyBell.spectralFlatness = 0.48;
    audioOnlyBell.transientStrength = 3.37 * audioOnlyBell.rms;
    audioOnlyBell.transientCount = 1;
    audioOnlyBell.fundamentalHz = 727.0;
    audioOnlyBell.pitchConfidence = 0.78;
    const auto bellSuggestion = AudioClassifier{}.classify(audioOnlyBell);
    if (bellSuggestion.category != "Percussion" || bellSuggestion.confidence >= 0.75) return 266;
    auto lowCentroidSnare = audioOnlyBell;
    lowCentroidSnare.file = juce::File("neutral-low-mid-hit.wav");
    lowCentroidSnare.durationSeconds = 2.29;
    lowCentroidSnare.spectralCentroidHz = 1538.0;
    lowCentroidSnare.spectralFlatness = 0.36;
    lowCentroidSnare.transientStrength = 4.51 * lowCentroidSnare.rms;
    lowCentroidSnare.fundamentalHz = 178.0;
    lowCentroidSnare.pitchConfidence = 0.0;
    const auto snareSuggestion = AudioClassifier{}.classify(lowCentroidSnare);
    if (snareSuggestion.category != "Snare" || snareSuggestion.confidence >= 0.75) return 267;
    ScannedSample contradictoryHat = kickLike;
    contradictoryHat.file = juce::File("Closed_HiHat_Open_HiHat.wav");
    if (AudioClassifier{}.classify(contradictoryHat).category != "Unknown") return 264;
    auto sustainedTone=scanned.getFirst();sustainedTone.file=juce::File("tone.wav");
    if(AudioClassifier{}.classify(sustainedTone).category!="Unknown")return 138;
    auto lowCentroidNoise=kickLike;lowCentroidNoise.transientStrength=.02;lowCentroidNoise.fundamentalHz=0;lowCentroidNoise.pitchConfidence=0;
    if(AudioClassifier{}.classify(lowCentroidNoise).category!="Unknown")return 139;
    ScannedSample namedHat=kickLike;namedHat.file=juce::File("808_Open_HiHat_deep.wav");
    const auto namedPrediction=AudioClassifier{}.classify(namedHat);
    if(namedPrediction.category!="Open hi-hat"||namedPrediction.confidence<0.9||!namedPrediction.tags.contains("808")||!namedPrediction.tags.contains("Electronic")||!namedPrediction.tags.contains("Deep"))return 64;
    ScannedSample articulationSample=namedHat;articulationSample.file=juce::File("Snare_Ghost_Note_Flam_Cross_Stick_Brush.wav");const auto articulationPrediction=AudioClassifier{}.classify(articulationSample);
    if(articulationPrediction.category!="Snare"||!articulationPrediction.tags.contains("Articulation: Ghost note")||!articulationPrediction.tags.contains("Articulation: Flam")||!articulationPrediction.tags.contains("Articulation: Cross stick")||!articulationPrediction.tags.contains("Articulation: Brushes"))return 173;
    articulationSample.file=juce::File("Half_Open_HiHat_Bell_Edge.wav");const auto cymbalArticulationPrediction=AudioClassifier{}.classify(articulationSample);
    if(cymbalArticulationPrediction.category!="Open hi-hat"||!cymbalArticulationPrediction.tags.contains("Articulation: Half-open")||!cymbalArticulationPrediction.tags.contains("Articulation: Bell")||!cymbalArticulationPrediction.tags.contains("Articulation: Edge"))return 174;
    const auto articulationRoot=root.getSiblingFile("MagDrumArticulationTagTest");articulationRoot.deleteRecursively();if(!articulationRoot.createDirectory().wasOk())return 175;
    const auto articulationFile=articulationRoot.getChildFile("Snare_Ghost_Note_Flam_Cross_Stick_Brush.wav");if(!waveFile.copyFileTo(articulationFile))return 176;
    const auto articulationScans=scanner.scan(articulationRoot);if(articulationScans.size()!=1||!articulationScans.getFirst().classificationTags.contains("Articulation: Ghost note")||!articulationScans.getFirst().classificationTags.contains("Articulation: Flam"))return 177;
    SampleDatabase articulationDb;if(!articulationDb.open(articulationRoot.getChildFile("articulations.sqlite3"))||!articulationDb.insertSample(articulationScans.getFirst())||!articulationDb.getClassificationTags(articulationFile).contains("Articulation: Flam"))return 178;
    SampleBrowseFilter articulationFilter;articulationFilter.query="Articulation: Flam";const auto articulationSearch=articulationDb.searchSampleBrowseRecords(articulationFilter,10,0);if(articulationSearch.records.size()!=1||!articulationSearch.records[0].classificationTags.contains("Articulation: Flam"))return 179;
    ScannedSample filenameOnly;filenameOnly.durationSeconds=.5;filenameOnly.rms=.2;filenameOnly.peak=.8;filenameOnly.spectralCentroidHz=2500;filenameOnly.transientStrength=.01;
    filenameOnly.file=juce::File("Kick_01.wav");const auto tokenKick=AudioClassifier{}.classify(filenameOnly);
    if(tokenKick.category!="Kick"||tokenKick.confidence<0.9)return 141;
    const std::array<std::pair<const char*,const char*>,13> drumAliases{{
        {"BD_01.wav","Kick"},{"SD_Center.wav","Snare"},{"CHH_Closed.wav","Closed hi-hat"},
        {"OHH_Open.wav","Open hi-hat"},{"PHH_Foot.wav","Pedal hi-hat"},{"kickmic_htom_center_vl1.wav","Tom"},{"kickmic_ltom_center_vl1.wav","Tom"},{"Agogo_High_v1_rr1.wav","Percussion"},{"Wood_Block_01.wav","Percussion"},{"Triangle_Hit.wav","Percussion"},{"Guiro_Scrape.wav","Percussion"},{"Timbales_High.wav","Percussion"},{"Cajon_Slap.wav","Percussion"}}};
    for(const auto& [filename,category] : drumAliases){filenameOnly.file=juce::File(filename);if(AudioClassifier{}.classify(filenameOnly).category!=category)return 197;}
    filenameOnly.file=juce::File("pride.wav");if(AudioClassifier{}.classify(filenameOnly).category!="Unknown")return 142;
    filenameOnly.file=juce::File("sidekick.wav");if(AudioClassifier{}.classify(filenameOnly).category!="Unknown")return 143;
    auto folderNamedSample=kickLike;folderNamedSample.file=juce::File("Library/KitA/Kicks/Layer 01/sample.wav");const auto folderKickPrediction=AudioClassifier{}.classify(folderNamedSample);if(folderKickPrediction.category!="Kick"||std::abs(folderKickPrediction.confidence-0.84)>1.0e-9)return 252;
    folderNamedSample.file=juce::File("808s/808-bass-sub.wav");const auto folder808Prediction=AudioClassifier{}.classify(folderNamedSample);if(folder808Prediction.category!="Kick"||std::abs(folder808Prediction.confidence-0.97)>1.0e-9)return 255;
    folderNamedSample.file=juce::File("808s/Snare.wav");if(AudioClassifier{}.classify(folderNamedSample).category!="Unknown")return 256;
    folderNamedSample.file=juce::File("Library/KitA/htom/Layer 01/sample.wav");const auto folderTomPrediction=AudioClassifier{}.classify(folderNamedSample);if(folderTomPrediction.category!="Tom"||std::abs(folderTomPrediction.confidence-0.84)>1.0e-9)return 257;
    folderNamedSample.file=juce::File("Library/KitA/ltom/Snare.wav");if(AudioClassifier{}.classify(folderNamedSample).category!="Unknown")return 258;
    folderNamedSample.file=juce::File("percussion/loop 01.wav");if(AudioClassifier{}.classify(folderNamedSample).category!="Percussion")return 253;
    folderNamedSample.file=juce::File("Library/KitA/Agogo/Layer 01/sample.wav");if(AudioClassifier{}.classify(folderNamedSample).category!="Percussion")return 259;
    folderNamedSample.file=juce::File("Snare/Kick.wav");if(AudioClassifier{}.classify(folderNamedSample).category!="Unknown")return 254;
    ScannedSample ambiguousName=kickLike;ambiguousName.file=juce::File("kick_snare.wav");
    if(AudioClassifier{}.classify(ambiguousName).category!="Unknown")return 65;
    const AudioClassifier modelClassifier(std::make_shared<FixtureClassificationModel>());
    const auto modelPrediction=modelClassifier.classify(kickLike);
    if(modelPrediction.category!="Clap"||std::abs(modelPrediction.confidence-0.83)>0.0001||modelPrediction.tags.size()!=1)return 54;
    const AudioClassifier invalidModelClassifier(std::make_shared<FixtureClassificationModel>("unsupported",1.2));
    if(invalidModelClassifier.classify(kickLike).category!="Unknown")return 55;
    ScannedSample manualExample;manualExample.durationSeconds=0.5;manualExample.channels=1;manualExample.sampleRate=44100;manualExample.peak=0.5;manualExample.rms=0.2;manualExample.spectralCentroidHz=1500;manualExample.spectralFlatness=0.4;manualExample.transientStrength=0.04;manualExample.transientCount=1;manualExample.category="Ride";
    std::vector<ScannedSample> manualExamples(3,manualExample);manualExamples[0].file=juce::File("manual-ride-a.wav");manualExamples[1].file=juce::File("manual-ride-b.wav");manualExamples[2].file=juce::File("manual-ride-c.wav");
    const AudioClassifier manualExampleClassifier(std::make_shared<ManualExampleDrumClassificationModel>(manualExamples));
    auto learnedQuery=manualExample;learnedQuery.category="Unknown";learnedQuery.file=juce::File("unlabeled-audio.wav");
    const auto learnedPrediction=manualExampleClassifier.classify(learnedQuery);
    if(learnedPrediction.category!="Ride"||learnedPrediction.confidence<0.55||learnedPrediction.confidence>=0.75||!learnedPrediction.tags.contains("User-trained suggestion"))return 218;
    auto kickFingerprint=std::string(128,'0'),snareFingerprint=std::string(128,'0');for(int segment=0;segment<4;++segment){kickFingerprint[static_cast<size_t>(segment*32)]='f';kickFingerprint[static_cast<size_t>(segment*32+1)]='f';snareFingerprint[static_cast<size_t>(segment*32+30)]='f';snareFingerprint[static_cast<size_t>(segment*32+31)]='f';}
    auto fingerprintExamples=manualExamples;for(auto& example:fingerprintExamples)example.acousticFingerprintVersion=1;
    fingerprintExamples[0].file=juce::File("kick-a.wav");fingerprintExamples[0].category="Kick";fingerprintExamples[0].acousticFingerprint=juce::String::fromUTF8(kickFingerprint.c_str());fingerprintExamples[1].file=juce::File("kick-b.wav");fingerprintExamples[1].category="Kick";fingerprintExamples[1].acousticFingerprint=juce::String::fromUTF8(kickFingerprint.c_str());fingerprintExamples[2].file=juce::File("snare-a.wav");fingerprintExamples[2].category="Snare";fingerprintExamples[2].acousticFingerprint=juce::String::fromUTF8(snareFingerprint.c_str());fingerprintExamples.push_back(fingerprintExamples[2]);fingerprintExamples.back().file=juce::File("snare-b.wav");
    auto fingerprintQuery=manualExample;fingerprintQuery.file=juce::File("anonymous-audio.wav");fingerprintQuery.acousticFingerprintVersion=1;fingerprintQuery.acousticFingerprint=juce::String::fromUTF8(kickFingerprint.c_str());
    const auto fingerprintPrediction=AudioClassifier(std::make_shared<ManualExampleDrumClassificationModel>(fingerprintExamples)).classify(fingerprintQuery);
    if(fingerprintPrediction.category!="Kick"||!fingerprintPrediction.tags.contains("User-trained suggestion")||fingerprintPrediction.confidence>=0.75)return 229;
    learnedQuery.file=juce::File("Kick_01.wav");if(manualExampleClassifier.classify(learnedQuery).category!="Kick")return 219;
    auto disagreeingExamples=manualExamples;disagreeingExamples[1].category="Crash";disagreeingExamples[2].category="Tom";
    learnedQuery.file=juce::File("unlabeled-audio.wav");
    if(AudioClassifier(std::make_shared<ManualExampleDrumClassificationModel>(disagreeingExamples)).classify(learnedQuery).category!="Unknown")return 220;
    const auto learnedScanRoot=root.getSiblingFile("MagDrumLearnedClassifierScanTest");learnedScanRoot.deleteRecursively();if(!learnedScanRoot.createDirectory().wasOk())return 222;
    const auto learnedTrainingA=learnedScanRoot.getChildFile("manual-training-a.wav"),learnedTrainingB=learnedScanRoot.getChildFile("manual-training-b.wav");
    juce::WavAudioFormat learnedWavFormat;
    if(!writeFormatFixture(learnedWavFormat,learnedTrainingA)||!writeFormatFixture(learnedWavFormat,learnedTrainingB))return 223;
    auto scanExamples=WavScanner{}.scan(learnedScanRoot);if(scanExamples.size()!=2)return 224;
    std::vector<ScannedSample> scanManualExamples;for(auto example:scanExamples){example.category="Ride";scanManualExamples.push_back(example);}
    scanManualExamples.push_back(scanManualExamples.front());
    const auto modelScanned=WavScanner{}.scan(learnedScanRoot,{}, {},nullptr,{}, {},{},std::make_shared<ManualExampleDrumClassificationModel>(scanManualExamples));
    if(modelScanned.size()!=2||std::any_of(modelScanned.begin(),modelScanned.end(),[](const ScannedSample& scanned){return scanned.category!="Ride"||scanned.classificationConfidence<=0.0||scanned.classificationConfidence>=0.75||!scanned.classificationTags.contains("User-trained suggestion");}))return 225;
    if(!db.setUserCategory(waveFile,"Snare"))return 14;
    const auto manualDatabaseExamples=db.getManualClassificationExamples();
    const auto foundManualExample=std::find_if(manualDatabaseExamples.begin(),manualDatabaseExamples.end(),[&](const ScannedSample& example){return example.file==waveFile;});
    if(foundManualExample==manualDatabaseExamples.end()||foundManualExample->category!="Snare"||foundManualExample->durationSeconds<=0.0||manualDatabaseExamples.size()!=1)return 221;
    const auto correctedBrowse=db.getSampleBrowseRecords();
    bool correctedSampleIsManual=false;
    for(const auto& record:correctedBrowse)
        if(record.file==waveFile)
        {
            correctedSampleIsManual=record.manuallyClassified&&
                !needsClassificationReview(record.manuallyClassified,record.category,record.classificationConfidence);
            break;
        }
    if(!correctedSampleIsManual)return 130;
    if(db.getFirstSampleForCategory("Snare")!=std::optional<juce::File>(waveFile))return 74;
    if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 111;sqlite3_stmt* correctedPluginSound=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT count(*) FROM plugin_sounds WHERE preview_path=? AND category='Snare'",-1,&correctedPluginSound,nullptr)!=SQLITE_OK)return 112;sqlite3_bind_text(correctedPluginSound,1,waveFile.getFullPathName().toRawUTF8(),-1,SQLITE_TRANSIENT);const bool pluginCategoryCorrected=sqlite3_step(correctedPluginSound)==SQLITE_ROW&&sqlite3_column_int(correctedPluginSound,0)==1;sqlite3_finalize(correctedPluginSound);sqlite3_close(legacy);if(!pluginCategoryCorrected)return 113;
    ScannedSample related=sample; related.file=root.getChildFile("related.wav"); related.durationSeconds+=0.02;
    if(!db.insertSample(related))return 15;
    const auto duplicates=db.findExactDuplicates(waveFile);
    if(duplicates.size()!=1||duplicates.getFirst()!=related.file)return 51;
    const auto perceptualDuplicates=db.findPerceptualDuplicates(waveFile,0.85);
    if(perceptualDuplicates.size()!=1||perceptualDuplicates.front().file!=gainFile||perceptualDuplicates.front().similarity<0.85)return 53;
    juce::StringArray tags{"Acoustic","Punchy"};
    if(!db.setUserTags(related.file,tags)||db.getUserTags(related.file).size()!=2)return 16;
    auto similar=db.findSimilar(waveFile,5);
    if(similar.size()!=3)return 17;
    const auto similarityCorpusRoot=root.getSiblingFile("MagDrumLabeledSimilarityFixture");similarityCorpusRoot.deleteRecursively();if(!similarityCorpusRoot.createDirectory().wasOk())return 181;
    constexpr std::array<double,4> similarityCorpusFrequencies{110.0,220.0,440.0,880.0};std::array<std::array<juce::File,2>,4> similarityCorpusFiles{};
    for(size_t family=0;family<similarityCorpusFrequencies.size();++family){const auto stem="family-"+juce::String(static_cast<int>(similarityCorpusFrequencies[family]));similarityCorpusFiles[family][0]=similarityCorpusRoot.getChildFile(stem+"-quiet.wav");similarityCorpusFiles[family][1]=similarityCorpusRoot.getChildFile(stem+"-loud.wav");if(!writeTone(similarityCorpusFiles[family][0],similarityCorpusFrequencies[family],0.25f)||!writeTone(similarityCorpusFiles[family][1],similarityCorpusFrequencies[family],0.5f))return 182;}
    const auto similarityCorpusSamples=scanner.scan(similarityCorpusRoot);if(similarityCorpusSamples.size()!=8)return 183;SampleDatabase similarityCorpusDb;if(!similarityCorpusDb.open(similarityCorpusRoot.getChildFile("labeled-similarity.sqlite3")))return 184;for(const auto& corpusSample:similarityCorpusSamples)if(!similarityCorpusDb.insertSample(corpusSample))return 185;
    int similarityTruePositives=0,similarityFalsePositives=0,similarityFalseNegatives=0;for(const auto& familyFiles:similarityCorpusFiles)for(size_t version=0;version<familyFiles.size();++version){const auto queryFile=familyFiles[version],expectedMatch=familyFiles[1-version];bool foundExpected=false;const auto matches=similarityCorpusDb.findPerceptualDuplicates(queryFile,0.85,100);for(const auto& match:matches){if(match.file==expectedMatch)++similarityTruePositives;else ++similarityFalsePositives;foundExpected=foundExpected||match.file==expectedMatch;}if(!foundExpected)++similarityFalseNegatives;}
    const double similarityPrecision=similarityTruePositives+similarityFalsePositives>0?static_cast<double>(similarityTruePositives)/(similarityTruePositives+similarityFalsePositives):0.0;const double similarityRecall=similarityTruePositives+similarityFalseNegatives>0?static_cast<double>(similarityTruePositives)/(similarityTruePositives+similarityFalseNegatives):0.0;if(similarityPrecision<1.0||similarityRecall<1.0)return 186;
    auto gm=MidiMappingProfile::generalMidi();
    DrumEvent hat{DrumInstrument::HiHat,DrumArticulation::Closed,96,1.5,0.125};
    auto mapped=gm.toMidi(hat,false);
    if(!mapped.note||*mapped.note!=42||mapped.usedFallback)return 19;
    auto decoded=gm.fromMidi(42,96,1.5,0.125);
    if(!decoded||decoded->instrument!=DrumInstrument::HiHat||decoded->articulation!=DrumArticulation::Closed||decoded->positionBeats!=1.5)return 20;
    const auto cowbell = gm.fromMidi(56, 112, 2.0, 0.25);
    if (!cowbell || cowbell->instrument != DrumInstrument::Percussion
        || cowbell->articulation != DrumArticulation::Bell
        || gm.toMidi(*cowbell, MidiFallbackPolicy::disabled).note != std::optional<std::uint8_t>(56))
    {
        std::cerr << "The General MIDI profile did not preserve Cowbell note 56 as a mapped percussion articulation.\n";
        return 292;
    }
    MidiPattern cowbellPattern;
    cowbellPattern.events.push_back(*cowbell);
    const auto cowbellMidi = root.getChildFile("gm-cowbell-roundtrip.mid");
    MidiPattern decodedCowbellPattern;
    const auto cowbellReport = MidiPatternIO{}.write(cowbellMidi, cowbellPattern, gm, false)
        ? MidiPatternIO{}.read(cowbellMidi, gm, decodedCowbellPattern)
        : MidiImportReport{};
    if (!cowbellReport.ok || !cowbellReport.unmappedNotes.empty()
        || decodedCowbellPattern.events.size() != 1
        || decodedCowbellPattern.events.front().instrument != DrumInstrument::Percussion
        || decodedCowbellPattern.events.front().articulation != DrumArticulation::Bell)
    {
        std::cerr << "The General MIDI Cowbell event did not survive MIDI export and re-import.\n";
        return 293;
    }
    constexpr std::array<int, 16> dr660MidiNotes{
        49, 57, 53, 51, 50, 47, 45, 43, 37, 39, 42, 46, 36, 38, 44, 56};
    MidiPattern dr660Pattern;
    for (const int note : dr660MidiNotes)
    {
        const auto event = gm.fromMidi(static_cast<std::uint8_t>(note), 100, 1.0, 0.125);
        if (!event || gm.toMidi(*event, MidiFallbackPolicy::disabled).note
                != std::optional<std::uint8_t>(static_cast<std::uint8_t>(note)))
        {
            std::cerr << "The General MIDI profile did not map DR-660 pad note " << note << " both ways.\n";
            return 294;
        }
        dr660Pattern.events.push_back(*event);
    }
    const auto dr660Midi = root.getChildFile("gm-dr660-pads-roundtrip.mid");
    MidiPattern decodedDr660Pattern;
    const auto dr660Report = MidiPatternIO{}.write(dr660Midi, dr660Pattern, gm, false)
        ? MidiPatternIO{}.read(dr660Midi, gm, decodedDr660Pattern)
        : MidiImportReport{};
    if (!dr660Report.ok || !dr660Report.unmappedNotes.empty()
        || compareMidiPatterns(dr660Pattern, decodedDr660Pattern).addedEvents.size() != 0
        || compareMidiPatterns(dr660Pattern, decodedDr660Pattern).removedEvents.size() != 0)
    {
        std::cerr << "The full DR-660 pad set did not survive General MIDI file round-trip.\n";
        return 295;
    }
    const DrumEvent bell{DrumInstrument::Ride,DrumArticulation::Bell,100,0.0,0.1,53};
    if(gm.toMidi(bell,false).note!=std::optional<std::uint8_t>(53))return 287;
    if(gm.mappingsForMidiNote(42).size()!=1||gm.mappingsForMidiNote(38).size()!=2)return 133;
    DrumEvent unsupported{DrumInstrument::HiHat,DrumArticulation::Edge,80,0.0,0.1};
    if(gm.toMidi(unsupported,false).note)return 21;
    auto fallback=gm.toMidi(unsupported,true);
    if(!fallback.note||!fallback.usedFallback||fallback.mappedArticulation!=DrumArticulation::Closed)return 22;
    const auto closedHatOnly = MidiMappingProfile({{DrumInstrument::HiHat, DrumArticulation::Closed, 42}});
    const DrumEvent pedalHat{DrumInstrument::HiHat, DrumArticulation::Pedal, 80, 0.0, 0.1};
    if (closedHatOnly.toMidi(pedalHat, MidiFallbackPolicy::disabled).note
        || closedHatOnly.toMidi(pedalHat, MidiFallbackPolicy::defaultArticulation).note)
        return 247;
    const auto anyHatFallback = closedHatOnly.toMidi(pedalHat, MidiFallbackPolicy::anyArticulation);
    if (!anyHatFallback.note || *anyHatFallback.note != 42 || !anyHatFallback.usedFallback
        || anyHatFallback.mappedArticulation != DrumArticulation::Closed)
        return 248;
    MidiPattern policyPattern;policyPattern.events.push_back(pedalHat);
    const auto defaultOnlyMidi=root.getChildFile("default-articulation-only.mid");
    const auto anyArticulationMidi=root.getChildFile("any-same-instrument-articulation.mid");
    std::vector<DrumEvent> policyOmitted;std::vector<MidiFallbackUse> policyFallbacks;
    if(!MidiPatternIO{}.write(defaultOnlyMidi,policyPattern,closedHatOnly,
            MidiFallbackPolicy::defaultArticulation,&policyOmitted,&policyFallbacks)
        ||policyOmitted.size()!=1||!policyFallbacks.empty())return 249;
    policyOmitted.clear();policyFallbacks.clear();
    if(!MidiPatternIO{}.write(anyArticulationMidi,policyPattern,closedHatOnly,
            MidiFallbackPolicy::anyArticulation,&policyOmitted,&policyFallbacks)
        ||!policyOmitted.empty()||policyFallbacks.size()!=1||policyFallbacks.front().midiNote!=42
        ||policyFallbacks.front().mappedArticulation!=DrumArticulation::Closed)return 250;
    MidiPattern fallbackExportPattern;fallbackExportPattern.events.push_back(unsupported);
    const auto strictMidi=root.getChildFile("strict-articulation-export.mid");
    const auto fallbackMidi=root.getChildFile("fallback-articulation-export.mid");
    std::vector<DrumEvent> omitted;std::vector<MidiFallbackUse> usedFallbacks;
    if(!MidiPatternIO{}.write(strictMidi,fallbackExportPattern,gm,false,&omitted,&usedFallbacks)
        ||omitted.size()!=1||!usedFallbacks.empty())return 245;
    omitted.clear();usedFallbacks.clear();
    if(!MidiPatternIO{}.write(fallbackMidi,fallbackExportPattern,gm,true,&omitted,&usedFallbacks)
        ||!omitted.empty()||usedFallbacks.size()!=1||usedFallbacks.front().midiNote!=42
        ||usedFallbacks.front().requestedEvent.articulation!=DrumArticulation::Edge
        ||usedFallbacks.front().mappedArticulation!=DrumArticulation::Closed)return 246;
    if(!db.saveMappingProfile("GM test",gm))return 26;
    if(!db.getMappingProfileNames().contains("GM test"))return 189;
    auto storedProfile=db.loadMappingProfile("GM test");
    if(!storedProfile||storedProfile->toMidi(hat,false).note!=std::optional<std::uint8_t>(42))return 27;
    auto editable=*storedProfile;
    if(!editable.setMapping({DrumInstrument::HiHat,DrumArticulation::Closed,41})||editable.toMidi(hat,false).note!=std::optional<std::uint8_t>(41))return 28;
    if(!editable.removeMapping(DrumInstrument::HiHat,DrumArticulation::Closed)||editable.toMidi(hat,false).note)return 29;
    const auto profileCsv=gm.toCsv();const auto importedProfile=MidiMappingProfile::fromCsv(profileCsv);
    if(!importedProfile||importedProfile->entries().size()!=gm.entries().size()||importedProfile->toMidi(hat,false).note!=std::optional<std::uint8_t>(42))return 42;
    if(MidiMappingProfile::fromCsv("instrument,articulation,note\nHiHat,Closed,128\n")||MidiMappingProfile::fromCsv("wrong,header,order\n"))return 43;
    const auto portableSourceRoot=root.getSiblingFile("MagDrumPortableMetadataSource"),portableTargetRoot=root.getSiblingFile("MagDrumPortableMetadataTarget");portableSourceRoot.deleteRecursively();portableTargetRoot.deleteRecursively();if(!portableSourceRoot.createDirectory().wasOk()||!portableTargetRoot.createDirectory().wasOk())return 229;const auto portableSourceFile=portableSourceRoot.getChildFile("same-content.wav"),portableTargetFile=portableTargetRoot.getChildFile("same-content.wav");if(!waveFile.copyFileTo(portableSourceFile)||!portableSourceFile.copyFileTo(portableTargetFile))return 230;const auto portableSourceSamples=scanner.scan(portableSourceRoot),portableTargetSamples=scanner.scan(portableTargetRoot);if(portableSourceSamples.size()!=1||portableTargetSamples.size()!=1)return 231;SampleDatabase portableSourceDb,portableTargetDb;const auto portableSourceDbFile=root.getSiblingFile("MagDrumPortableMetadataSource.sqlite3"),portableTargetDbFile=root.getSiblingFile("MagDrumPortableMetadataTarget.sqlite3");portableSourceDbFile.deleteFile();portableTargetDbFile.deleteFile();if(!portableSourceDb.open(portableSourceDbFile)||!portableTargetDb.open(portableTargetDbFile)||!portableSourceDb.insertSample(portableSourceSamples.getFirst())||!portableTargetDb.insertSample(portableTargetSamples.getFirst()))return 232;juce::StringArray portableTags{"Shared tag"};if(!portableSourceDb.setUserCategory(portableSourceFile,"Snare")||!portableSourceDb.setUserTags(portableSourceFile,portableTags)||!portableSourceDb.saveMappingProfile("Portable GM",gm))return 233;const auto portableArchive=portableSourceDb.exportPortableMetadata();const auto portableJson=juce::JSON::toString(portableArchive,false);if(portableJson.contains(portableSourceFile.getFullPathName())||portableJson.contains(portableTargetFile.getFullPathName()))return 234;const auto parsedPortableArchive=juce::JSON::parse(portableJson);juce::StringArray localPortableTags{"Local tag"};if(!portableTargetDb.setUserTags(portableTargetFile,localPortableTags))return 235;auto portableImport=portableTargetDb.mergePortableMetadata(parsedPortableArchive);if(!portableImport.ok||portableImport.samplesMatched!=1||portableImport.samplesUnavailable!=0||portableImport.tagsAdded!=1||portableImport.profilesAdded!=1||portableImport.categoryConflicts!=0||portableImport.profileConflicts!=0||portableTargetDb.getCategory(portableTargetFile)!="Snare"||!portableTargetDb.getUserTags(portableTargetFile).contains("Shared tag")||!portableTargetDb.getUserTags(portableTargetFile).contains("Local tag")||!portableTargetDb.loadMappingProfile("Portable GM"))return 236;auto conflictingPortableProfile=gm;if(!conflictingPortableProfile.setMapping({DrumInstrument::HiHat,DrumArticulation::Closed,41})||!portableTargetDb.setUserCategory(portableTargetFile,"Kick")||!portableTargetDb.saveMappingProfile("Portable GM",conflictingPortableProfile))return 237;portableImport=portableTargetDb.mergePortableMetadata(parsedPortableArchive);const auto preservedPortableProfile=portableTargetDb.loadMappingProfile("Portable GM");if(!portableImport.ok||portableImport.tagsAdded!=0||portableImport.categoryConflicts!=1||portableImport.profileConflicts!=1||portableTargetDb.getCategory(portableTargetFile)!="Kick"||!preservedPortableProfile||preservedPortableProfile->toMidi(hat,false).note!=std::optional<std::uint8_t>(41))return 238;auto unsupportedPortableArchive=parsedPortableArchive;unsupportedPortableArchive.getDynamicObject()->setProperty("version",2);if(portableTargetDb.mergePortableMetadata(unsupportedPortableArchive).ok)return 239;
    if (portableImport.conflicts.size() != 2) return 272;
    const auto categoryConflictIt = std::find_if(portableImport.conflicts.begin(), portableImport.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::manualCategory; });
    const auto profileConflictIt = std::find_if(portableImport.conflicts.begin(), portableImport.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::mappingProfile; });
    if (categoryConflictIt == portableImport.conflicts.end() || profileConflictIt == portableImport.conflicts.end()
        || categoryConflictIt->localValue != "Kick" || categoryConflictIt->incomingValues.size() != 1
        || categoryConflictIt->incomingValues.front().value != "Snare" || profileConflictIt->profileName != "Portable GM"
        || profileConflictIt->incomingValues.size() != 1) return 273;
    const auto categoryConflict = *categoryConflictIt;
    const auto profileConflict = *profileConflictIt;
    if (!portableTargetDb.setUserCategory(portableTargetFile, "Rimshot")
        || portableTargetDb.resolvePortableMetadataConflict(categoryConflict, categoryConflict.incomingValues.front())
        || portableTargetDb.getCategory(portableTargetFile) != "Rimshot") return 274;
    if (!portableTargetDb.setUserCategory(portableTargetFile, "Kick")
        || !portableTargetDb.resolvePortableMetadataConflict(categoryConflict, categoryConflict.incomingValues.front())
        || portableTargetDb.getCategory(portableTargetFile) != "Snare") return 275;
    if (!portableTargetDb.saveMappingProfile("Portable GM", gm)
        || portableTargetDb.resolvePortableMetadataConflict(profileConflict, profileConflict.incomingValues.front())) return 283;
    if (!portableTargetDb.saveMappingProfile("Portable GM", conflictingPortableProfile)
        || !portableTargetDb.resolvePortableMetadataConflict(profileConflict, profileConflict.incomingValues.front())) return 280;
    const auto resolvedPortableProfile = portableTargetDb.loadMappingProfile("Portable GM");
    if (!resolvedPortableProfile) return 281;
    if (resolvedPortableProfile->entries().size() != gm.entries().size()
        || resolvedPortableProfile->toMidi(hat, false).note != gm.toMidi(hat, false).note) return 282;
    if (!portableTargetDb.setUserCategory(portableTargetFile, "Kick")
        || !portableTargetDb.saveMappingProfile("Portable GM", conflictingPortableProfile)) return 277;
    const auto metadataSyncFolder = root.getSiblingFile("MagDrumPortableMetadataSync");
    metadataSyncFolder.deleteRecursively();
    if (!metadataSyncFolder.createDirectory().wasOk()) return 240;
    const auto sourceSync = portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!sourceSync.ok || sourceSync.archivesMerged != 0 || sourceSync.deviceId.isEmpty()) return 241;
    auto syncFiles = metadataSyncFolder.findChildFiles(juce::File::findFiles, false, "MagDrum-sync-*.json");
    if (syncFiles.size() != 1) return 242;
    const auto syncSnapshotText = syncFiles.getFirst().loadFileAsString();
    const auto syncSnapshot = juce::JSON::parse(syncSnapshotText);
    if (!syncSnapshot.isObject() || syncSnapshot.getProperty("device_id", {}).toString() != sourceSync.deviceId
        || static_cast<int>(syncSnapshot.getProperty("sync_version", {})) != 2
        || !syncSnapshot.getProperty("tag_operations", {}).isArray()
        || syncSnapshotText.contains(portableSourceFile.getFullPathName())) return 243;
    const auto targetSync = portableTargetDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!targetSync.ok) { std::cerr << "Portable tag sync failed: " << targetSync.error << '\n'; return 247; }
    if (targetSync.archivesMerged != 1) return 248;
    if (targetSync.samplesMatched != 1) return 249;
    if (targetSync.tagsAdded != 0) return 250;
    if (targetSync.categoryConflicts != 1) return 251;
    if (targetSync.profileConflicts != 1) return 252;
    if (targetSync.conflicts.size() != 2) return 278;
    const auto targetSyncCategoryConflict = std::find_if(targetSync.conflicts.begin(), targetSync.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::manualCategory; });
    const auto targetSyncProfileConflict = std::find_if(targetSync.conflicts.begin(), targetSync.conflicts.end(),
        [](const PortableMetadataConflict& conflict) { return conflict.type == PortableMetadataConflictType::mappingProfile; });
    if (targetSyncCategoryConflict == targetSync.conflicts.end() || targetSyncProfileConflict == targetSync.conflicts.end()
        || targetSyncCategoryConflict->incomingValues.size() != 1
        || targetSyncCategoryConflict->incomingValues.front().deviceId != sourceSync.deviceId
        || targetSyncCategoryConflict->incomingValues.front().value != "Snare"
        || targetSyncProfileConflict->incomingValues.size() != 1
        || targetSyncProfileConflict->incomingValues.front().deviceId != sourceSync.deviceId) return 279;
    if (portableTargetDb.getCategory(portableTargetFile) != "Kick") return 253;
    const auto returnSync = portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!returnSync.ok || returnSync.conflicts.size() != 2 || returnSync.archivesMerged != 1 || returnSync.categoryConflicts != 1
        || returnSync.profileConflicts != 1 || portableSourceDb.getCategory(portableSourceFile) != "Snare") return 245;
    const auto repeatSync = portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!repeatSync.ok || repeatSync.tagsAdded != 0 || repeatSync.profilesAdded != 0) return 246;

    const juce::StringArray sourceTagsWithDelete{"Shared tag", "Delete me"};
    if (!portableSourceDb.setUserTags(portableSourceFile, sourceTagsWithDelete)) return 260;
    if (!portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder).ok) return 261;
    const auto targetGetsDelete = portableTargetDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!targetGetsDelete.ok || !portableTargetDb.getUserTags(portableTargetFile).contains("Delete me")) return 262;
    if (!portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder).ok) return 271;

    const juce::StringArray sourceTagsAfterDelete{"Shared tag"};
    const juce::StringArray targetConcurrentTags{"Shared tag", "Concurrent tag"};
    if (!portableSourceDb.setUserTags(portableSourceFile, sourceTagsAfterDelete)
        || !portableTargetDb.setUserTags(portableTargetFile, targetConcurrentTags)) return 263;
    if (!portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder).ok) return 264;
    const auto targetAfterDelete = portableTargetDb.syncPortableMetadataFolder(metadataSyncFolder);
    const auto targetTagsAfterDelete = portableTargetDb.getUserTags(portableTargetFile);
    if (!targetAfterDelete.ok || targetTagsAfterDelete.contains("Delete me")
        || !targetTagsAfterDelete.contains("Concurrent tag") || !targetTagsAfterDelete.contains("Shared tag")) return 265;
    const auto sourceAfterConcurrentTag = portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder);
    const auto sourceTagsAfterConcurrent = portableSourceDb.getUserTags(portableSourceFile);
    if (!sourceAfterConcurrentTag.ok || sourceTagsAfterConcurrent.contains("Delete me")
        || !sourceTagsAfterConcurrent.contains("Concurrent tag") || !sourceTagsAfterConcurrent.contains("Shared tag")) return 266;

    const juce::StringArray tagsWithoutShared{"Concurrent tag"};
    const juce::StringArray tagsWithShared{"Concurrent tag", "Shared tag"};
    if (!portableSourceDb.setUserTags(portableSourceFile, tagsWithoutShared)
        || !portableTargetDb.setUserTags(portableTargetFile, tagsWithoutShared)
        || !portableTargetDb.setUserTags(portableTargetFile, tagsWithShared)) return 267;
    if (!portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder).ok) return 268;
    const auto concurrentReAdd = portableTargetDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!concurrentReAdd.ok || !portableTargetDb.getUserTags(portableTargetFile).contains("Shared tag")) return 269;
    const auto finalTagConvergence = portableSourceDb.syncPortableMetadataFolder(metadataSyncFolder);
    if (!finalTagConvergence.ok || !portableSourceDb.getUserTags(portableSourceFile).contains("Shared tag")
        || !portableSourceDb.getUserTags(portableSourceFile).contains("Concurrent tag")) return 270;
     if(portableSourceDb.getUserTags(portableSourceFile).contains("Local tag"))return 254;
    const auto invalidSyncFolder=root.getSiblingFile("MagDrumPortableMetadataInvalidSync"),invalidSyncDbFile=root.getSiblingFile("MagDrumPortableMetadataInvalidSync.sqlite3");invalidSyncFolder.deleteRecursively();invalidSyncDbFile.deleteFile();if(!invalidSyncFolder.createDirectory().wasOk())return 255;for(const auto& existingArchive:metadataSyncFolder.findChildFiles(juce::File::findFiles,false,"MagDrum-sync-*.json"))if(!existingArchive.copyFileTo(invalidSyncFolder.getChildFile(existingArchive.getFileName())))return 256;if(!invalidSyncFolder.getChildFile("MagDrum-sync-invalid.json").replaceWithText("{\"format\":\"com.magdrum.library-metadata\",\"version\":2,\"device_id\":\"invalid\",\"samples\":[],\"mapping_profiles\":[]}"))return 257;SampleDatabase invalidSyncDb;if(!invalidSyncDb.open(invalidSyncDbFile)||!invalidSyncDb.insertSample(portableTargetSamples.getFirst()))return 258;const auto invalidSyncCategory=invalidSyncDb.getCategory(portableTargetFile);const auto invalidSyncResult=invalidSyncDb.syncPortableMetadataFolder(invalidSyncFolder);if(invalidSyncResult.ok||!invalidSyncDb.getUserTags(portableTargetFile).isEmpty()||invalidSyncDb.getCategory(portableTargetFile)!=invalidSyncCategory||invalidSyncDb.loadMappingProfile("Portable GM"))return 259;
    const auto invalidReferenceFolder = root.getSiblingFile("MagDrumInvalidTagReferenceSync");
    const auto invalidReferenceDbFile = root.getSiblingFile("MagDrumInvalidTagReferenceSync.sqlite3");
    invalidReferenceFolder.deleteRecursively();
    invalidReferenceDbFile.deleteFile();
    if (!invalidReferenceFolder.createDirectory().wasOk()) return 272;
    const auto sourceSnapshot = juce::JSON::parse(metadataSyncFolder.getChildFile("MagDrum-sync-" + sourceSync.deviceId + ".json").loadFileAsString());
    if (!sourceSnapshot.isObject()) return 273;
    auto tamperedSnapshot = sourceSnapshot;
    auto* tagOperations = tamperedSnapshot.getProperty("tag_operations", {}).getArray();
    if (tagOperations == nullptr) return 274;
    bool changedReference = false;
    for (auto& removalOperation : *tagOperations)
    {
        if (removalOperation.getProperty("type", {}).toString() != "remove") continue;
        for (const auto& addOperation : *tagOperations)
        {
            if (addOperation.getProperty("type", {}).toString() != "add"
                || addOperation.getProperty("tag", {}).toString() == removalOperation.getProperty("tag", {}).toString()) continue;
            auto* observedAddIds = removalOperation.getProperty("observed_add_ids", {}).getArray();
            if (observedAddIds == nullptr) return 275;
            observedAddIds->clear();
            observedAddIds->add(addOperation.getProperty("operation_id", {}));
            changedReference = true;
            break;
        }
        if (changedReference) break;
    }
    if (!changedReference || !invalidReferenceFolder.getChildFile("MagDrum-sync-tampered.json").replaceWithText(juce::JSON::toString(tamperedSnapshot, true))) return 276;
    SampleDatabase invalidReferenceDb;
    if (!invalidReferenceDb.open(invalidReferenceDbFile) || !invalidReferenceDb.insertSample(portableTargetSamples.getFirst())) return 277;
    const auto invalidReferenceCategoryBefore = invalidReferenceDb.getCategory(portableTargetFile);
    const auto invalidReferenceResult = invalidReferenceDb.syncPortableMetadataFolder(invalidReferenceFolder);
    const auto invalidReferenceTags = invalidReferenceDb.getUserTags(portableTargetFile);
    const auto invalidReferenceCategory = invalidReferenceDb.getCategory(portableTargetFile);
    const auto invalidReferenceProfile = invalidReferenceDb.loadMappingProfile("Portable GM");
    if (invalidReferenceResult.ok || !invalidReferenceTags.isEmpty() || invalidReferenceCategory != invalidReferenceCategoryBefore || invalidReferenceProfile)
    {
        std::cerr << "Invalid tag reference rollback check: ok=" << invalidReferenceResult.ok
                  << ", error=" << invalidReferenceResult.error << ", tags=" << invalidReferenceTags.joinIntoString(",")
                  << ", category=" << invalidReferenceCategory << ", profile=" << invalidReferenceProfile.has_value() << '\n';
        return 278;
    }
     MidiPattern pattern;pattern.events.push_back(hat);pattern.tempoAndSignature.push_back({0.0,juce::MidiMessage::tempoMetaEvent(600000)});pattern.tempoAndSignature.push_back({0.0,juce::MidiMessage::timeSignatureMetaEvent(7,8)});pattern.lengthBeats=8.0;
    MidiPatternIO midiIO;const auto midiFile=root.getChildFile("mapping.mid");
    if(!midiIO.write(midiFile,pattern,gm,false))return 23;
    MidiPattern fallbackPattern;fallbackPattern.events.push_back(unsupported);const auto omittedFallbackFile=root.getChildFile("fallback-omitted.mid");std::vector<DrumEvent> omittedFallbackEvents;std::vector<MidiFallbackUse> unusedFallbacks;
    if(!midiIO.write(omittedFallbackFile,fallbackPattern,gm,false,&omittedFallbackEvents,&unusedFallbacks)||omittedFallbackEvents.size()!=1||!unusedFallbacks.empty())return 190;
    const auto mappedFallbackFile=root.getChildFile("fallback-enabled.mid");std::vector<DrumEvent> unsupportedFallbackEvents;std::vector<MidiFallbackUse> appliedFallbacks;
    if(!midiIO.write(mappedFallbackFile,fallbackPattern,gm,true,&unsupportedFallbackEvents,&appliedFallbacks)||!unsupportedFallbackEvents.empty()||appliedFallbacks.size()!=1||appliedFallbacks[0].requestedEvent.articulation!=DrumArticulation::Edge||appliedFallbacks[0].midiNote!=42||appliedFallbacks[0].mappedArticulation!=DrumArticulation::Closed)return 191;
    MidiPattern fallbackRoundTrip;if(!midiIO.read(mappedFallbackFile,gm,fallbackRoundTrip).ok||fallbackRoundTrip.events.size()!=1||fallbackRoundTrip.events[0].articulation!=DrumArticulation::Closed)return 192;
    MidiPattern decodedPattern;auto midiReport=midiIO.read(midiFile,gm,decodedPattern);
    if(!midiReport.ok||!midiReport.ambiguousNotes.empty()||decodedPattern.events.size()!=1||decodedPattern.tempoAndSignature.size()!=2||std::abs(decodedPattern.lengthBeats-8.0)>0.001)return 24;
    if(decodedPattern.events[0].velocity!=96||decodedPattern.events[0].midiNote!=42||std::abs(decodedPattern.events[0].positionBeats-1.5)>0.001||std::abs(decodedPattern.events[0].durationBeats-.125)>0.002)return 25;
    juce::MidiFile smpteMidi;
    smpteMidi.setSmpteTimeFormat(25, 40);
    juce::MidiMessageSequence smpteTempoTrack, smpteDrumTrack;
    smpteTempoTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
    smpteTempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(1000000), 1000.0);
    smpteTempoTrack.addEvent(juce::MidiMessage::endOfTrack(), 2000.0);
    smpteDrumTrack.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 1000.0);
    smpteDrumTrack.addEvent(juce::MidiMessage::noteOff(10, 36), 1500.0);
    smpteDrumTrack.addEvent(juce::MidiMessage::endOfTrack(), 2000.0);
    smpteTempoTrack.sort();
    smpteDrumTrack.sort();
    smpteMidi.addTrack(smpteTempoTrack);
    smpteMidi.addTrack(smpteDrumTrack);
    const auto smpteMidiFile = root.getChildFile("smpte-tempo-change.mid");
    auto smpteMidiStream = smpteMidiFile.createOutputStream();
    if (! smpteMidiStream || ! smpteMidi.writeTo(*smpteMidiStream)) return 282;
    smpteMidiStream->flush();
    MidiPattern smptePattern;
    const auto smpteReport = midiIO.read(smpteMidiFile, gm, smptePattern);
    if (! smpteReport.ok || smptePattern.events.size() != 1 || smptePattern.tempoAndSignature.size() != 2
        || smptePattern.events[0].instrument != DrumInstrument::Kick
        || std::abs(smptePattern.events[0].positionBeats - 2.0) > 0.001
        || std::abs(smptePattern.events[0].durationBeats - 0.5) > 0.001
        || std::abs(smptePattern.lengthBeats - 3.0) > 0.001
        || std::abs(smptePattern.tempoAndSignature[1].positionBeats - 2.0) > 0.001)
    {
        std::cerr << "SMPTE tempo-map conversion did not preserve drum timing.\n";
        return 283;
    }

    juce::MidiFile dropFrameMidi;
    dropFrameMidi.setSmpteTimeFormat(29, 80);
    juce::MidiMessageSequence dropFrameTrack;
    dropFrameTrack.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0.0);
    dropFrameTrack.addEvent(juce::MidiMessage::noteOff(10, 36), 3000.0);
    dropFrameTrack.addEvent(juce::MidiMessage::endOfTrack(), 3000.0);
    dropFrameTrack.sort();
    dropFrameMidi.addTrack(dropFrameTrack);
    const auto dropFrameMidiFile = root.getChildFile("smpte-drop-frame.mid");
    auto dropFrameStream = dropFrameMidiFile.createOutputStream();
    if (! dropFrameStream || ! dropFrameMidi.writeTo(*dropFrameStream)) return 284;
    dropFrameStream->flush();
    MidiPattern dropFramePattern;
    const auto dropFrameReport = midiIO.read(dropFrameMidiFile, gm, dropFramePattern);
    if (! dropFrameReport.ok || dropFramePattern.events.size() != 1
        || std::abs(dropFramePattern.events[0].durationBeats - 2.5025) > 0.001
        || std::abs(dropFramePattern.lengthBeats - 2.5025) > 0.001)
    {
        std::cerr << "29.97 fps SMPTE timing was not converted using its drop-frame rate.\n";
        return 285;
    }

    juce::MidiFile invalidSmpteMidi;
    invalidSmpteMidi.setSmpteTimeFormat(23, 80);
    juce::MidiMessageSequence invalidSmpteTrack;
    invalidSmpteTrack.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(100)), 0.0);
    invalidSmpteTrack.addEvent(juce::MidiMessage::noteOff(10, 36), 10.0);
    invalidSmpteTrack.sort();
    invalidSmpteMidi.addTrack(invalidSmpteTrack);
    const auto invalidSmpteMidiFile = root.getChildFile("smpte-invalid-frame-rate.mid");
    auto invalidSmpteStream = invalidSmpteMidiFile.createOutputStream();
    if (! invalidSmpteStream || ! invalidSmpteMidi.writeTo(*invalidSmpteStream)) return 286;
    invalidSmpteStream->flush();
    MidiPattern invalidSmptePattern;
    const auto invalidSmpteReport = midiIO.read(invalidSmpteMidiFile, gm, invalidSmptePattern);
    if (invalidSmpteReport.ok || invalidSmpteReport.error.isEmpty() || ! invalidSmptePattern.events.empty()) return 287;
    juce::MidiFile overlappingTrackMidi;overlappingTrackMidi.setTicksPerQuarterNote(960);juce::MidiMessageSequence firstTrack,secondTrack;firstTrack.addEvent(juce::MidiMessage::noteOn(10,42,static_cast<juce::uint8>(90)),0);firstTrack.addEvent(juce::MidiMessage::noteOff(10,42),2880);secondTrack.addEvent(juce::MidiMessage::noteOn(10,42,static_cast<juce::uint8>(100)),960);secondTrack.addEvent(juce::MidiMessage::noteOff(10,42),1920);firstTrack.sort();secondTrack.sort();overlappingTrackMidi.addTrack(firstTrack);overlappingTrackMidi.addTrack(secondTrack);const auto overlappingTrackFile=root.getChildFile("overlapping-notes-across-tracks.mid");auto overlappingTrackStream=overlappingTrackFile.createOutputStream();if(!overlappingTrackStream||!overlappingTrackMidi.writeTo(*overlappingTrackStream))return 280;overlappingTrackStream->flush();MidiPattern overlappingTrackPattern;const auto overlappingTrackReport=midiIO.read(overlappingTrackFile,gm,overlappingTrackPattern);if(!overlappingTrackReport.ok||overlappingTrackPattern.events.size()!=2||std::abs(overlappingTrackPattern.events[0].positionBeats)>0.001||std::abs(overlappingTrackPattern.events[0].durationBeats-2.0)>0.002||std::abs(overlappingTrackPattern.events[1].positionBeats-1.0)>0.001||std::abs(overlappingTrackPattern.events[1].durationBeats-2.0)>0.002)return 281;
    const auto exactPatternDiff=compareMidiPatterns(pattern,decodedPattern);
    if(!exactPatternDiff.removedEvents.empty()||!exactPatternDiff.addedEvents.empty()||!exactPatternDiff.removedMetadata.empty()||!exactPatternDiff.addedMetadata.empty()||exactPatternDiff.lengthChanged){std::cerr<<"Exact MIDI round-trip diff: removed="<<exactPatternDiff.removedEvents.size()<<", added="<<exactPatternDiff.addedEvents.size()<<", removed metadata="<<exactPatternDiff.removedMetadata.size()<<", added metadata="<<exactPatternDiff.addedMetadata.size()<<", length changed="<<exactPatternDiff.lengthChanged<<"\n";return 135;}
    auto differentRawPitch=decodedPattern;differentRawPitch.events[0].midiNote=43;const auto rawPitchDiff=compareMidiPatterns(decodedPattern,differentRawPitch);
    if(rawPitchDiff.removedEvents.size()!=1||rawPitchDiff.addedEvents.size()!=1){std::cerr<<"MIDI pattern comparison ignored a changed explicit source pitch.\n";return 288;}
    auto editedPattern=decodedPattern;editedPattern.events[0].velocity=80;editedPattern.events.push_back({DrumInstrument::Snare,DrumArticulation::Center,90,2.0,0.25});editedPattern.lengthBeats=10.0;if(!setPatternTempo(editedPattern,120))return 136;const auto editedPatternDiff=compareMidiPatterns(decodedPattern,editedPattern);
    if(editedPatternDiff.removedEvents.size()!=1||editedPatternDiff.addedEvents.size()!=2||editedPatternDiff.removedMetadata.size()!=1||editedPatternDiff.addedMetadata.size()!=1||!editedPatternDiff.lengthChanged){std::cerr<<"Edited MIDI diff: removed="<<editedPatternDiff.removedEvents.size()<<", added="<<editedPatternDiff.addedEvents.size()<<", removed metadata="<<editedPatternDiff.removedMetadata.size()<<", added metadata="<<editedPatternDiff.addedMetadata.size()<<", length changed="<<editedPatternDiff.lengthChanged<<"\n";return 137;}
    MidiMappingProfile ambiguousProfile({{DrumInstrument::HiHat,DrumArticulation::Closed,42},{DrumInstrument::Snare,DrumArticulation::Center,42},{DrumInstrument::HiHat,DrumArticulation::Closed,42}});
    MidiPattern ambiguousPattern;const auto ambiguousReport=midiIO.read(midiFile,ambiguousProfile,ambiguousPattern);
    if(!ambiguousReport.ok||ambiguousReport.ambiguousNotes.size()!=1||ambiguousReport.ambiguousNotes[0].note!=42||ambiguousReport.ambiguousNotes[0].eventCount!=1||ambiguousReport.ambiguousNotes[0].candidates.size()!=2||ambiguousPattern.events.size()!=1||ambiguousPattern.events[0].instrument!=DrumInstrument::HiHat||ambiguousPattern.events[0].midiNote!=42)return 134;
    MidiPattern emptyBars;emptyBars.lengthBeats=12.0;const auto emptyBarsFile=root.getChildFile("empty-bars.mid");if(!midiIO.write(emptyBarsFile,emptyBars,gm,false))return 72;MidiPattern decodedEmptyBars;if(!midiIO.read(emptyBarsFile,gm,decodedEmptyBars).ok||!decodedEmptyBars.events.empty()||std::abs(decodedEmptyBars.lengthBeats-12.0)>0.001)return 73;
    MidiPattern metadataEdit;metadataEdit.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{0.0,juce::MidiMessage::timeSignatureMetaEvent(4,4)},{8.0,juce::MidiMessage::tempoMetaEvent(750000)},{8.0,juce::MidiMessage::timeSignatureMetaEvent(3,4)}};
    if(!setPatternTempo(metadataEdit,100)||!setPatternTimeSignature(metadataEdit,5,8)||setPatternTempo(metadataEdit,10)||setPatternTimeSignature(metadataEdit,3,3)||metadataEdit.tempoAndSignature.size()!=4)return 64;
    bool startTempo=false,startSignature=false,laterTempo=false,laterSignature=false;for(const auto& meta:metadataEdit.tempoAndSignature){if(meta.positionBeats==0.0&&meta.message.isTempoMetaEvent())startTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.6)<0.000001;if(meta.positionBeats==0.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);startSignature=n==5&&d==8;}if(meta.positionBeats==8.0&&meta.message.isTempoMetaEvent())laterTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.75)<0.000001;if(meta.positionBeats==8.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);laterSignature=n==3&&d==4;}}if(!startTempo||!startSignature||!laterTempo||!laterSignature)return 65;
    auto metadataRemoval=metadataEdit;if(!removePatternTempo(metadataRemoval,8.0)||removePatternTempo(metadataRemoval,8.0)||metadataRemoval.tempoAndSignature.size()!=3||!removePatternTimeSignature(metadataRemoval,8.0)||metadataRemoval.tempoAndSignature.size()!=2||removePatternTimeSignature(metadataRemoval,std::numeric_limits<double>::quiet_NaN()))return 68;
    const auto metadataFile=root.getChildFile("metadata.mid");if(!midiIO.write(metadataFile,metadataEdit,gm,false))return 66;MidiPattern metadataRoundTrip;if(!midiIO.read(metadataFile,gm,metadataRoundTrip).ok||metadataRoundTrip.tempoAndSignature.size()!=4)return 67;
    std::vector<MidiNoteMapping> roundTripMappings;for(const auto& mapping:gm.entries())if(std::none_of(roundTripMappings.begin(),roundTripMappings.end(),[&](const auto& existing){return existing.note==mapping.note;})){const auto customNote=static_cast<std::uint8_t>(24+roundTripMappings.size());roundTripMappings.push_back({mapping.instrument,mapping.articulation,customNote});}MidiMappingProfile customRoundTripProfile(roundTripMappings);std::uint32_t midiRandom=0x5a17c9e3u;const auto nextMidiRandom=[&midiRandom](){midiRandom=midiRandom*1664525u+1013904223u;return midiRandom;};
    constexpr int variedMidiPatterns=24,variedMidiEvents=64;for(int patternIndex=0;patternIndex<variedMidiPatterns;++patternIndex){MidiPattern varied;varied.lengthBeats=16.0;const int initialTempo=47+static_cast<int>(nextMidiRandom()%200u),laterTempo=47+static_cast<int>(nextMidiRandom()%200u);const int numeratorA=3+static_cast<int>(nextMidiRandom()%6u),numeratorB=3+static_cast<int>(nextMidiRandom()%6u);const int denominators[]{4,8,16};if(!setPatternTempo(varied,initialTempo)||!setPatternTimeSignature(varied,numeratorA,denominators[nextMidiRandom()%3u])||!setPatternTempo(varied,laterTempo,8.0)||!setPatternTimeSignature(varied,numeratorB,denominators[nextMidiRandom()%3u],8.0))return 218;for(int eventIndex=0;eventIndex<variedMidiEvents;++eventIndex){const auto& mapping=roundTripMappings[nextMidiRandom()%roundTripMappings.size()];const auto velocity=static_cast<std::uint8_t>(1+nextMidiRandom()%127u);const auto durationTicks=1+static_cast<int>(nextMidiRandom()%96u);varied.events.push_back({mapping.instrument,mapping.articulation,velocity,static_cast<double>(eventIndex)*0.25,static_cast<double>(durationTicks)/960.0});}const auto variedFile=root.getChildFile("varied-roundtrip-"+juce::String(patternIndex)+".mid");if(!midiIO.write(variedFile,varied,customRoundTripProfile,false))return 219;MidiPattern variedDecoded;const auto variedReport=midiIO.read(variedFile,customRoundTripProfile,variedDecoded);const auto variedDiff=compareMidiPatterns(varied,variedDecoded);if(!variedReport.ok||!variedReport.unmappedNotes.empty()||!variedReport.ambiguousNotes.empty()||!variedDiff.removedEvents.empty()||!variedDiff.addedEvents.empty()||!variedDiff.removedMetadata.empty()||!variedDiff.addedMetadata.empty()||variedDiff.lengthChanged){std::cerr<<"Varied MIDI round-trip failed at case "<<patternIndex<<" (removed="<<variedDiff.removedEvents.size()<<", added="<<variedDiff.addedEvents.size()<<", metadata-="<<variedDiff.removedMetadata.size()<<", metadata+="<<variedDiff.addedMetadata.size()<<")\\n";return 220;}}
    SyntheticInstrument synthetic;synthetic.setCurrentProgram(1);const auto programs=discoverPluginPrograms(synthetic);if(programs.programs.size()!=2||programs.programs[0].index!=0||programs.programs[0].name!="Factory Kit A"||programs.programs[1].name!="Program 2"||programs.onlyGenericMidiPrograms||synthetic.getCurrentProgram()!=1)return 70;SyntheticInstrument genericProgramInstrument;genericProgramInstrument.setGenericProgramNames(true);const auto genericPrograms=discoverPluginPrograms(genericProgramInstrument);if(!genericPrograms.onlyGenericMidiPrograms||genericPrograms.programs.size()!=2)return 219;const auto limitedPrograms=discoverPluginPrograms(synthetic,1);if(!limitedPrograms.exceedsLimit||!limitedPrograms.programs.empty())return 71;PluginNoteScanSettings scanSettings;scanSettings.sampleRate=8000;scanSettings.blockSize=64;scanSettings.velocityLevels={64,127};scanSettings.noteDurationSeconds=.01;scanSettings.releaseSeconds=.01;
    int progress=0;auto noteScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{},[&](int done,int total,int,int){if(done<1||done>total)progress=-1000;else ++progress;});
    if(!noteScan.error.isEmpty()||noteScan.notes.size()!=256||progress!=256||synthetic.getCurrentProgram()!=1)return 39;if(noteScan.notes.empty()||noteScan.notes.front().sampleRate!=scanSettings.sampleRate)return 185;
    if(!synthetic.observedOfflineProcessing())return 183;
    int silentNotes=0,activeNotes=0;for(const auto&capture:noteScan.notes){if(capture.midiNote==60){if(capture.silent)silentNotes++;}else if(capture.velocity==127&&!capture.silent&&capture.peak>0.1)activeNotes++;}
    if(silentNotes!=2||activeNotes!=127)return 40;
    int streamedCaptureCount=0;const auto streamedNoteScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{}, {},[&](const PluginNoteCapture&){++streamedCaptureCount;return true;});if(!streamedNoteScan.error.isEmpty()||!streamedNoteScan.notes.empty()||streamedCaptureCount!=256)return 181;
    scanSettings.repeatCount=3;int repeatedProgress=0;auto repeatedScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{},[&](int done,int total,int,int){if(done<1||done>total)repeatedProgress=-1000;else ++repeatedProgress;});if(!repeatedScan.error.isEmpty()||repeatedScan.notes.size()!=768||repeatedProgress!=768)return 105;for(size_t index=0;index<repeatedScan.notes.size();index+=3)if(repeatedScan.notes[index].takeIndex!=1||repeatedScan.notes[index+1].takeIndex!=2||repeatedScan.notes[index+2].takeIndex!=3||repeatedScan.notes[index].midiNote!=repeatedScan.notes[index+2].midiNote||repeatedScan.notes[index].velocity!=repeatedScan.notes[index+2].velocity)return 106;scanSettings.repeatCount=1;
    auto percussionRangeSettings=scanSettings;percussionRangeSettings.velocityLevels={100};percussionRangeSettings.firstMidiNote=35;percussionRangeSettings.lastMidiNote=36;percussionRangeSettings.repeatCount=2;int percussionRangeProgress=0;const auto percussionRangeScan=PluginNoteScanner{}.scan(synthetic,percussionRangeSettings,{},[&](int done,int total,int,int){if(done<1||done>total||total!=4)percussionRangeProgress=-1000;else ++percussionRangeProgress;});if(!percussionRangeScan.error.isEmpty()||percussionRangeScan.notes.size()!=4||percussionRangeProgress!=4)return 292;for(size_t index=0;index<percussionRangeScan.notes.size();++index)if(percussionRangeScan.notes[index].midiNote!=35+static_cast<int>(index/2)||percussionRangeScan.notes[index].takeIndex!=1+static_cast<int>(index%2))return 293;auto invalidNoteRangeSettings=percussionRangeSettings;invalidNoteRangeSettings.firstMidiNote=81;invalidNoteRangeSettings.lastMidiNote=35;if(PluginNoteScanner{}.scan(synthetic,invalidNoteRangeSettings).error.isEmpty())return 294;
    auto cancelledScan=PluginNoteScanner{}.scan(synthetic,scanSettings,[]{return true;});if(!cancelledScan.cancelled||!cancelledScan.notes.empty()||synthetic.getCurrentProgram()!=1)return 41;
    SyntheticInstrument instrumentWithoutStateBlob;instrumentWithoutStateBlob.setCurrentProgram(1);instrumentWithoutStateBlob.setProgramSerializationEnabled(false);instrumentWithoutStateBlob.setProgramChangeOnPrepare(true);const auto restoredProgramScan=PluginNoteScanner{}.scan(instrumentWithoutStateBlob,scanSettings,[]{return true;});if(!restoredProgramScan.cancelled||!restoredProgramScan.error.isEmpty()||instrumentWithoutStateBlob.getCurrentProgram()!=1)return 189;
    auto realtimeSettings=scanSettings;realtimeSettings.velocityLevels={100};realtimeSettings.noteDurationSeconds=.005;realtimeSettings.releaseSeconds=.005;realtimeSettings.captureLengthSeconds=.01;realtimeSettings.renderMode=PluginNoteRenderMode::realtimePaced;synthetic.clearRenderModeObservations();synthetic.setNonRealtime(true);bool stopRealtimeAfterCapture=false;int realtimeCaptures=0;const auto realtimeStart=std::chrono::steady_clock::now();const auto realtimeScan=PluginNoteScanner{}.scan(synthetic,realtimeSettings,[&]{return stopRealtimeAfterCapture;},{},[&](const PluginNoteCapture&){++realtimeCaptures;stopRealtimeAfterCapture=true;return true;});const auto realtimeElapsed=std::chrono::steady_clock::now()-realtimeStart;if(!realtimeScan.error.isEmpty()||!realtimeScan.cancelled||realtimeCaptures!=1||!synthetic.observedRealtimeProcessing()||!synthetic.isNonRealtime()||realtimeElapsed<std::chrono::milliseconds(8))return 184;synthetic.setNonRealtime(false);
    SyntheticInstrument deviceSynthetic;deviceSynthetic.prepareToPlay(8000,64);DeviceRealtimeRenderer deviceRenderer(deviceSynthetic);if(!deviceRenderer.prepareForTesting(8000,64))return 187;juce::AudioBuffer<float> deviceCapture(2,160);deviceCapture.clear();std::atomic<bool> stopDeviceDriver{false};std::thread deviceDriver([&]{float left[64]{},right[64]{};float* outputs[]{left,right};while(!stopDeviceDriver.load(std::memory_order_acquire)&&!deviceRenderer.hasActiveTestRequest())std::this_thread::sleep_for(std::chrono::milliseconds(1));while(!stopDeviceDriver.load(std::memory_order_acquire)){deviceRenderer.invokeTestCallback(outputs,2,64);std::this_thread::sleep_for(std::chrono::milliseconds(1));}});bool deviceCancelled=false;const bool deviceRendered=deviceRenderer.render(36,100,80,deviceCapture,{},deviceCancelled);stopDeviceDriver.store(true,std::memory_order_release);deviceDriver.join();deviceSynthetic.releaseResources();if(!deviceRendered||deviceCancelled||!deviceSynthetic.observedRealtimeProcessing()||deviceCapture.getRMSLevel(0,0,deviceCapture.getNumSamples())<=0.01f)return 188;
    SyntheticInstrument multiOutputSynthetic(64, 10);
    auto multiOutputSettings = scanSettings;
    multiOutputSettings.velocityLevels = { 100 };
    int multiOutputCaptureCount = 0;
    bool multiOutputKickWasAudible = false;
    const auto multiOutputScan = PluginNoteScanner{}.scan(multiOutputSynthetic, multiOutputSettings, {}, {},
        [&](const PluginNoteCapture& capture)
        {
            ++multiOutputCaptureCount;
            if (capture.midiNote == 36)
                multiOutputKickWasAudible = capture.audio.getNumChannels() == 2
                    && capture.peak > 0.01f && capture.rms > 0.01f;
            return true;
        });
    if (!multiOutputScan.error.isEmpty() || multiOutputCaptureCount != 128 || !multiOutputKickWasAudible)
        return 290;

    PluginNoteScanSettings largeStreamingSettings;largeStreamingSettings.sampleRate=192000;largeStreamingSettings.blockSize=8192;largeStreamingSettings.velocityLevels={1,8,16,24,32,40,48,56,64,72,80,88,96,104,112,127};largeStreamingSettings.repeatCount=8;largeStreamingSettings.noteDurationSeconds=10;largeStreamingSettings.releaseSeconds=10;largeStreamingSettings.captureLengthSeconds=20;const auto largeStreamingScan=PluginNoteScanner{}.scan(synthetic,largeStreamingSettings,[]{return true;},{},[](const PluginNoteCapture&){return true;});if(!largeStreamingScan.cancelled||!largeStreamingScan.error.isEmpty())return 182;
    PatternAudioRenderer::Samples previewSamples;for(int instrument=0;instrument<2;++instrument){auto& audio=previewSamples[static_cast<size_t>(instrument)].audio;audio.setSize(1,4410);audio.clear();for(int sample=0;sample<audio.getNumSamples();++sample)audio.setSample(0,sample,0.2f);previewSamples[static_cast<size_t>(instrument)].sampleRate=44100.0;}
    MidiPattern previewPattern;previewPattern.lengthBeats=4.0;previewPattern.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{2.0,juce::MidiMessage::tempoMetaEvent(600000)}};previewPattern.events={{DrumInstrument::Kick,DrumArticulation::Default,127,0.0,0.1},{DrumInstrument::Snare,DrumArticulation::Default,80,1.0,0.1}};
    const auto previewFile=root.getChildFile("pattern-preview.wav");const auto previewResult=PatternAudioRenderer{}.render(previewPattern,previewSamples,previewFile);if(!previewResult.ok||std::abs(previewResult.durationSeconds-2.2)>0.00001)return 75;
    juce::AudioFormatManager previewFormats;previewFormats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> previewReader(previewFormats.createReaderFor(previewFile));if(!previewReader||std::abs(previewReader->lengthInSamples/previewReader->sampleRate-2.2)>0.00003)return 76;juce::AudioBuffer<float> previewAudio(2,static_cast<int>(previewReader->lengthInSamples));if(!previewReader->read(previewAudio.getArrayOfWritePointers(),2,0,static_cast<int>(previewReader->lengthInSamples))||previewAudio.getRMSLevel(0,0,previewAudio.getNumSamples())<0.01f)return 77;
    LoopingAudioSource loopingSource(previewFormats.createReaderFor(previewFile));const auto loopLength=loopingSource.getTotalLength();if(loopLength!=previewReader->lengthInSamples)return 120;loopingSource.setLooping(true);loopingSource.setNextReadPosition(loopLength-32);juce::AudioBuffer<float> loopBlock(2,64);loopingSource.getNextAudioBlock({&loopBlock,0,64});if(loopingSource.getNextReadPosition()!=32)return 121;for(int channel=0;channel<2;++channel)for(int i=0;i<64;++i){const int expectedIndex=i<32?static_cast<int>(loopLength)-32+i:i-32;if(std::abs(loopBlock.getSample(channel,i)-previewAudio.getSample(channel,expectedIndex))>0.00001f)return 122;}loopingSource.setLooping(false);loopingSource.setNextReadPosition(loopLength-8);loopBlock.clear();loopingSource.getNextAudioBlock({&loopBlock,0,16});for(int channel=0;channel<2;++channel)for(int i=8;i<16;++i)if(std::abs(loopBlock.getSample(channel,i))>0.00001f)return 123;
    int previewCancelChecks=0;const auto cancelledPreviewFile=root.getChildFile("cancelled-preview.wav");const auto cancelledPreview=PatternAudioRenderer{}.render(previewPattern,previewSamples,cancelledPreviewFile,[&]{return ++previewCancelChecks>=3;});if(cancelledPreview.ok||cancelledPreview.error!="Preview cancelled."||cancelledPreviewFile.existsAsFile())return 78;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--magdrum-audit-sample-folder") == 0)
    {
        const juce::File root(argv[2]);
        const int limit = std::atoi(argv[3]);
        if (!root.isDirectory() || limit < 1 || limit > 1000)
        {
            std::cerr << "Sample audit requires an existing folder and a limit from 1 to 1000.\n";
            return 274;
        }

        WavScanner::Stats stats;
        int analyzed = 0;
        std::map<std::string, int> categories;
        juce::StringArray suspicious;
        WavScanner{}.scan(root,
            [&analyzed, limit] { return analyzed >= limit; }, {}, &stats,
            [&analyzed, &categories, &suspicious, &root](const ScannedSample& sample)
            {
                ++analyzed;
                ++categories[sample.category.toStdString()];
                if (sample.category == "Snare"
                    && (root.getFileName().containsIgnoreCase("hat")
                        || root.getFileName().containsIgnoreCase("cymbal")))
                    suspicious.add(sample.file.getFileName() + " [confidence "
                        + juce::String(sample.classificationConfidence, 2) + "]");
                return true;
            });

        std::cout << "Root: " << root.getFullPathName() << "\n"
                  << "Analyzed WAV samples: " << analyzed << "\n"
                  << "Errors: " << stats.errors << "\n"
                  << "Categories: ";
        bool first = true;
        for (const auto& [category, count] : categories)
        {
            if (!first) std::cout << ", ";
            first = false;
            std::cout << category << "=" << count;
        }
        std::cout << "\n";
        for (const auto& item : suspicious)
            std::cout << "Possible folder-label conflict: " << item << "\n";
        return analyzed == limit && stats.errors == 0 ? 0 : 275;
    }

    if (argc == 5 && std::strcmp(argv[1], "--magdrum-test-vst3-discovery-timeout-fixture") == 0)
    {
        const juce::File progressFile(argv[2]);
        magdrum::plugins::Vst3DiscoveryProgress progress;
        progress.total = 2;
        progress.current = "First instrument";
        progress.currentPath = argv[3];
        if (!magdrum::plugins::writeVst3DiscoveryProgress(progressFile, progress)) return 307;
        juce::Thread::sleep(200);
        progress.current = "Stalled instrument";
        progress.currentPath = argv[4];
        if (!magdrum::plugins::writeVst3DiscoveryProgress(progressFile, progress)) return 308;
        for (;;) juce::Thread::sleep(25);
    }

    if (argc == 4 && std::strcmp(argv[1], "--magdrum-test-plugin-scan-progress-worker") == 0)
    {
        const juce::File progressFile(argv[2]);
        const juce::File resultFile(argv[3]);
        const auto writeProgress = [&progressFile](const juce::String& phase, int completed, int total)
        {
            auto* progress = new juce::DynamicObject();
            progress->setProperty("phase", phase);
            progress->setProperty("completed", completed);
            progress->setProperty("total", total);
            progress->setProperty("midiNote", completed > 0 ? 36 + completed % 48 : 0);
            progress->setProperty("velocity", 100);
            return progressFile.replaceWithText(juce::JSON::toString(juce::var(progress), true));
        };
        if (!writeProgress("loading", 0, 0)) return 291;
        juce::Thread::sleep(300);
        if (!writeProgress("restoring", 0, 0)) return 292;
        juce::Thread::sleep(300);
        if (!writeProgress("preparing", 0, 128)) return 293;
        juce::Thread::sleep(300);
        if (!writeProgress("scanning", 1, 128)) return 294;
        juce::Thread::sleep(300);
        if (!writeProgress("scanning", 24, 128)) return 295;
        juce::Thread::sleep(300);
        if (!writeProgress("scanning", 128, 128)) return 296;
        juce::Thread::sleep(300);
        return resultFile.replaceWithText(R"({"ok":true,"error":"","cancelled":false,"captures":[]})") ? 0 : 297;
    }

    if (argc == 3 && std::strcmp(argv[1], "--magdrum-test-unresponsive-helper") == 0)
    {
        const juce::File progressFile(argv[2]);
        if (!progressFile.replaceWithText("loading"))
            return 289;
        for (;;)
            juce::Thread::sleep(25);
    }

    if (argc == 3 && std::strcmp(argv[1], "--magdrum-audit-midi-folder") == 0)
    {
        const juce::File root(argv[2]);
        if (!root.isDirectory())
        {
            std::cerr << "MIDI audit root is not a directory.\n";
            return 273;
        }

        constexpr size_t maximumMidiFiles = 200;
        const auto files = magdrum::library::findDrumMidiPatternFiles({root}, maximumMidiFiles, 500000);
        const auto profile = MidiMappingProfile::generalMidi();
        MidiPatternIO midiIO;
        size_t parseFailures = 0;
        size_t emptyPatterns = 0;
        size_t drumEvents = 0;
        size_t unmappedNoteEvents = 0;
        size_t ambiguousNoteEvents = 0;
        std::array<size_t, 128> unmappedByNote{};
        std::array<size_t, 128> ambiguousByNote{};
        for (const auto& file : files)
        {
            MidiPattern pattern;
            const auto report = midiIO.read(file, profile, pattern);
            bool valid = report.ok && std::isfinite(pattern.lengthBeats) && pattern.lengthBeats >= 0.0;
            for (const auto& event : pattern.events)
                valid = valid && std::isfinite(event.positionBeats) && event.positionBeats >= 0.0
                    && std::isfinite(event.durationBeats) && event.durationBeats >= 0.0
                    && event.velocity >= 1 && event.velocity <= 127;
            for (const auto& event : pattern.tempoAndSignature)
                valid = valid && std::isfinite(event.positionBeats) && event.positionBeats >= 0.0;
            if (!valid)
            {
                ++parseFailures;
                std::cerr << "Could not import " << file.getFileName() << ": "
                          << report.error << '\n';
                continue;
            }
            if (pattern.events.empty()) ++emptyPatterns;
            drumEvents += pattern.events.size();
            unmappedNoteEvents += report.unmappedNotes.size();
            for (const auto note : report.unmappedNotes)
                if (juce::isPositiveAndBelow(note, static_cast<int>(unmappedByNote.size())))
                    ++unmappedByNote[static_cast<size_t>(note)];
            for (const auto& ambiguous : report.ambiguousNotes)
            {
                ambiguousNoteEvents += static_cast<size_t>(juce::jmax(0, ambiguous.eventCount));
                if (juce::isPositiveAndBelow(ambiguous.note, static_cast<int>(ambiguousByNote.size())))
                    ambiguousByNote[static_cast<size_t>(ambiguous.note)] += static_cast<size_t>(juce::jmax(0, ambiguous.eventCount));
            }
        }
        std::cout << "MagDrum MIDI corpus audit\n"
                  << "Root: " << root.getFullPathName() << '\n'
                  << "Files discovered: " << files.size() << " (limit " << maximumMidiFiles << ")\n"
                  << "Parse/validation failures: " << parseFailures << '\n'
                  << "Imported drum events: " << drumEvents << '\n'
                  << "Unmapped note events: " << unmappedNoteEvents << '\n'
                  << "Ambiguous note events: " << ambiguousNoteEvents << '\n'
                  << "Files with no mapped drum events: " << emptyPatterns << '\n';
        const auto printTopNotes = [](const char* label, const std::array<size_t, 128>& counts)
        {
            std::vector<std::pair<int, size_t>> ordered;
            for (int note = 0; note < static_cast<int>(counts.size()); ++note)
                if (counts[static_cast<size_t>(note)] > 0)
                    ordered.emplace_back(note, counts[static_cast<size_t>(note)]);
            std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right)
            {
                return left.second == right.second ? left.first < right.first : left.second > right.second;
            });
            if (ordered.empty()) return;
            std::cout << label;
            const auto count = juce::jmin(12, static_cast<int>(ordered.size()));
            for (int index = 0; index < count; ++index)
            {
                const auto [note, events] = ordered[static_cast<size_t>(index)];
                std::cout << (index == 0 ? " " : ", ") << note << " ("
                          << formatMidiNoteName(note) << "): " << events;
            }
            std::cout << '\n';
        };
        printTopNotes("Top unmapped MIDI notes:", unmappedByNote);
        printTopNotes("Top ambiguous MIDI notes:", ambiguousByNote);
        return parseFailures == 0 ? 0 : 1;
    }

#if JUCE_WINDOWS
    if (argc == 3 && std::strcmp(argv[1], "--magdrum-debug-assertion-worker-check") == 0)
    {
#if JUCE_DEBUG
        juce::ChildProcess assertionWorker;
        const juce::StringArray command{juce::File(argv[2]).getFullPathName(),
                                        "--magdrum-debug-assertion-worker"};
        if (!assertionWorker.start(command, 0)) return 270;
        if (!assertionWorker.waitForProcessToFinish(10000))
        {
            assertionWorker.kill();
            assertionWorker.waitForProcessToFinish(3000);
            std::cerr << "A Debug assertion opened a modal dialog or failed to stop its isolated worker.\n";
            return 271;
        }
        if (static_cast<unsigned int>(assertionWorker.getExitCode()) != 0xC0000420u)
        {
            std::cerr << "The isolated Debug worker did not exit with STATUS_ASSERTION_FAILURE.\n";
            return 272;
        }
        std::cout << "Debug assertion terminated only the isolated worker without a dialog.\n";
        return 0;
#else
        std::cout << "Skipped: the CRT assertion-worker check is Debug-only.\n";
        return 77;
#endif
    }
#endif

    if (formatMidiNoteName(36) != "C1" || formatMidiNoteName(38) != "D1"
        || formatMidiNoteName(44) != "G#1" || formatMidiNoteName(56) != "G#2"
        || !formatMidiNoteName(-1).empty() || !formatMidiNoteName(128).empty())
    {
        std::cerr << "MIDI note labels do not match the Cubase octave convention.\n";
        return 249;
    }
    const auto result = runSmokeTests(argc, argv);
    if (result != 0)
        std::cerr << "MagDrumTests failed with diagnostic code " << result << ".\n";
    return result;
}
