#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

struct PluginProgram
{
    int index = 0;
    juce::String name;
};

struct PluginProgramList
{
    std::vector<PluginProgram> programs;
    bool exceedsLimit = false;
    bool onlyGenericMidiPrograms = false;
};

PluginProgramList discoverPluginPrograms(juce::AudioProcessor& plugin, int maximumPrograms = 1024);
