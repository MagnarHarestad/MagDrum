#pragma once

#include <juce_core/juce_core.h>
#include <optional>

struct PluginSourceKitRequest
{
    juce::String pluginIdentifier;
    juce::String kitName;

    bool isValid() const
    {
        return pluginIdentifier.isNotEmpty()
            && pluginIdentifier.length() <= 4096
            && kitName.length() <= 4096;
    }

    juce::var toVar() const
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("pluginIdentifier", pluginIdentifier);
        object->setProperty("kitName", kitName);
        return object;
    }

    static std::optional<PluginSourceKitRequest> fromVar(const juce::var& value)
    {
        if (! value.isObject())
            return std::nullopt;

        PluginSourceKitRequest request;
        request.pluginIdentifier = value.getProperty("pluginIdentifier", {}).toString();
        request.kitName = value.getProperty("kitName", {}).toString();
        if (! request.isValid())
            return std::nullopt;

        return request;
    }
};
