#pragma once

#include "../core/SampleDatabase.h"
#include "MagDrumLocalization.h"

#include <functional>
#include <memory>
#include <vector>

namespace magdrum::ui
{
inline juce::String portableConflictValueLabel(PortableMetadataConflictType type, const juce::String& value)
{
    if (type == PortableMetadataConflictType::manualCategory)
        return value.isEmpty() ? localisedText("(empty)") : value;

    const auto profile = MidiMappingProfile::fromCsv(value.toStdString());
    if (!profile)
        return localisedText("Invalid mapping profile");

    return localisedText("Mapping with %1 notes").replace("%1", juce::String(static_cast<int>(profile->entries().size())));
}

inline void showPortableMetadataConflictReview(
    juce::Component* owner,
    std::vector<PortableMetadataConflict> conflicts,
    std::function<bool(const PortableMetadataConflict&, const PortableMetadataConflictCandidate&)> applyChoice,
    std::function<void(int, int, int)> finished)
{
    struct ReviewState : std::enable_shared_from_this<ReviewState>
    {
        juce::Component::SafePointer<juce::Component> owner;
        std::vector<PortableMetadataConflict> conflicts;
        std::function<bool(const PortableMetadataConflict&, const PortableMetadataConflictCandidate&)> applyChoice;
        std::function<void(int, int, int)> finished;
        size_t index = 0;
        int applied = 0, failed = 0, skipped = 0;

        void showNext()
        {
            if (owner == nullptr)
                return;
            if (index >= conflicts.size())
            {
                if (finished) finished(applied, failed, skipped);
                return;
            }

            const auto conflict = conflicts[index];
            juce::String message;
            if (conflict.type == PortableMetadataConflictType::manualCategory)
            {
                message = localisedText("Manual category for sample %1")
                    .replace("%1", conflict.sampleHash.substring(0, 12));
            }
            else
            {
                message = localisedText("MIDI mapping profile: %1").replace("%1", conflict.profileName);
            }
            message += "\n" + localisedText("Current local value: %1")
                .replace("%1", portableConflictValueLabel(conflict.type, conflict.localValue));
            message += "\n" + localisedText("Choose an incoming value to replace it.");

            juce::StringArray choices;
            choices.add(localisedText("Keep this device's value"));
            for (const auto& incoming : conflict.incomingValues)
            {
                auto label = portableConflictValueLabel(conflict.type, incoming.value);
                if (incoming.deviceId.isNotEmpty())
                    label += " — " + incoming.deviceId.substring(0, 8);
                choices.add(label);
            }

            auto* dialog = new juce::AlertWindow(localisedText("Resolve metadata conflict"), message,
                                                 juce::AlertWindow::WarningIcon);
            dialog->addComboBox("choice", choices, localisedText("Value to use"));
            dialog->addButton(localisedText("Apply selection"), 1, juce::KeyPress(juce::KeyPress::returnKey));
            dialog->addButton(localisedText("Skip"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
            dialog->addButton(localisedText("Keep remaining local"), 2);
            dialog->setSize(560, 220);

            auto self = shared_from_this();
            dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                [self, dialog, conflict](int result)
                {
                    const int selectedId = dialog->getComboBoxComponent("choice")->getSelectedId();
                    delete dialog;
                    if (result == 2)
                    {
                        self->skipped += static_cast<int>(self->conflicts.size() - self->index);
                        self->index = self->conflicts.size();
                        self->showNext();
                        return;
                    }

                    if (result == 1 && selectedId >= 2
                        && static_cast<size_t>(selectedId - 2) < conflict.incomingValues.size())
                    {
                        if (self->applyChoice && self->applyChoice(conflict, conflict.incomingValues[static_cast<size_t>(selectedId - 2)]))
                            ++self->applied;
                        else
                            ++self->failed;
                    }
                    else
                    {
                        ++self->skipped;
                    }
                    ++self->index;
                    self->showNext();
                }), false);
        }
    };

    if (conflicts.empty() || owner == nullptr)
    {
        if (finished) finished(0, 0, static_cast<int>(conflicts.size()));
        return;
    }

    auto state = std::make_shared<ReviewState>();
    state->owner = owner;
    state->conflicts = std::move(conflicts);
    state->applyChoice = std::move(applyChoice);
    state->finished = std::move(finished);
    state->showNext();
}
}