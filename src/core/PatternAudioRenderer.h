#pragma once

#include "MidiPatternIO.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <functional>

struct PatternAudioSample
{
    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
};

struct PatternAudioRenderResult
{
    bool ok = false;
    double durationSeconds = 0.0;
    juce::String error;
};

class PatternAudioRenderer
{
public:
    using ShouldCancel = std::function<bool()>;
    using Samples = std::array<PatternAudioSample, 9>;

    static bool loadSample(juce::AudioFormatManager&, const juce::File&,
                           PatternAudioSample&, juce::String& error);
    PatternAudioRenderResult render(const MidiPattern&, const Samples&, const juce::File& output,
                                    ShouldCancel shouldCancel = {}) const;
};
