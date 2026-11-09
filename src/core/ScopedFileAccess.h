#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace MagDrumFiles
{
bool copyURLToFile(const juce::URL& source, const juce::File& destination,
                   const std::function<bool()>& shouldCancel, juce::String& error);
bool copyFileToURL(const juce::File& source, const juce::URL& destination, juce::String& error);
}
