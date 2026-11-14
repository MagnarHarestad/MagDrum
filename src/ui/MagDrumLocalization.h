#pragma once

#include <juce_core/juce_core.h>

namespace magdrum::ui
{
void initialiseMagDrumLocalization();

inline juce::String localisedText(const char* sourceText)
{
    return juce::LocalisedStrings::translateWithCurrentMappings(sourceText);
}
}
