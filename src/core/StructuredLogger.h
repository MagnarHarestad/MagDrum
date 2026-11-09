#pragma once

#include <juce_core/juce_core.h>
#include <initializer_list>
#include <utility>

class StructuredLogger
{
public:
    enum class Level { info, warning, error };
    using Fields = std::initializer_list<std::pair<juce::String, juce::String>>;

    static void initialise(const juce::File& logDirectory);
    static void log(Level level, const juce::String& event, Fields fields = {});
    static juce::String formatEvent(Level level, const juce::String& event, Fields fields = {},
                                    const juce::String& timestamp = {});
    static juce::File getLogFile();
};
