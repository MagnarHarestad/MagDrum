#pragma once

#include <juce_core/juce_core.h>
#include <functional>

struct PluginPresetSearchResult
{
    juce::Array<juce::File> files;
    bool cancelled = false;
    bool rootIsDirectory = false;
};

PluginPresetSearchResult findVst3PresetFiles(const juce::File& root,
                                             const std::function<bool()>& shouldCancel = {});
