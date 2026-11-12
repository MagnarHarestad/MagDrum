#pragma once

#include <JuceHeader.h>

namespace magdrum::ui
{
struct Palette
{
    inline static const juce::Colour Background{ 0xff0b0b12 };
    inline static const juce::Colour Panel{ 0xff12121c };
    inline static const juce::Colour Surface{ 0xff1a1a28 };
    inline static const juce::Colour SurfaceHover{ 0xff252538 };
    inline static const juce::Colour Border{ 0xff303044 };
    inline static const juce::Colour Text{ 0xffe8e8f4 };
    inline static const juce::Colour Muted{ 0xff8a8aa6 };
    inline static const juce::Colour Accent{ 0xfff2a83c };
    inline static const juce::Colour Green{ 0xff3ce08a };
    inline static const juce::Colour Teal{ 0xff8fc9a8 };
    inline static const juce::Colour Violet{ 0xffa98bff };
    inline static const juce::Colour Coral{ 0xfff26d5b };
    inline static const juce::Colour Selection{ 0xff30364a };

    static juce::Colour forDrumCategory(juce::String category)
    {
        category = category.toLowerCase();
        if (category.contains("kick")) return Accent;
        if (category.contains("snare") || category.contains("clap") || category.contains("rim")) return Coral;
        if (category.contains("hat")) return Green;
        if (category.contains("tom")) return Teal;
        if (category.contains("ride") || category.contains("crash") || category.contains("cymbal")) return Violet;
        if (category.contains("perc")) return juce::Colour(0xff78a8ff);
        return Muted;
    }
};

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, Palette::Background);
        setColour(juce::Label::textColourId, Palette::Text);
        setColour(juce::TextButton::buttonColourId, Palette::Surface);
        setColour(juce::TextButton::buttonOnColourId, Palette::Accent);
        setColour(juce::TextButton::textColourOffId, Palette::Text);
        setColour(juce::TextButton::textColourOnId, Palette::Background);
        setColour(juce::ComboBox::backgroundColourId, Palette::Panel);
        setColour(juce::ComboBox::textColourId, Palette::Text);
        setColour(juce::ComboBox::outlineColourId, Palette::Border);
        setColour(juce::ComboBox::arrowColourId, Palette::Muted);
        setColour(juce::TextEditor::backgroundColourId, Palette::Panel);
        setColour(juce::TextEditor::textColourId, Palette::Text);
        setColour(juce::TextEditor::highlightColourId, Palette::Selection);
        setColour(juce::TextEditor::highlightedTextColourId, Palette::Text);
        setColour(juce::TextEditor::outlineColourId, Palette::Border);
        setColour(juce::TextEditor::focusedOutlineColourId, Palette::Accent);
        setColour(juce::ListBox::backgroundColourId, Palette::Panel);
        setColour(juce::ListBox::outlineColourId, Palette::Border);
        setColour(juce::ScrollBar::thumbColourId, Palette::Muted.withAlpha(0.7f));
        setColour(juce::ScrollBar::backgroundColourId, Palette::Background);
        setColour(juce::PopupMenu::backgroundColourId, Palette::Panel);
        setColour(juce::PopupMenu::textColourId, Palette::Text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Palette::Selection);
        setColour(juce::PopupMenu::highlightedTextColourId, Palette::Text);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = backgroundColour;
        if (!button.isEnabled())
            fill = Palette::Panel;
        else if (isButtonDown)
            fill = Palette::Accent;
        else if (isMouseOverButton)
            fill = Palette::SurfaceHover;

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(isMouseOverButton || isButtonDown ? Palette::Accent.withAlpha(0.75f) : Palette::Border);
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }
};
}