#include "PluginPrograms.h"

PluginProgramList discoverPluginPrograms(juce::AudioProcessor& plugin, int maximumPrograms)
{
    PluginProgramList result;
    if (maximumPrograms < 1)
        return result;

    const int programCount = plugin.getNumPrograms();
    if (programCount <= 0)
        return result;
    if (programCount > maximumPrograms)
    {
        result.exceedsLimit = true;
        return result;
    }

    result.programs.reserve(static_cast<size_t>(programCount));
    for (int index = 0; index < programCount; ++index)
    {
        auto name = plugin.getProgramName(index).trim();
        if (name.isEmpty())
            name = "Program " + juce::String(index + 1);
        result.programs.push_back({index, name});
    }
    return result;
}
