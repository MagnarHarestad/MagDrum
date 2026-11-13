#pragma once

#include <JuceHeader.h>

namespace magdrum::ui
{
struct Palette
{
    inline static const juce::Colour Background{ 0xff111318 };
    inline static const juce::Colour Panel{ 0xff191b21 };
    inline static const juce::Colour Surface{ 0xff24262d };
    inline static const juce::Colour SurfaceHover{ 0xff30333b };
    inline static const juce::Colour Border{ 0xff3a3d46 };
    inline static const juce::Colour Text{ 0xffe7e9ee };
    inline static const juce::Colour Muted{ 0xff9398a4 };
    inline static const juce::Colour Accent{ 0xff26c9f4 };
    inline static const juce::Colour Magenta{ 0xffe84daf };
    inline static const juce::Colour Green{ 0xff54dbbd };
    inline static const juce::Colour Teal{ 0xff47b9c5 };
    inline static const juce::Colour Violet{ 0xff9d8bff };
    inline static const juce::Colour Coral{ 0xffff786f };
    inline static const juce::Colour Selection{ 0xff253d4a };

    static juce::Colour forDrumCategory(juce::String category)
    {
        category = category.toLowerCase();
        if (category.contains("kick")) return Magenta;
        if (category.contains("snare") || category.contains("clap") || category.contains("rim")) return Accent;
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
        const auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        auto base = backgroundColour;
        if (!button.isEnabled())
            base = Palette::Panel;
        else if (isButtonDown)
            base = backgroundColour.darker(0.24f);
        else if (isMouseOverButton)
            base = backgroundColour.brighter(0.12f);

        // A dark lower lip and a lit top edge give the controls a raised hardware feel.
        const float radius = button.getHeight() < 30 ? 6.0f : 8.0f;
        const auto shadow = bounds.translated(0.0f, isButtonDown ? 0.5f : 2.5f);
        g.setColour(juce::Colours::black.withAlpha(button.isEnabled() ? 0.62f : 0.25f));
        g.fillRoundedRectangle(shadow, radius);

        const auto face = bounds.withTrimmedBottom(isButtonDown ? 0.5f : 2.0f)
                               .translated(0.0f, isButtonDown ? 1.5f : 0.0f);
        juce::ColourGradient faceGradient(base.brighter(0.2f), face.getX(), face.getY(),
                                          base.darker(0.12f), face.getX(), face.getBottom(), false);
        g.setGradientFill(faceGradient);
        g.fillRoundedRectangle(face, radius);

        g.setColour(base.brighter(0.48f).withAlpha(button.isEnabled() ? 0.62f : 0.2f));
        g.drawLine(face.getX() + radius, face.getY() + 1.0f,
                   face.getRight() - radius, face.getY() + 1.0f, 1.0f);
        g.setColour((isMouseOverButton || isButtonDown) ? Palette::Accent.withAlpha(0.9f)
                                                        : base.brighter(0.18f).withAlpha(0.72f));
        g.drawRoundedRectangle(face, radius, isMouseOverButton || isButtonDown ? 1.35f : 1.0f);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosition, float startAngle, float endAngle,
                          juce::Slider& slider) override
    {
        const float diameter = juce::jmin(static_cast<float>(width), static_cast<float>(height)) - 8.0f;
        const float radius = diameter * 0.5f;
        const float centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const float angle = startAngle + sliderPosition * (endAngle - startAngle);
        const auto alpha = slider.isEnabled() ? 1.0f : 0.38f;

        g.setColour(juce::Colours::black.withAlpha(0.7f * alpha));
        g.fillEllipse(centreX - radius, centreY - radius + 3.0f, diameter, diameter);
        juce::ColourGradient body(Palette::SurfaceHover, centreX, centreY - radius,
                                  Palette::Background, centreX, centreY + radius, false);
        g.setGradientFill(body);
        g.fillEllipse(centreX - radius, centreY - radius, diameter, diameter);
        g.setColour(Palette::Border.withAlpha(alpha));
        g.drawEllipse(centreX - radius + 1.0f, centreY - radius + 1.0f,
                      diameter - 2.0f, diameter - 2.0f, 1.5f);

        juce::Path arc;
        arc.addCentredArc(centreX, centreY, radius - 4.0f, radius - 4.0f,
                          0.0f, startAngle, endAngle, true);
        g.setColour(Palette::Border.withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius - 4.0f, radius - 4.0f,
                               0.0f, startAngle, angle, true);
        g.setColour(Palette::Accent.withAlpha(alpha));
        g.strokePath(valueArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        const juce::Point<float> indicator(centreX + std::sin(angle) * radius * 0.52f,
                                           centreY - std::cos(angle) * radius * 0.52f);
        g.setColour(Palette::Text.withAlpha(alpha));
        g.drawLine(centreX, centreY, indicator.x, indicator.y, 2.0f);
        g.setColour(Palette::Accent.withAlpha(alpha));
        g.fillEllipse(centreX - 2.2f, centreY - 2.2f, 4.4f, 4.4f);
    }
};
}
