#include "WavScanner.h"

juce::Array<ScannedSample> WavScanner::scan(const juce::File& root, ShouldCancel shouldCancel) const
{
    juce::Array<ScannedSample> result;
    if (!root.isDirectory())
        return result;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*.wav", juce::File::findFiles))
    {
        if (shouldCancel && shouldCancel())
            break;
        const auto file = entry.getFile();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            continue;
        result.add({file, reader->lengthInSamples / reader->sampleRate,
                    static_cast<int>(reader->numChannels), reader->sampleRate});
    }
    return result;
}
