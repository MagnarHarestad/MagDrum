#include "core/SampleDatabase.h"
#include "core/WavScanner.h"
#include "core/AudioClassifier.h"

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
    float samples[128]{};
    const float* channels[]{samples};
    if(!writer->writeFromFloatArrays(channels,1,128))return 4;
    writer.reset();

    WavScanner scanner;
    auto scanned=scanner.scan(root);
    if(scanned.size()!=1||scanned.getFirst().file!=waveFile)return 5;
    if(scanned.getFirst().channels!=1||scanned.getFirst().sampleRate!=44100.0)return 6;
    if(scanned.getFirst().format!="WAV"||scanned.getFirst().bitDepth!=16||scanned.getFirst().fingerprint.isEmpty())return 12;
    if(!scanner.scan(root,[]{return true;}).isEmpty())return 7;

    const auto dbFile=root.getChildFile("index.sqlite3");
    SampleDatabase db;
    if(!db.open(dbFile))return 8;
    const auto& sample=scanned.getFirst();
    if(!db.insertSample(sample.file,sample.durationSeconds,sample.channels,sample.sampleRate))return 9;
    if(!db.insertSample(sample.file,sample.durationSeconds,sample.channels,sample.sampleRate))return 10;
    auto paths=db.getAllPaths();
    if(paths.size()!=1||paths.getFirst()!=waveFile)return 11;
    ScannedSample kickLike; kickLike.durationSeconds=.5; kickLike.rms=.2; kickLike.peak=.8; kickLike.spectralCentroidHz=700; kickLike.transientStrength=.08;
    auto prediction=AudioClassifier{}.classify(kickLike);
    if(prediction.category!="Kick"||prediction.confidence<=0.0||prediction.confidence>1.0)return 13;
    if(!db.setUserCategory(waveFile,"Snare"))return 14;
    return 0;
}
