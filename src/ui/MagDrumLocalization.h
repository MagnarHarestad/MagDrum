#pragma once

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace magdrum::ui
{
enum class Language { english, german, french, spanish, swedish, norwegian, danish, finnish };

void initialiseMagDrumLocalization();
Language currentLanguage();
void setLanguage(Language language);
juce::String languageName(Language language);
void showLanguageMenu(juce::Component& target, std::function<void()> onChanged = {});

inline juce::String localisedText(const char* sourceText)
{
    return juce::LocalisedStrings::translateWithCurrentMappings(sourceText);
}

inline juce::String localisedText(const juce::String& sourceText)
{
    return juce::LocalisedStrings::translateWithCurrentMappings(sourceText);
}
}
