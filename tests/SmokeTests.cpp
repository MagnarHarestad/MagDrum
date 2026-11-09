#include "core/SampleDatabase.h"
#include "core/WavScanner.h"
#include "core/AudioClassifier.h"
#include "core/MidiMapping.h"
#include "core/MidiPatternIO.h"
#include "core/PluginNoteScanner.h"
#include "core/PluginPrograms.h"
#include "core/PatternAudioRenderer.h"
#include "core/PluginVelocityLayerAnalyzer.h"
#include <iostream>
#include <cstring>
#include <memory>
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
private:
    double sampleRate = 44100.0, phase = 0.0;
    float amplitude = 0.0f;
    int activeNote = -1, program = 0;
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

int main()
{
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MagDrumScannerTest");
    root.deleteRecursively();
    if(!root.createDirectory().wasOk())return 1;
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

    WavScanner scanner;
    WavScanner::Stats initialScanStats;
    auto scanned=scanner.scan(root,{}, {},&initialScanStats);
    if(scanned.size()!=1||scanned.getFirst().file!=waveFile)return 5;
    if(initialScanStats.filesConsidered!=1||initialScanStats.filesAnalyzed!=1||initialScanStats.errors!=0)return 44;
    if(scanned.getFirst().channels!=1||scanned.getFirst().sampleRate!=44100.0)return 6;
    if(scanned.getFirst().format!="WAV"||scanned.getFirst().bitDepth!=16||scanned.getFirst().fingerprint.isEmpty())return 12;
    if(scanned.getFirst().acousticFingerprint.length()!=128)return 56;
    if(scanned.getFirst().fundamentalHz<418.0||scanned.getFirst().fundamentalHz>462.0||scanned.getFirst().pitchConfidence<0.65)return 43;
    if(scanned.getFirst().peak<0.45||scanned.getFirst().rms<0.30||scanned.getFirst().spectralCentroidHz<300||scanned.getFirst().spectralCentroidHz>600){std::cerr<<"Feature check: peak="<<scanned.getFirst().peak<<" rms="<<scanned.getFirst().rms<<" centroid="<<scanned.getFirst().spectralCentroidHz<<"\n";return 18;}
    if(!scanner.scan(root,[]{return true;}).isEmpty())return 7;

    const auto acousticRoot=root.getSiblingFile("MagDrumAcousticFingerprintTest");acousticRoot.deleteRecursively();if(!acousticRoot.createDirectory().wasOk())return 57;
    const auto writeTone=[&](const juce::File& file,double frequency,float amplitude){auto output=file.createOutputStream();if(!output)return false;std::unique_ptr<juce::AudioFormatWriter> toneWriter(wav.createWriterFor(output.get(),44100.0,1u,16,{},0));if(!toneWriter)return false;output.release();float tone[4096]{};for(int i=0;i<4096;++i)tone[i]=amplitude*std::sin(2.0*juce::MathConstants<double>::pi*frequency*i/44100.0);const float* data[]{tone};return toneWriter->writeFromFloatArrays(data,1,4096);};
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
        "INSERT INTO plugin_sounds(kit_id,midi_note,velocity) VALUES(1,36,100);";
    const bool legacyCreated=sqlite3_exec(legacy,legacySql.toRawUTF8(),nullptr,nullptr,nullptr)==SQLITE_OK;sqlite3_close(legacy);if(!legacyCreated)return 37;
    SampleDatabase migrated;if(!migrated.open(legacyDbFile)||migrated.getAllPaths().size()!=1||!migrated.setUserCategory(waveFile,"Kick"))return 38;
    if(migrated.isSampleUnchanged(waveFile,static_cast<std::int64_t>(waveFile.getSize()),waveFile.getLastModificationTime().toMilliseconds()))return 63;
    if(sqlite3_open(legacyDbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 90;sqlite3_stmt* migratedColumns=nullptr;if(sqlite3_prepare_v2(legacy,"PRAGMA table_info(plugin_sounds)",-1,&migratedColumns,nullptr)!=SQLITE_OK)return 91;bool hasAcousticFingerprint=false;while(sqlite3_step(migratedColumns)==SQLITE_ROW){const auto* columnName=reinterpret_cast<const char*>(sqlite3_column_text(migratedColumns,1));if(columnName!=nullptr&&std::strcmp(columnName,"acoustic_fingerprint")==0)hasAcousticFingerprint=true;}sqlite3_finalize(migratedColumns);if(!hasAcousticFingerprint)return 92;
    sqlite3_stmt* migratedVersion=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT version FROM schema_info WHERE singleton=1",-1,&migratedVersion,nullptr)!=SQLITE_OK)return 93;const bool schemaUpgraded=sqlite3_step(migratedVersion)==SQLITE_ROW&&sqlite3_column_int(migratedVersion,0)==8;sqlite3_finalize(migratedVersion);sqlite3_close(legacy);if(!schemaUpgraded)return 94;
    SampleDatabase db;
    if(!db.open(dbFile))return 8;
    PluginInstrumentRecord instrument{"vst3:fixture:drums","VST3","Fixture Drums","MagDrum Test","Instrument",root.getFullPathName(),"1.0"};
    if(!db.upsertPluginInstrument(instrument)||!db.upsertPluginInstrument(instrument))return 31;
    const auto instruments=db.getPluginInstruments();
    if(instruments.size()!=1||instruments.getFirst().identifier!=instrument.identifier||instruments.getFirst().name!=instrument.name)return 32;
    juce::MemoryBlock pluginState;const std::uint8_t stateBytes[]{1,2,3};pluginState.append(stateBytes,sizeof(stateBytes));
    const auto kitId=db.savePluginKit(instrument.identifier,"Test Kit",pluginState);
    if(!kitId||db.savePluginKit(instrument.identifier,"Test Kit",pluginState)!=kitId)return 33;
    const auto restoredKitState=db.loadPluginKitState(instrument.identifier,"Test Kit");if(!restoredKitState||restoredKitState->getSize()!=sizeof(stateBytes)||std::memcmp(restoredKitState->getData(),stateBytes,sizeof(stateBytes))!=0)return 42;
    PluginSoundRecord generatedSound;generatedSound.midiNote=36;generatedSound.velocity=100;generatedSound.articulation="Kick";generatedSound.previewPath=waveFile.getFullPathName();generatedSound.duration=0.4;generatedSound.sampleRate=44100;generatedSound.channels=2;generatedSound.peak=0.8;generatedSound.rms=0.2;generatedSound.category="Kick";generatedSound.confidence=0.6;
    if(!db.upsertPluginSound(*kitId,generatedSound)||!db.upsertPluginSound(*kitId,generatedSound))return 34;
    generatedSound.midiNote=128;if(db.upsertPluginSound(*kitId,generatedSound))return 35;
    juce::String lowFingerprint,highFingerprint;for(int segment=0;segment<4;++segment)for(int band=0;band<16;++band){lowFingerprint+=band==0?"ff":"00";highFingerprint+=band==15?"ff":"00";}
    PluginSoundRecord lowVelocity;lowVelocity.midiNote=38;lowVelocity.velocity=40;lowVelocity.acousticFingerprint=lowFingerprint;PluginSoundRecord highVelocity=lowVelocity;highVelocity.velocity=88;highVelocity.acousticFingerprint=highFingerprint;
    PluginVelocityLayerAnalyzer layerAnalyzer;const auto layerCandidates=layerAnalyzer.analyze({lowVelocity,highVelocity});if(layerCandidates.size()!=1||layerCandidates.front().midiNote!=38||layerCandidates.front().lowerVelocity!=40||layerCandidates.front().upperVelocity!=88||layerCandidates.front().spectralDistance<0.99)return 85;
    lowVelocity.articulation="Test articulation";highVelocity.articulation=lowVelocity.articulation;if(!db.upsertPluginSound(*kitId,lowVelocity)||!db.upsertPluginSound(*kitId,highVelocity))return 86;
    if(!db.replacePluginVelocityLayerCandidates(*kitId,layerCandidates))return 87;const auto storedLayerCandidates=db.getPluginVelocityLayerCandidates(*kitId);if(storedLayerCandidates.size()!=1||storedLayerCandidates.front().algorithmVersion!=PluginVelocityLayerAnalyzer::algorithmVersion)return 88;const auto recalledLayerCandidates=db.getPluginVelocityLayerCandidates(instrument.identifier,"Test Kit");if(recalledLayerCandidates.size()!=1||recalledLayerCandidates.front().upperVelocity!=88)return 89;
    if(sqlite3_open(dbFile.getFullPathName().toRawUTF8(),&legacy)!=SQLITE_OK)return 95;sqlite3_stmt* storedFingerprintQuery=nullptr;if(sqlite3_prepare_v2(legacy,"SELECT count(*) FROM plugin_sounds WHERE kit_id=? AND midi_note=38 AND acoustic_fingerprint<>''",-1,&storedFingerprintQuery,nullptr)!=SQLITE_OK)return 96;sqlite3_bind_int64(storedFingerprintQuery,1,*kitId);const bool fingerprintsStored=sqlite3_step(storedFingerprintQuery)==SQLITE_ROW&&sqlite3_column_int(storedFingerprintQuery,0)==2;sqlite3_finalize(storedFingerprintQuery);sqlite3_close(legacy);if(!fingerprintsStored)return 97;
    auto invalidLayer=layerCandidates.front();invalidLayer.upperVelocity=128;if(db.replacePluginVelocityLayerCandidates(*kitId,{invalidLayer})||db.getPluginVelocityLayerCandidates(*kitId).size()!=1)return 98;
    if(!db.replacePluginVelocityLayerCandidates(*kitId,{})||!db.getPluginVelocityLayerCandidates(*kitId).empty())return 99;
    const auto& sample=scanned.getFirst();
    if(!db.insertSample(sample))return 9;
    if(!db.insertSample(sample))return 10;
    const auto browseRecords=db.getSampleBrowseRecords();if(browseRecords.size()!=1||browseRecords.getFirst().file!=sample.file||browseRecords.getFirst().durationSeconds<=0.0||browseRecords.getFirst().category.isEmpty())return 68;
    const auto pluginBrowse=db.getPluginSoundBrowseRecords();if(pluginBrowse.size()!=1||pluginBrowse.getFirst().previewFile!=sample.file||pluginBrowse.getFirst().pluginName!=instrument.name||pluginBrowse.getFirst().kitName!="Test Kit"||pluginBrowse.getFirst().midiNote!=36||pluginBrowse.getFirst().velocity!=100)return 69;
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
    ScannedSample kickLike; kickLike.durationSeconds=.5; kickLike.rms=.2; kickLike.peak=.8; kickLike.spectralCentroidHz=700; kickLike.transientStrength=.08;
    auto prediction=AudioClassifier{}.classify(kickLike);
    if(prediction.category!="Kick"||prediction.confidence<=0.0||prediction.confidence>1.0)return 13;
    ScannedSample namedHat=kickLike;namedHat.file=juce::File("808_Open_HiHat_deep.wav");
    const auto namedPrediction=AudioClassifier{}.classify(namedHat);
    if(namedPrediction.category!="Open hi-hat"||namedPrediction.confidence<0.9||!namedPrediction.tags.contains("808")||!namedPrediction.tags.contains("Electronic")||!namedPrediction.tags.contains("Deep"))return 64;
    ScannedSample ambiguousName=kickLike;ambiguousName.file=juce::File("kick_snare.wav");
    if(AudioClassifier{}.classify(ambiguousName).category!="Unknown")return 65;
    const AudioClassifier modelClassifier(std::make_shared<FixtureClassificationModel>());
    const auto modelPrediction=modelClassifier.classify(kickLike);
    if(modelPrediction.category!="Clap"||std::abs(modelPrediction.confidence-0.83)>0.0001||modelPrediction.tags.size()!=1)return 54;
    const AudioClassifier invalidModelClassifier(std::make_shared<FixtureClassificationModel>("unsupported",1.2));
    if(invalidModelClassifier.classify(kickLike).category!="Unknown")return 55;
    if(!db.setUserCategory(waveFile,"Snare"))return 14;
    if(db.getFirstSampleForCategory("Snare")!=std::optional<juce::File>(waveFile))return 74;
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
    auto gm=MidiMappingProfile::generalMidi();
    DrumEvent hat{DrumInstrument::HiHat,DrumArticulation::Closed,96,1.5,0.125};
    auto mapped=gm.toMidi(hat,false);
    if(!mapped.note||*mapped.note!=42||mapped.usedFallback)return 19;
    auto decoded=gm.fromMidi(42,96,1.5,0.125);
    if(!decoded||decoded->instrument!=DrumInstrument::HiHat||decoded->articulation!=DrumArticulation::Closed||decoded->positionBeats!=1.5)return 20;
    DrumEvent unsupported{DrumInstrument::HiHat,DrumArticulation::Edge,80,0.0,0.1};
    if(gm.toMidi(unsupported,false).note)return 21;
    auto fallback=gm.toMidi(unsupported,true);
    if(!fallback.note||!fallback.usedFallback||fallback.mappedArticulation!=DrumArticulation::Closed)return 22;
    if(!db.saveMappingProfile("GM test",gm))return 26;
    auto storedProfile=db.loadMappingProfile("GM test");
    if(!storedProfile||storedProfile->toMidi(hat,false).note!=std::optional<std::uint8_t>(42))return 27;
    auto editable=*storedProfile;
    if(!editable.setMapping({DrumInstrument::HiHat,DrumArticulation::Closed,41})||editable.toMidi(hat,false).note!=std::optional<std::uint8_t>(41))return 28;
    if(!editable.removeMapping(DrumInstrument::HiHat,DrumArticulation::Closed)||editable.toMidi(hat,false).note)return 29;
    const auto profileCsv=gm.toCsv();const auto importedProfile=MidiMappingProfile::fromCsv(profileCsv);
    if(!importedProfile||importedProfile->entries().size()!=gm.entries().size()||importedProfile->toMidi(hat,false).note!=std::optional<std::uint8_t>(42))return 42;
    if(MidiMappingProfile::fromCsv("instrument,articulation,note\nHiHat,Closed,128\n")||MidiMappingProfile::fromCsv("wrong,header,order\n"))return 43;
    MidiPattern pattern;pattern.events.push_back(hat);pattern.tempoAndSignature.push_back({0.0,juce::MidiMessage::tempoMetaEvent(600000)});pattern.tempoAndSignature.push_back({0.0,juce::MidiMessage::timeSignatureMetaEvent(7,8)});pattern.lengthBeats=8.0;
    MidiPatternIO midiIO;const auto midiFile=root.getChildFile("mapping.mid");
    if(!midiIO.write(midiFile,pattern,gm,false))return 23;
    MidiPattern decodedPattern;auto midiReport=midiIO.read(midiFile,gm,decodedPattern);
    if(!midiReport.ok||decodedPattern.events.size()!=1||decodedPattern.tempoAndSignature.size()!=2||std::abs(decodedPattern.lengthBeats-8.0)>0.001)return 24;
    if(decodedPattern.events[0].velocity!=96||std::abs(decodedPattern.events[0].positionBeats-1.5)>0.001||std::abs(decodedPattern.events[0].durationBeats-.125)>0.002)return 25;
    MidiPattern emptyBars;emptyBars.lengthBeats=12.0;const auto emptyBarsFile=root.getChildFile("empty-bars.mid");if(!midiIO.write(emptyBarsFile,emptyBars,gm,false))return 72;MidiPattern decodedEmptyBars;if(!midiIO.read(emptyBarsFile,gm,decodedEmptyBars).ok||!decodedEmptyBars.events.empty()||std::abs(decodedEmptyBars.lengthBeats-12.0)>0.001)return 73;
    MidiPattern metadataEdit;metadataEdit.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{0.0,juce::MidiMessage::timeSignatureMetaEvent(4,4)},{8.0,juce::MidiMessage::tempoMetaEvent(750000)},{8.0,juce::MidiMessage::timeSignatureMetaEvent(3,4)}};
    if(!setPatternTempo(metadataEdit,100)||!setPatternTimeSignature(metadataEdit,5,8)||setPatternTempo(metadataEdit,10)||setPatternTimeSignature(metadataEdit,3,3)||metadataEdit.tempoAndSignature.size()!=4)return 64;
    bool startTempo=false,startSignature=false,laterTempo=false,laterSignature=false;for(const auto& meta:metadataEdit.tempoAndSignature){if(meta.positionBeats==0.0&&meta.message.isTempoMetaEvent())startTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.6)<0.000001;if(meta.positionBeats==0.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);startSignature=n==5&&d==8;}if(meta.positionBeats==8.0&&meta.message.isTempoMetaEvent())laterTempo=std::abs(meta.message.getTempoSecondsPerQuarterNote()-0.75)<0.000001;if(meta.positionBeats==8.0&&meta.message.isTimeSignatureMetaEvent()){int n=0,d=0;meta.message.getTimeSignatureInfo(n,d);laterSignature=n==3&&d==4;}}if(!startTempo||!startSignature||!laterTempo||!laterSignature)return 65;
    const auto metadataFile=root.getChildFile("metadata.mid");if(!midiIO.write(metadataFile,metadataEdit,gm,false))return 66;MidiPattern metadataRoundTrip;if(!midiIO.read(metadataFile,gm,metadataRoundTrip).ok||metadataRoundTrip.tempoAndSignature.size()!=4)return 67;
    SyntheticInstrument synthetic;synthetic.setCurrentProgram(1);const auto programs=discoverPluginPrograms(synthetic);if(programs.programs.size()!=2||programs.programs[0].index!=0||programs.programs[0].name!="Factory Kit A"||programs.programs[1].name!="Program 2"||synthetic.getCurrentProgram()!=1)return 70;const auto limitedPrograms=discoverPluginPrograms(synthetic,1);if(!limitedPrograms.exceedsLimit||!limitedPrograms.programs.empty())return 71;PluginNoteScanSettings scanSettings;scanSettings.sampleRate=8000;scanSettings.blockSize=64;scanSettings.velocityLevels={64,127};scanSettings.noteDurationSeconds=.01;scanSettings.releaseSeconds=.01;
    int progress=0;auto noteScan=PluginNoteScanner{}.scan(synthetic,scanSettings,{},[&](int done,int total,int,int){if(done<1||done>total)progress=-1000;else ++progress;});
    if(!noteScan.error.isEmpty()||noteScan.notes.size()!=256||progress!=256||synthetic.getCurrentProgram()!=1)return 39;
    int silentNotes=0,activeNotes=0;for(const auto&capture:noteScan.notes){if(capture.midiNote==60){if(capture.silent)silentNotes++;}else if(capture.velocity==127&&!capture.silent&&capture.peak>0.1)activeNotes++;}
    if(silentNotes!=2||activeNotes!=127)return 40;
    auto cancelledScan=PluginNoteScanner{}.scan(synthetic,scanSettings,[]{return true;});if(!cancelledScan.cancelled||!cancelledScan.notes.empty()||synthetic.getCurrentProgram()!=1)return 41;
    PatternAudioRenderer::Samples previewSamples;for(int instrument=0;instrument<2;++instrument){auto& audio=previewSamples[static_cast<size_t>(instrument)].audio;audio.setSize(1,4410);audio.clear();for(int sample=0;sample<audio.getNumSamples();++sample)audio.setSample(0,sample,0.2f);previewSamples[static_cast<size_t>(instrument)].sampleRate=44100.0;}
    MidiPattern previewPattern;previewPattern.lengthBeats=4.0;previewPattern.tempoAndSignature={{0.0,juce::MidiMessage::tempoMetaEvent(500000)},{2.0,juce::MidiMessage::tempoMetaEvent(600000)}};previewPattern.events={{DrumInstrument::Kick,DrumArticulation::Default,127,0.0,0.1},{DrumInstrument::Snare,DrumArticulation::Default,80,1.0,0.1}};
    const auto previewFile=root.getChildFile("pattern-preview.wav");const auto previewResult=PatternAudioRenderer{}.render(previewPattern,previewSamples,previewFile);if(!previewResult.ok||std::abs(previewResult.durationSeconds-2.2)>0.00001)return 75;
    juce::AudioFormatManager previewFormats;previewFormats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> previewReader(previewFormats.createReaderFor(previewFile));if(!previewReader||std::abs(previewReader->lengthInSamples/previewReader->sampleRate-2.2)>0.00003)return 76;juce::AudioBuffer<float> previewAudio(2,static_cast<int>(previewReader->lengthInSamples));if(!previewReader->read(previewAudio.getArrayOfWritePointers(),2,0,static_cast<int>(previewReader->lengthInSamples))||previewAudio.getRMSLevel(0,0,previewAudio.getNumSamples())<0.01f)return 77;
    int previewCancelChecks=0;const auto cancelledPreviewFile=root.getChildFile("cancelled-preview.wav");const auto cancelledPreview=PatternAudioRenderer{}.render(previewPattern,previewSamples,cancelledPreviewFile,[&]{return ++previewCancelChecks>=3;});if(cancelledPreview.ok||cancelledPreview.error!="Preview cancelled."||cancelledPreviewFile.existsAsFile())return 78;
    return 0;
}
