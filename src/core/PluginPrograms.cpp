#include "PluginPrograms.h"

namespace
{
bool isGenericMidiProgramName(const juce::String& rawName)
{
    const auto name = rawName.trim().toLowerCase();
    const juce::String prefix = "midi channel ";
    if (!name.startsWith(prefix)) return false;
    const auto remainder = name.substring(prefix.length());
    const auto separator = remainder.indexOf(" program ");
    if (separator <= 0) return false;
    const auto channel = remainder.substring(0, separator);
    const auto program = remainder.substring(separator + 9);
    return !channel.isEmpty() && channel.containsOnly("0123456789")
        && !program.isEmpty() && program.containsOnly("0123456789");
}
}

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
    result.onlyGenericMidiPrograms = true;
    for (int index = 0; index < programCount; ++index)
    {
        auto name = plugin.getProgramName(index).trim();
        if (name.isEmpty())
            name = "Program " + juce::String(index + 1);
        result.onlyGenericMidiPrograms = result.onlyGenericMidiPrograms && isGenericMidiProgramName(name);
        result.programs.push_back({index, name});
    }
    return result;
}
