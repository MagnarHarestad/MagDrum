#include "core/SampleDatabase.h"
#include "core/WavScanner.h"
#include "core/DefaultLibraryDiscovery.h"
#include "core/AudioClassifier.h"
#include "core/MidiMapping.h"
#include "core/MidiPatternIO.h"
#include "core/SequencerGrid.h"
#include "core/PluginNoteScanner.h"
#include "core/DeviceRealtimeRenderer.h"
#include "core/PluginScanProcess.h"
#include "core/PluginPrograms.h"
#include "core/PluginPresetFiles.h"
#include "core/PatternAudioRenderer.h"
#include "core/LoopingAudioSource.h"
#include "core/PluginVelocityLayerAnalyzer.h"
#include "core/PluginRoundRobinAnalyzer.h"
#include "core/SampleFeatureFilter.h"
#include "core/SampleClassificationReview.h"
#include "core/StructuredLogger.h"
#include <iostream>
#include <array>
#include <algorithm>
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
    SyntheticInstrument() : AudioProcessor(BusesProperties().withOutput("Stereo Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Synthetic Test Instrument"; }
    void prepareToPlay(double rate, int) override { sampleRate = rate; phase = 0.0; }
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
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel) buffer.setSample(channel, i, value);
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
    const juce::String getProgramName(int index) override { return index == 0 ? "Factory Kit A" : juce::String(); }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override { destination.reset(); destination.append(&program, sizeof(program)); }
    void setStateInformation(const void* data, int size) override { if (data != nullptr && size == static_cast<int>(sizeof(program))) std::memcpy(&program, data, sizeof(program)); }
    bool observedOfflineProcessing() const { return sawOfflineProcessing; }
    bool observedRealtimeProcessing() const { return sawRealtimeProcessing; }
    void clearRenderModeObservations() { sawOfflineProcessing = false; sawRealtimeProcessing = false; }
private:
    double sampleRate = 44100.0, phase = 0.0;
    float amplitude = 0.0f;
    int activeNote = -1, program = 0;
    bool sawOfflineProcessing = false, sawRealtimeProcessing = false;
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
    const juce::File& sampleFile, const juce::File& root)
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
    stateCommand.add(sampleFile.getFullPathName());
    stateCommand.add(stateFile.getFullPathName());
    juce::ChildProcess stateWriter;
    if (!stateWriter.start(stateCommand) || !stateWriter.waitForProcessToFinish(30000)
        || stateWriter.getExitCode() != 0 || !stateFile.existsAsFile())
    { std::cerr << "Could not create a host-format VST3 state fixture.\n"; return false; }

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
    workerCommand.add(pluginTestsExecutable.getFullPathName());
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

int main(int argc,char** argv)
{
    if (magdrum::sequencer::stepsPerBar(3,4) != 12
        || magdrum::sequencer::stepsPerBar(5,4) != 20
        || magdrum::sequencer::stepsPerBar(7,8) != 14
        || magdrum::sequencer::stepsPerBar(12,8) != 24
        || magdrum::sequencer::stepsPerBar(8,4) != 32
        || magdrum::sequencer::stepsPerBar(9,4) != 0
        || magdrum::sequencer::maximumSteps != 512)
        return 230;
    if(argc==2&&std::strcmp(argv[1],"--magdrum-test-crash-fixture")==0)std::abort();
    if(argc==3&&std::strcmp(argv[1],"--magdrum-test-partial-crash-fixture")==0){const auto log=juce::File(argv[2]);log.replaceWithText(R"({"midiNote":36,"velocity":100,"takeIndex":1,"channels":2,"sampleCount":2048,"sampleRate":44100,"peak":0.5,"rms":0.2,"silent":false,"fileName":"note_036_velocity_100_take_001.wav"})"+juce::String("\n"));std::abort();}
    if(!needsClassificationReview(false,"Unknown",0.99)||!needsClassificationReview(false,"Kick",0.74)||needsClassificationReview(false,"Kick",0.75)||needsClassificationReview(true,"Unknown",0.0)||!needsClassificationReview(false,"Snare",std::numeric_limits<double>::quiet_NaN()))return 128;
    if(!matchesSampleFeatureFilter(SampleFeatureFilter::veryShort,0.099,500.0)||matchesSampleFeatureFilter(SampleFeatureFilter::veryShort,0.1,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::shortSample,0.1,500.0)||matchesSampleFeatureFilter(SampleFeatureFilter::shortSample,0.5,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::mediumSample,0.5,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::longSample,2.0,500.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::dark,1.0,999.9)||matchesSampleFeatureFilter(SampleFeatureFilter::dark,1.0,1000.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::midBrightness,1.0,1000.0)||!matchesSampleFeatureFilter(SampleFeatureFilter::bright,1.0,3000.0)||matchesSampleFeatureFilter(SampleFeatureFilter::bright,1.0,0.0))return 110;
    const auto logLine=StructuredLogger::formatEvent(StructuredLogger::Level::warning,"test.event",{{"detail","quoted \"text\"\nline"}},"2026-01-02T03:04:05Z");const auto parsedLog=juce::JSON::parse(logLine);if(!parsedLog.isObject()||parsedLog.getDynamicObject()->getProperty("level").toString()!="warning"||parsedLog.getDynamicObject()->getProperty("event").toString()!="test.event"||parsedLog.getDynamicObject()->getProperty("detail").toString()!="quoted \"text\"\nline")return 114;
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrumScannerTest");
    root.deleteRecursively();
    if(!root.createDirectory().wasOk())return 1;
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
    midiFixture.deleteRecursively();
    juce::StringArray crashCommand;crashCommand.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());crashCommand.add("--magdrum-test-crash-fixture");
    const auto crashExecution=PluginScanProcess::monitorWorker(crashCommand,root.getChildFile("crash-progress.json"),root.getChildFile("crash-result.json"),root.getChildFile("crash-cancel"));
    if(!crashExecution.started||crashExecution.manifestRead||crashExecution.terminatedForCancellation||crashExecution.exitCode==0)return 144;
    juce::StringArray partialCrashCommand;partialCrashCommand.add(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName());partialCrashCommand.add("--magdrum-test-partial-crash-fixture");const auto partialCaptureLog=root.getChildFile("partial-captures.jsonl");partialCrashCommand.add(partialCaptureLog.getFullPathName());
    const auto partialCrashExecution=PluginScanProcess::monitorWorker(partialCrashCommand,root.getChildFile("partial-progress.json"),root.getChildFile("partial-result.json"),root.getChildFile("partial-cancel"),{}, {},10000,partialCaptureLog);
    if(!partialCrashExecution.started||!partialCrashExecution.manifestRead||!partialCrashExecution.manifest.partial||partialCrashExecution.manifest.succeeded||partialCrashExecution.manifest.captures.size()!=1||partialCrashExecution.exitCode==0)return 180;
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
    auto stream=waveFile.createOutputStream();
    if(!stream)return 2;
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(),44100.0,1u,16,{},0));
    if(!writer)return 3;
    stream.release();
    float samples[4096]{};
    for (int i=0;i<4096;++i) samples[i]=0.5f*std::sin(2.0*juce::MathConstants<double>::pi*440.0*i/44100.0);
    const float* channels[]{samples};
    if(!writer->writeFromFloatArrays(channels,1,4096))return 4;
    writer.reset();

    if (argc == 3 && !runBuiltVst3WorkerFixture(juce::File(argv[1]), juce::File(argv[2]), waveFile, root))
        return 188;

    WavScanner scanner;
    WavScanner::Stats initialScanStats;
    auto scanned=scanner.scan(root,{}, {},&initialScanStats);
    if(scanned.size()!=1||scanned.getFirst().file!=waveFile)return 5;
    if(initialScanStats.filesConsidered!=1||initialScanStats.filesAnalyzed!=1||initialScanStats.errors!=0)return 44;
    if(scanned.getFirst().channels!=1||scanned.getFirst().sampleRate!=44100.0)return 6;
    if(scanned.getFirst().format!="WAV"||scanned.getFirst().bitDepth!=16||scanned.getFirst().fingerprint.isEmpty())return 12;
    if(scanned.getFirst().acousticFingerprint.length()!=128)return 56;
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
    WavScanner::Stats formatScanStats;const auto additionalFormatSamples=scanner.scan(formatRoot,{}, {},&formatScanStats);
    juce::StringArray scannedFormats;for(const auto& formatSample:additionalFormatSamples){scannedFormats.addIfNotAlreadyThere(formatSample.format);if(formatSample.channels!=1||formatSample.sampleRate!=44100.0||formatSample.durationSeconds<=0.0||formatSample.peak<0.35||formatSample.acousticFingerprint.length()!=128)return 147;}
    if(additionalFormatSamples.size()!=3||formatScanStats.filesConsidered!=3||formatScanStats.filesAnalyzed!=3||formatScanStats.errors!=0||!scannedFormats.contains("WAV")||!scannedFormats.contains("AIFF")||!scannedFormats.contains("FLAC"))return 148;

    const auto classificationTagRoot=root.getSiblingFile("MagDrumClassificationTagTest");classificationTagRoot.deleteRecursively();if(!classificationTagRoot.createDirectory().wasOk())return 149;
    const auto classifiedFile=classificationTagRoot.getChildFile("808_Open_HiHat_deep.wav");if(!waveFile.copyFileTo(classifiedFile))return 150;
    const auto classifiedSamples=scanner.scan(classificationTagRoot);if(classifiedSamples.size()!=1||classifiedSamples.getFirst().category!="Open hi-hat"||!classifiedSamples.getFirst().classificationTags.contains("808")||!classifiedSamples.getFirst().classificationTags.contains("Electronic")||!classifiedSamples.getFirst().classificationTags.contains("Deep"))return 151;
    SampleDatabase classificationTagDb;if(!classificationTagDb.open(classificationTagRoot.getChildFile("classification-tags.sqlite3"))||!classificationTagDb.insertSample(classifiedSamples.getFirst()))return 152;
    const auto storedClassificationTags=classificationTagDb.getClassificationTags(classifiedFile);if(!storedClassificationTags.contains("808")||!storedClassificationTags.contains("Electronic")||!storedClassificationTags.contains("Deep"))return 153;
    SampleBrowseFilter classificationTagFilter;classificationTagFilter.query="Electronic";const auto classifierTagSearch=classificationTagDb.searchSampleBrowseRecords(classificationTagFilter,10,0);if(classifierTagSearch.records.size()!=1||!classifierTagSearch.records[0].classificationTags.contains("Electronic"))return 154;
    if(classificationTagDb.searchSampleBrowseRecords("Deep",juce::String(),10,0).size()!=1)return 155;
    juce::StringArray manualTags;manualTags.add("Reviewed");if(!classificationTagDb.setUserTags(classifiedFile,manualTags)||!classificationTagDb.setUserCategory(classifiedFile,"Snare"))return 156;
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
    SampleDatabase migrated;if(!migrated.open(legacyDbFile)||migrated.getAllPaths().size()!=1||!migrated.setUserCategory(waveFile,"Kick"))return 38;const auto migratedBrowse=migrated.getSampleBrowseRecords();if(migratedBrowse.size()!=1||migratedBrowse.getFirst().sourceType!="folder"||migratedBrowse.getFirst().sourceIdentifier!=root.getFullPathName())return 109;
    if(migrated.isSampleUnchanged(waveFile,static_cast<std::int64_t>(waveFile.getSize()),waveFile.getLastModificationTime().toMilliseconds()))return 63;
    if(sqlite3_open(legacyDbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 90;sqlite3_stmt* migratedColumns=nullptr;if(sqlite3_prepare_v2(legacy,"PRAGMA table_info(plugin_sounds)",-1,&migratedColumns,nullptr)!=SQLITE_OK)return 91;bool hasAcousticFingerprint=false,hasPluginSpectralFlatness=false,hasPluginTransientCount=false;while(sqlite3_step(migratedColumns)==SQLITE_ROW){const auto* columnName=reinterpret_cast<const char*>(sqlite3_column_text(migratedColumns,1));if(columnName!=nullptr&&std::strcmp(columnName,"acoustic_fingerprint")==0)hasAcousticFingerprint=true;if(columnName!=nullptr&&std::strcmp(columnName,"spectral_flatness")==0)hasPluginSpectralFlatness=true;if(columnName!=nullptr&&std::strcmp(columnName,"transient_count")==0)hasPluginTransientCount=true;}sqlite3_finalize(migratedColumns);if(!hasAcousticFingerprint||!hasPluginSpectralFlatness||!hasPluginTransientCount)return 92;
    sqlite3_stmt* migratedVersion=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT version FROM schema_info WHERE singleton=1",-1,&migratedVersion,nullptr)!=SQLITE_OK)return 93;const bool schemaUpgraded=sqlite3_step(migratedVersion)==SQLITE_ROW&&sqlite3_column_int(migratedVersion,0)==16;sqlite3_finalize(migratedVersion);sqlite3_close(legacy);if(!schemaUpgraded)return 94;
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
    const auto browseRecords=db.getSampleBrowseRecords();if(browseRecords.size()!=1||browseRecords.getFirst().file!=sample.file||browseRecords.getFirst().durationSeconds<=0.0||browseRecords.getFirst().category.isEmpty()||browseRecords.getFirst().sourceType!="folder"||browseRecords.getFirst().sourceName!=root.getFileName())return 68;
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
    databasePageFilter.query.clear();databasePageFilter.sourceType="folder";databasePageFilter.sourceIdentifier=pagedBrowseRoot.getFullPathName();const auto sourceDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(sourceDatabasePage.records.size()!=4||sourceDatabasePage.records[0].sourceType!="folder")return 156;
    databasePageFilter.sourceType.clear();databasePageFilter.sourceIdentifier.clear();databasePageFilter.feature=2;const auto shortDurationPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(shortDurationPage.records.size()!=1||shortDurationPage.records[0].file.getFileName()!="Alpha.wav")return 159;
    databasePageFilter.feature=5;databasePageFilter.sort=3;const auto longDurationPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(longDurationPage.records.size()!=1||longDurationPage.records[0].file.getFileName()!="Gamma.wav")return 160;
    auto reviewSample=sample;reviewSample.file=pagedBrowseRoot.getChildFile("NeedsReview.wav");reviewSample.category="Unknown";reviewSample.classificationConfidence=0.2;reviewSample.fileSizeBytes=waveFile.getSize();reviewSample.modifiedTimeMillis=waveFile.getLastModificationTime().toMilliseconds();if(!waveFile.copyFileTo(reviewSample.file)||!pagedBrowseDb.insertSample(reviewSample))return 161;
    databasePageFilter.feature=1;databasePageFilter.sort=1;databasePageFilter.needsReview=true;const auto reviewDatabasePage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,10,0);if(reviewDatabasePage.records.size()!=1||reviewDatabasePage.records[0].file.getFileName()!="NeedsReview.wav"||reviewDatabasePage.records[0].manuallyClassified)return 162;
    databasePageFilter.needsReview=false;databasePageFilter.sort=3;const auto durationSortedPage=pagedBrowseDb.searchSampleBrowseRecords(databasePageFilter,2,0);if(durationSortedPage.records.size()!=2||durationSortedPage.records[0].file.getFileName()!="Gamma.wav"||durationSortedPage.records[1].file.getFileName()!="Delta.wav"||!durationSortedPage.hasMore)return 163;
    const auto largeBrowseFile=pagedBrowseRoot.getSiblingFile("MagDrumLargeBrowseTest.sqlite3");largeBrowseFile.deleteFile();SampleDatabase largeBrowseDb;if(!largeBrowseDb.open(largeBrowseFile))return 164;sqlite3* largeFixture=nullptr;if(sqlite3_open(largeBrowseFile.getFullPathName().toRawUTF8(),&largeFixture)!=SQLITE_OK)return 165;
    sqlite3_stmt* browseIndices=nullptr;if(sqlite3_prepare_v2(largeFixture,"SELECT count(*) FROM sqlite_master WHERE type='index' AND name IN ('samples_name_path','samples_duration_name_path','samples_centroid_name_path')",-1,&browseIndices,nullptr)!=SQLITE_OK)return 214;const bool hasBrowseIndices=sqlite3_step(browseIndices)==SQLITE_ROW&&sqlite3_column_int(browseIndices,0)==3;sqlite3_finalize(browseIndices);if(!hasBrowseIndices)return 215;
    sqlite3_stmt* nameBrowsePlan=nullptr;if(sqlite3_prepare_v2(largeFixture,"EXPLAIN QUERY PLAN SELECT path FROM samples ORDER BY name COLLATE NOCASE,path COLLATE NOCASE LIMIT 251",-1,&nameBrowsePlan,nullptr)!=SQLITE_OK)return 216;bool namePlanUsesIndex=false;while(sqlite3_step(nameBrowsePlan)==SQLITE_ROW){const auto* detail=reinterpret_cast<const char*>(sqlite3_column_text(nameBrowsePlan,3));if(detail!=nullptr&&std::strstr(detail,"samples_name_path")!=nullptr)namePlanUsesIndex=true;}sqlite3_finalize(nameBrowsePlan);if(!namePlanUsesIndex)return 217;
    if(sqlite3_exec(largeFixture,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK)return 166;sqlite3_stmt* largeInsert=nullptr;if(sqlite3_prepare_v2(largeFixture,"INSERT INTO samples(path,name,duration,channels,sample_rate,spectral_centroid,category,classification_confidence) VALUES(?,?,0.2,1,44100,1800,?,0.8)",-1,&largeInsert,nullptr)!=SQLITE_OK)return 167;bool largeInsertOk=true;for(int index=0;index<250000;++index){const auto name="Sample "+juce::String(index).paddedLeft('0',6);const auto path="/magdrum-large-fixture/"+name+".wav";sqlite3_reset(largeInsert);sqlite3_clear_bindings(largeInsert);sqlite3_bind_text(largeInsert,1,path.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(largeInsert,2,name.toRawUTF8(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(largeInsert,3,index%2==0?"Kick":"Snare",-1,SQLITE_STATIC);if(sqlite3_step(largeInsert)!=SQLITE_DONE){largeInsertOk=false;break;}}sqlite3_finalize(largeInsert);const bool largeCommit=sqlite3_exec(largeFixture,largeInsertOk?"COMMIT":"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(largeFixture);if(!largeInsertOk||!largeCommit)return 168;SampleBrowseFilter largeFilter;const auto largeFirst=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,0),largeMiddle=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,124750),largeLast=largeBrowseDb.searchSampleBrowseRecords(largeFilter,250,249750);if(largeFirst.records.size()!=250||!largeFirst.hasMore||largeFirst.records[0].file.getFileName()!="Sample 000000.wav"||largeMiddle.records.size()!=250||largeMiddle.records[0].file.getFileName()!="Sample 124750.wav"||largeLast.records.size()!=250||largeLast.hasMore||largeLast.records[0].file.getFileName()!="Sample 249750.wav"||largeLast.records[249].file.getFileName()!="Sample 249999.wav")return 169;
    const auto pluginBrowse=db.getPluginSoundBrowseRecords();if(pluginBrowse.size()!=1||pluginBrowse.getFirst().previewFile!=sample.file||pluginBrowse.getFirst().pluginIdentifier!=instrument.identifier||pluginBrowse.getFirst().pluginName!=instrument.name||pluginBrowse.getFirst().kitName!="Test Kit"||pluginBrowse.getFirst().midiNote!=36||pluginBrowse.getFirst().velocity!=100)return 69;
    SampleBrowseFilter pluginPageFilter;pluginPageFilter.pluginName=instrument.name;pluginPageFilter.kitName="Test Kit";const auto pluginDatabasePage=db.searchSampleBrowseRecords(pluginPageFilter,10,0);if(pluginDatabasePage.records.size()!=1||pluginDatabasePage.records[0].pluginIdentifier!=instrument.identifier||pluginDatabasePage.records[0].midiNote!=36||pluginDatabasePage.records[0].velocity!=100)return 157;
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
    ScannedSample kickLike; kickLike.durationSeconds=.5; kickLike.rms=.2; kickLike.peak=.8; kickLike.spectralCentroidHz=700; kickLike.transientStrength=.12; kickLike.fundamentalHz=90; kickLike.pitchConfidence=.8;
    auto prediction=AudioClassifier{}.classify(kickLike);
    if(prediction.category!="Kick"||prediction.confidence<=0.0||prediction.confidence>1.0)return 13;
    auto tonalPercussion=kickLike;tonalPercussion.spectralCentroidHz=2500.0;tonalPercussion.spectralFlatness=0.3;tonalPercussion.fundamentalHz=180.0;
    if(AudioClassifier{}.classify(tonalPercussion).category!="Snare")return 208;
    auto tomLike=kickLike;tomLike.fundamentalHz=180.0;tomLike.spectralCentroidHz=700.0;const auto tomPrediction=AudioClassifier{}.classify(tomLike);
    if(tomPrediction.category!="Tom"||tomPrediction.confidence<=0.0||tomPrediction.confidence>=0.75)return 209;
    if(!prediction.tags.contains("Dark"))return 193;
    auto brightFeatureSample=kickLike;brightFeatureSample.spectralCentroidHz=3500.0;
    if(!AudioClassifier{}.classify(brightFeatureSample).tags.contains("Bright"))return 194;
    auto loudShortSample=kickLike;loudShortSample.durationSeconds=0.1;loudShortSample.rms=0.3;loudShortSample.bitDepth=0;
    if(!AudioClassifier{}.classify(loudShortSample).tags.contains("Short"))return 195;
    auto descriptiveTagSample=kickLike;descriptiveTagSample.file=juce::File("Distorted_909_Snare.wav");
    const auto descriptiveTags=AudioClassifier{}.classify(descriptiveTagSample);
    if(descriptiveTags.category!="Snare"||!descriptiveTags.tags.contains("Distorted")||!descriptiveTags.tags.contains("909"))return 196;
    const std::array<std::pair<const char*, const char*>, 8> sfzNameClasses{{
        {"hhc_vl1_rr1.flac", "Closed hi-hat"}, {"hho_vl1_rr1.flac", "Open hi-hat"},
        {"hhp_vl1_rr1.flac", "Pedal hi-hat"}, {"tomhi_vl1_rr1.flac", "Tom"},
        {"tomlo_vl1_rr1.flac", "Tom"}, {"ridebell_vl1_rr1.flac", "Ride"},
        {"sidestick_vl1_rr1.flac", "Rimshot"}, {"kick_vl1_rr1.flac", "Kick"}}};
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
    const std::array<std::pair<const char*,const char*>,5> drumAliases{{
        {"BD_01.wav","Kick"},{"SD_Center.wav","Snare"},{"CHH_Closed.wav","Closed hi-hat"},
        {"OHH_Open.wav","Open hi-hat"},{"PHH_Foot.wav","Pedal hi-hat"}}};
    for(const auto& [filename,category] : drumAliases){filenameOnly.file=juce::File(filename);if(AudioClassifier{}.classify(filenameOnly).category!=category)return 197;}
    filenameOnly.file=juce::File("pride.wav");if(AudioClassifier{}.classify(filenameOnly).category!="Unknown")return 142;
    filenameOnly.file=juce::File("sidekick.wav");if(AudioClassifier{}.classify(filenameOnly).category!="Unknown")return 143;
    ScannedSample ambiguousName=kickLike;ambiguousName.file=juce::File("kick_snare.wav");
    if(AudioClassifier{}.classify(ambiguousName).category!="Unknown")return 65;
    const AudioClassifier modelClassifier(std::make_shared<FixtureClassificationModel>());
    const auto modelPrediction=modelClassifier.classify(kickLike);
    if(modelPrediction.category!="Clap"||std::abs(modelPrediction.confidence-0.83)>0.0001||modelPrediction.tags.size()!=1)return 54;
    const AudioClassifier invalidModelClassifier(std::make_shared<FixtureClassificationModel>("unsupported",1.2));
    if(invalidModelClassifier.classify(kickLike).category!="Unknown")return 55;
    if(!db.setUserCategory(waveFile,"Snare"))return 14;
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
    if(gm.mappingsForMidiNote(42).size()!=1||gm.mappingsForMidiNote(38).size()!=2)return 133;
    DrumEvent unsupported{DrumInstrument::HiHat,DrumArticulation::Edge,80,0.0,0.1};
    if(gm.toMidi(unsupported,false).note)return 21;
    auto fallback=gm.toMidi(unsupported,true);
    if(!fallback.note||!fallback.usedFallback||fallback.mappedArticulation!=DrumArticulation::Closed)return 22;
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
    if(decodedPattern.events[0].velocity!=96||std::abs(decodedPattern.events[0].positionBeats-1.5)>0.001||std::abs(decodedPattern.events[0].durationBeats-.125)>0.002)return 25;
    const auto exactPatternDiff=compareMidiPatterns(pattern,decodedPattern);
    if(!exactPatternDiff.removedEvents.empty()||!exactPatternDiff.addedEvents.empty()||!exactPatternDiff.removedMetadata.empty()||!exactPatternDiff.addedMetadata.empty()||exactPatternDiff.lengthChanged){std::cerr<<"Exact MIDI round-trip diff: removed="<<exactPatternDiff.removedEvents.size()<<", added="<<exactPatternDiff.addedEvents.size()<<", removed metadata="<<exactPatternDiff.removedMetadata.size()<<", added metadata="<<exactPatternDiff.addedMetadata.size()<<", length changed="<<exactPatternDiff.lengthChanged<<"\n";return 135;}
    auto editedPattern=decodedPattern;editedPattern.events[0].velocity=80;editedPattern.events.push_back({DrumInstrument::Snare,DrumArticulation::Center,90,2.0,0.25});editedPattern.lengthBeats=10.0;if(!setPatternTempo(editedPattern,120))return 136;const auto editedPatternDiff=compareMidiPatterns(decodedPattern,editedPattern);
    if(editedPatternDiff.removedEvents.size()!=1||editedPatternDiff.addedEvents.size()!=2||editedPatternDiff.removedMetadata.size()!=1||editedPatternDiff.addedMetadata.size()!=1||!editedPatternDiff.lengthChanged){std::cerr<<"Edited MIDI diff: removed="<<editedPatternDiff.removedEvents.size()<<", added="<<editedPatternDiff.addedEvents.size()<<", removed metadata="<<editedPatternDiff.removedMetadata.size()<<", added metadata="<<editedPatternDiff.addedMetadata.size()<<", length changed="<<editedPatternDiff.lengthChanged<<"\n";return 137;}
    MidiMappingProfile ambiguousProfile({{DrumInstrument::HiHat,DrumArticulation::Closed,42},{DrumInstrument::Snare,DrumArticulation::Center,42},{DrumInstrument::HiHat,DrumArticulation::Closed,42}});
    MidiPattern ambiguousPattern;const auto ambiguousReport=midiIO.read(midiFile,ambiguousProfile,ambiguousPattern);
    if(!ambiguousReport.ok||ambiguousReport.ambiguousNotes.size()!=1||ambiguousReport.ambiguousNotes[0].note!=42||ambiguousReport.ambiguousNotes[0].eventCount!=1||ambiguousReport.ambiguousNotes[0].candidates.size()!=2||ambiguousPattern.events.size()!=1||ambiguousPattern.events[0].instrument!=DrumInstrument::HiHat)return 134;
    MidiPattern emptyBars;emptyBars.lengthBeats=12.0;const auto emptyBarsFile=root.getChildFile("empty-bars.mid");if(!midiIO.write(emptyBarsFile,emptyBars,gm,false))return 72;MidiPattern decodedEmptyBars;if(!midiIO.read(emptyBarsFile,gm,decodedEmptyBars).ok||!decodedEmptyBars.events.empty()||std::abs(decodedEmptyBars.lengthBeats-12.0)>0.001)return 73;
    MidiPattern metadataEdit;metadataEdit.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{0.0,juce::MidiMessage::timeSignatureMetaEvent(4,4)},{8.0,juce::MidiMessage::tempoMetaEvent(750000)},{8.0,juce::MidiMessage::timeSignatureMetaEvent(3,4)}};
    if(!setPatternTempo(metadataEdit,100)||!setPatternTimeSignature(metadataEdit,5,8)||setPatternTempo(metadataEdit,10)||setPatternTimeSignature(metadataEdit,3,3)||metadataEdit.tempoAndSignature.size()!=4)return 64;
    bool startTempo=false,startSignature=false,laterTempo=false,laterSignature=false;for(const auto& meta:metadataEdit.tempoAndSignature){if(meta.positionBeats==0.0&&meta.message.isTempoMetaEvent())startTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.6)<0.000001;if(meta.positionBeats==0.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);startSignature=n==5&&d==8;}if(meta.positionBeats==8.0&&meta.message.isTempoMetaEvent())laterTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.75)<0.000001;if(meta.positionBeats==8.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);laterSignature=n==3&&d==4;}}if(!startTempo||!startSignature||!laterTempo||!laterSignature)return 65;
    auto metadataRemoval=metadataEdit;if(!removePatternTempo(metadataRemoval,8.0)||removePatternTempo(metadataRemoval,8.0)||metadataRemoval.tempoAndSignature.size()!=3||!removePatternTimeSignature(metadataRemoval,8.0)||metadataRemoval.tempoAndSignature.size()!=2||removePatternTimeSignature(metadataRemoval,std::numeric_limits<double>::quiet_NaN()))return 68;
    const auto metadataFile=root.getChildFile("metadata.mid");if(!midiIO.write(metadataFile,metadataEdit,gm,false))return 66;MidiPattern metadataRoundTrip;if(!midiIO.read(metadataFile,gm,metadataRoundTrip).ok||metadataRoundTrip.tempoAndSignature.size()!=4)return 67;
    std::vector<MidiNoteMapping> roundTripMappings;for(const auto& mapping:gm.entries())if(std::none_of(roundTripMappings.begin(),roundTripMappings.end(),[&](const auto& existing){return existing.note==mapping.note;})){const auto customNote=static_cast<std::uint8_t>(24+roundTripMappings.size());roundTripMappings.push_back({mapping.instrument,mapping.articulation,customNote});}MidiMappingProfile customRoundTripProfile(roundTripMappings);std::uint32_t midiRandom=0x5a17c9e3u;const auto nextMidiRandom=[&midiRandom](){midiRandom=midiRandom*1664525u+1013904223u;return midiRandom;};
    constexpr int variedMidiPatterns=24,variedMidiEvents=64;for(int patternIndex=0;patternIndex<variedMidiPatterns;++patternIndex){MidiPattern varied;varied.lengthBeats=16.0;const int initialTempo=47+static_cast<int>(nextMidiRandom()%200u),laterTempo=47+static_cast<int>(nextMidiRandom()%200u);const int numeratorA=3+static_cast<int>(nextMidiRandom()%6u),numeratorB=3+static_cast<int>(nextMidiRandom()%6u);const int denominators[]{4,8,16};if(!setPatternTempo(varied,initialTempo)||!setPatternTimeSignature(varied,numeratorA,denominators[nextMidiRandom()%3u])||!setPatternTempo(varied,laterTempo,8.0)||!setPatternTimeSignature(varied,numeratorB,denominators[nextMidiRandom()%3u],8.0))return 218;for(int eventIndex=0;eventIndex<variedMidiEvents;++eventIndex){const auto& mapping=roundTripMappings[nextMidiRandom()%roundTripMappings.size()];const auto velocity=static_cast<std::uint8_t>(1+nextMidiRandom()%127u);const auto durationTicks=1+static_cast<int>(nextMidiRandom()%96u);varied.events.push_back({mapping.instrument,mapping.articulation,velocity,static_cast<double>(eventIndex)*0.25,static_cast<double>(durationTicks)/960.0});}const auto variedFile=root.getChildFile("varied-roundtrip-"+juce::String(patternIndex)+".mid");if(!midiIO.write(variedFile,varied,customRoundTripProfile,false))return 219;MidiPattern variedDecoded;const auto variedReport=midiIO.read(variedFile,customRoundTripProfile,variedDecoded);const auto variedDiff=compareMidiPatterns(varied,variedDecoded);if(!variedReport.ok||!variedReport.unmappedNotes.empty()||!variedReport.ambiguousNotes.empty()||!variedDiff.removedEvents.empty()||!variedDiff.addedEvents.empty()||!variedDiff.removedMetadata.empty()||!variedDiff.addedMetadata.empty()||variedDiff.lengthChanged){std::cerr<<"Varied MIDI round-trip failed at case "<<patternIndex<<" (removed="<<variedDiff.removedEvents.size()<<", added="<<variedDiff.addedEvents.size()<<", metadata-="<<variedDiff.removedMetadata.size()<<", metadata+="<<variedDiff.addedMetadata.size()<<")\\n";return 220;}}
    SyntheticInstrument synthetic;synthetic.setCurrentProgram(1);const auto programs=discoverPluginPrograms(synthetic);if(programs.programs.size()!=2||programs.programs[0].index!=0||programs.programs[0].name!="Factory Kit A"||programs.programs[1].name!="Program 2"||synthetic.getCurrentProgram()!=1)return 70;const auto limitedPrograms=discoverPluginPrograms(synthetic,1);if(!limitedPrograms.exceedsLimit||!limitedPrograms.programs.empty())return 71;PluginNoteScanSettings scanSettings;scanSettings.sampleRate=8000;scanSettings.blockSize=64;scanSettings.velocityLevels={64,127};scanSettings.noteDurationSeconds=.01;scanSettings.releaseSeconds=.01;
    int progress=0;auto noteScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{},[&](int done,int total,int,int){if(done<1||done>total)progress=-1000;else ++progress;});
    if(!noteScan.error.isEmpty()||noteScan.notes.size()!=256||progress!=256||synthetic.getCurrentProgram()!=1)return 39;if(noteScan.notes.empty()||noteScan.notes.front().sampleRate!=scanSettings.sampleRate)return 185;
    if(!synthetic.observedOfflineProcessing())return 183;
    int silentNotes=0,activeNotes=0;for(const auto&capture:noteScan.notes){if(capture.midiNote==60){if(capture.silent)silentNotes++;}else if(capture.velocity==127&&!capture.silent&&capture.peak>0.1)activeNotes++;}
    if(silentNotes!=2||activeNotes!=127)return 40;
    int streamedCaptureCount=0;const auto streamedNoteScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{}, {},[&](const PluginNoteCapture&){++streamedCaptureCount;return true;});if(!streamedNoteScan.error.isEmpty()||!streamedNoteScan.notes.empty()||streamedCaptureCount!=256)return 181;
    scanSettings.repeatCount=3;int repeatedProgress=0;auto repeatedScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{},[&](int done,int total,int,int){if(done<1||done>total)repeatedProgress=-1000;else ++repeatedProgress;});if(!repeatedScan.error.isEmpty()||repeatedScan.notes.size()!=768||repeatedProgress!=768)return 105;for(size_t index=0;index<repeatedScan.notes.size();index+=3)if(repeatedScan.notes[index].takeIndex!=1||repeatedScan.notes[index+1].takeIndex!=2||repeatedScan.notes[index+2].takeIndex!=3||repeatedScan.notes[index].midiNote!=repeatedScan.notes[index+2].midiNote||repeatedScan.notes[index].velocity!=repeatedScan.notes[index+2].velocity)return 106;scanSettings.repeatCount=1;
    auto cancelledScan=PluginNoteScanner{}.scan(synthetic,scanSettings,[]{return true;});if(!cancelledScan.cancelled||!cancelledScan.notes.empty()||synthetic.getCurrentProgram()!=1)return 41;
    auto realtimeSettings=scanSettings;realtimeSettings.velocityLevels={100};realtimeSettings.noteDurationSeconds=.005;realtimeSettings.releaseSeconds=.005;realtimeSettings.captureLengthSeconds=.01;realtimeSettings.renderMode=PluginNoteRenderMode::realtimePaced;synthetic.clearRenderModeObservations();synthetic.setNonRealtime(true);bool stopRealtimeAfterCapture=false;int realtimeCaptures=0;const auto realtimeStart=std::chrono::steady_clock::now();const auto realtimeScan=PluginNoteScanner{}.scan(synthetic,realtimeSettings,[&]{return stopRealtimeAfterCapture;},{},[&](const PluginNoteCapture&){++realtimeCaptures;stopRealtimeAfterCapture=true;return true;});const auto realtimeElapsed=std::chrono::steady_clock::now()-realtimeStart;if(!realtimeScan.error.isEmpty()||!realtimeScan.cancelled||realtimeCaptures!=1||!synthetic.observedRealtimeProcessing()||!synthetic.isNonRealtime()||realtimeElapsed<std::chrono::milliseconds(8))return 184;synthetic.setNonRealtime(false);
    SyntheticInstrument deviceSynthetic;deviceSynthetic.prepareToPlay(8000,64);DeviceRealtimeRenderer deviceRenderer(deviceSynthetic);if(!deviceRenderer.prepareForTesting(8000,64))return 187;juce::AudioBuffer<float> deviceCapture(2,160);deviceCapture.clear();std::atomic<bool> stopDeviceDriver{false};std::thread deviceDriver([&]{float left[64]{},right[64]{};float* outputs[]{left,right};while(!stopDeviceDriver.load(std::memory_order_acquire)&&!deviceRenderer.hasActiveTestRequest())std::this_thread::sleep_for(std::chrono::milliseconds(1));while(!stopDeviceDriver.load(std::memory_order_acquire)){deviceRenderer.invokeTestCallback(outputs,2,64);std::this_thread::sleep_for(std::chrono::milliseconds(1));}});bool deviceCancelled=false;const bool deviceRendered=deviceRenderer.render(36,100,80,deviceCapture,{},deviceCancelled);stopDeviceDriver.store(true,std::memory_order_release);deviceDriver.join();deviceSynthetic.releaseResources();if(!deviceRendered||deviceCancelled||!deviceSynthetic.observedRealtimeProcessing()||deviceCapture.getRMSLevel(0,0,deviceCapture.getNumSamples())<=0.01f)return 188;
    PluginNoteScanSettings largeStreamingSettings;largeStreamingSettings.sampleRate=192000;largeStreamingSettings.blockSize=8192;largeStreamingSettings.velocityLevels={1,8,16,24,32,40,48,56,64,72,80,88,96,104,112,127};largeStreamingSettings.repeatCount=8;largeStreamingSettings.noteDurationSeconds=10;largeStreamingSettings.releaseSeconds=10;largeStreamingSettings.captureLengthSeconds=20;const auto largeStreamingScan=PluginNoteScanner{}.scan(synthetic,largeStreamingSettings,[]{return true;},{},[](const PluginNoteCapture&){return true;});if(!largeStreamingScan.cancelled||!largeStreamingScan.error.isEmpty())return 182;
    PatternAudioRenderer::Samples previewSamples;for(int instrument=0;instrument<2;++instrument){auto& audio=previewSamples[static_cast<size_t>(instrument)].audio;audio.setSize(1,4410);audio.clear();for(int sample=0;sample<audio.getNumSamples();++sample)audio.setSample(0,sample,0.2f);previewSamples[static_cast<size_t>(instrument)].sampleRate=44100.0;}
    MidiPattern previewPattern;previewPattern.lengthBeats=4.0;previewPattern.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{2.0,juce::MidiMessage::tempoMetaEvent(600000)}};previewPattern.events={{DrumInstrument::Kick,DrumArticulation::Default,127,0.0,0.1},{DrumInstrument::Snare,DrumArticulation::Default,80,1.0,0.1}};
    const auto previewFile=root.getChildFile("pattern-preview.wav");const auto previewResult=PatternAudioRenderer{}.render(previewPattern,previewSamples,previewFile);if(!previewResult.ok||std::abs(previewResult.durationSeconds-2.2)>0.00001)return 75;
    juce::AudioFormatManager previewFormats;previewFormats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> previewReader(previewFormats.createReaderFor(previewFile));if(!previewReader||std::abs(previewReader->lengthInSamples/previewReader->sampleRate-2.2)>0.00003)return 76;juce::AudioBuffer<float> previewAudio(2,static_cast<int>(previewReader->lengthInSamples));if(!previewReader->read(previewAudio.getArrayOfWritePointers(),2,0,static_cast<int>(previewReader->lengthInSamples))||previewAudio.getRMSLevel(0,0,previewAudio.getNumSamples())<0.01f)return 77;
    LoopingAudioSource loopingSource(previewFormats.createReaderFor(previewFile));const auto loopLength=loopingSource.getTotalLength();if(loopLength!=previewReader->lengthInSamples)return 120;loopingSource.setLooping(true);loopingSource.setNextReadPosition(loopLength-32);juce::AudioBuffer<float> loopBlock(2,64);loopingSource.getNextAudioBlock({&loopBlock,0,64});if(loopingSource.getNextReadPosition()!=32)return 121;for(int channel=0;channel<2;++channel)for(int i=0;i<64;++i){const int expectedIndex=i<32?static_cast<int>(loopLength)-32+i:i-32;if(std::abs(loopBlock.getSample(channel,i)-previewAudio.getSample(channel,expectedIndex))>0.00001f)return 122;}loopingSource.setLooping(false);loopingSource.setNextReadPosition(loopLength-8);loopBlock.clear();loopingSource.getNextAudioBlock({&loopBlock,0,16});for(int channel=0;channel<2;++channel)for(int i=8;i<16;++i)if(std::abs(loopBlock.getSample(channel,i))>0.00001f)return 123;
    int previewCancelChecks=0;const auto cancelledPreviewFile=root.getChildFile("cancelled-preview.wav");const auto cancelledPreview=PatternAudioRenderer{}.render(previewPattern,previewSamples,cancelledPreviewFile,[&]{return ++previewCancelChecks>=3;});if(cancelledPreview.ok||cancelledPreview.error!="Preview cancelled."||cancelledPreviewFile.existsAsFile())return 78;
    return 0;
}
