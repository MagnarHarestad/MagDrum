#pragma once

#include "../core/SampleDatabase.h"
#include "MagDrumLocalization.h"

#include <array>
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
    class ReviewOverlay final : public juce::Component, private juce::Timer
    {
    public:
        ReviewOverlay(juce::Component& parent, std::vector<PortableMetadataConflict> pendingConflicts,
                      std::function<bool(const PortableMetadataConflict&, const PortableMetadataConflictCandidate&)> apply,
                      std::function<void(int, int, int)> completion)
            : owner(&parent), conflicts(std::move(pendingConflicts)), applyChoice(std::move(apply)),
              finished(std::move(completion))
        {
            setInterceptsMouseClicks(true, true);
            title.setText(localisedText("Resolve metadata conflict"), juce::dontSendNotification);
            title.setFont(juce::Font(20.0f, juce::Font::bold));
            title.setColour(juce::Label::textColourId, juce::Colour(0xffe8edf5));
            addAndMakeVisible(title);

            details.setMultiLine(true);
            details.setReadOnly(true);
            details.setScrollbarsShown(false);
            details.setCaretVisible(false);
            details.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff171b22));
            details.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc7cfdb));
            details.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff303846));
            addAndMakeVisible(details);
            addAndMakeVisible(choice);

            applyButton.setButtonText(localisedText("Apply selection"));
            applyButton.onClick = [this] { applyCurrent(); };
            addAndMakeVisible(applyButton);
            skipButton.setButtonText(localisedText("Skip"));
            skipButton.onClick = [this] { skipCurrent(); };
            addAndMakeVisible(skipButton);
            keepLocalButton.setButtonText(localisedText("Keep remaining local"));
            keepLocalButton.onClick = [this] { keepRemainingLocal(); };
            addAndMakeVisible(keepLocalButton);

            showNext();
            startTimerHz(30);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(juce::Colour(0xc8000000));
            auto panel = getLocalBounds().reduced(24).withSizeKeepingCentre(
                juce::jmin(620, getWidth() - 48), juce::jmin(360, getHeight() - 48));
            graphics.setColour(juce::Colour(0xff10141a));
            graphics.fillRoundedRectangle(panel.toFloat(), 12.0f);
            graphics.setColour(juce::Colour(0xff3e4a5a));
            graphics.drawRoundedRectangle(panel.toFloat(), 12.0f, 1.0f);
            graphics.setColour(juce::Colour(0xff19c8ed));
            graphics.fillRoundedRectangle(panel.removeFromLeft(4).toFloat(), 2.0f);
        }

        void resized() override
        {
            auto panel = getLocalBounds().reduced(24).withSizeKeepingCentre(
                juce::jmin(620, getWidth() - 48), juce::jmin(360, getHeight() - 48)).reduced(24, 18);
            title.setBounds(panel.removeFromTop(34));
            panel.removeFromTop(8);
            details.setBounds(panel.removeFromTop(104));
            panel.removeFromTop(12);
            choice.setBounds(panel.removeFromTop(34));
            panel.removeFromTop(16);
            auto buttons = panel.removeFromBottom(38);
            keepLocalButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 3).reduced(3, 0));
            skipButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(3, 0));
            applyButton.setBounds(buttons.reduced(3, 0));
        }

    private:
        void timerCallback() override
        {
            if (owner == nullptr)
                return;
            if (getBounds() != owner->getLocalBounds())
                setBounds(owner->getLocalBounds());
        }

        void showNext()
        {
            if (owner == nullptr)
                return;
            if (index >= conflicts.size())
            {
                finish();
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

            details.setText(message, juce::dontSendNotification);
            choice.clear(juce::dontSendNotification);
            choice.addItem(localisedText("Keep this device's value"), 1);
            for (const auto& incoming : conflict.incomingValues)
            {
                auto label = portableConflictValueLabel(conflict.type, incoming.value);
                if (incoming.deviceId.isNotEmpty())
                    label += " — " + incoming.deviceId.substring(0, 8);
                choice.addItem(label, choice.getNumItems() + 1);
            }
            choice.setTextWhenNothingSelected(localisedText("Value to use"));
            choice.setSelectedId(2, juce::dontSendNotification);
            applyButton.setEnabled(!conflict.incomingValues.empty());
            repaint();
        }

        void applyCurrent()
        {
            if (index >= conflicts.size()) return;
            const auto selectedId = choice.getSelectedId();
            const auto& conflict = conflicts[index];
            if (selectedId >= 2 && static_cast<size_t>(selectedId - 2) < conflict.incomingValues.size())
            {
                if (applyChoice && applyChoice(conflict, conflict.incomingValues[static_cast<size_t>(selectedId - 2)]))
                    ++applied;
                else
                    ++failed;
            }
            else
                ++skipped;
            ++index;
            showNext();
        }

        void skipCurrent()
        {
            if (index < conflicts.size()) ++index;
            ++skipped;
            showNext();
        }

        void keepRemainingLocal()
        {
            skipped += static_cast<int>(conflicts.size() - index);
            index = conflicts.size();
            showNext();
        }

        void finish()
        {
            if (finishing) return;
            finishing = true;
            stopTimer();
            const auto completed = finished;
            const auto counts = std::array<int, 3>{applied, failed, skipped};
            const juce::Component::SafePointer<juce::Component> safeOwner(owner);
            const juce::Component::SafePointer<ReviewOverlay> safeOverlay(this);
            juce::MessageManager::callAsync([safeOwner, safeOverlay, completed, counts]
            {
                if (safeOverlay != nullptr)
                {
                    if (safeOwner != nullptr) safeOwner->removeChildComponent(safeOverlay.getComponent());
                    delete safeOverlay.getComponent();
                }
                if (completed) completed(counts[0], counts[1], counts[2]);
            });
        }

        juce::Component::SafePointer<juce::Component> owner;
        std::vector<PortableMetadataConflict> conflicts;
        std::function<bool(const PortableMetadataConflict&, const PortableMetadataConflictCandidate&)> applyChoice;
        std::function<void(int, int, int)> finished;
        juce::Label title;
        juce::TextEditor details;
        juce::ComboBox choice;
        juce::TextButton applyButton, skipButton, keepLocalButton;
        size_t index = 0;
        int applied = 0, failed = 0, skipped = 0;
        bool finishing = false;
    };

    if (conflicts.empty() || owner == nullptr)
    {
        if (finished) finished(0, 0, static_cast<int>(conflicts.size()));
        return;
    }

    auto* overlay = new ReviewOverlay(*owner, std::move(conflicts), std::move(applyChoice), std::move(finished));
    owner->addAndMakeVisible(overlay);
    overlay->setBounds(owner->getLocalBounds());
    overlay->toFront(true);
}
}
