#pragma once

#include <JuceHeader.h>
#include <BinaryData.h>

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

inline juce::Image darkBackgroundTexture()
{
    return juce::ImageCache::getFromMemory(MagDrumUiAssets::darkcharcoalplaster_png,
                                           MagDrumUiAssets::darkcharcoalplaster_pngSize);
}

inline void paintTexturedBackground(juce::Graphics& g, juce::Rectangle<int> bounds,
                                    float textureOpacity = 0.42f)
{
    g.fillAll(Palette::Background);
    const auto texture = darkBackgroundTexture();
    if (!texture.isValid())
        return;

    g.setOpacity(juce::jlimit(0.0f, 1.0f, textureOpacity));
    g.drawImage(texture, bounds.toFloat(), juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1.0f);
}

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
        setColour(juce::TextButton::textColourOnId, Palette::Text);
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
        const bool active = button.getToggleState();
        auto base = active ? Palette::Surface.interpolatedWith(backgroundColour, 0.22f) : backgroundColour;
        if (!button.isEnabled())
            base = Palette::Panel;
        else if (isButtonDown)
            base = backgroundColour.darker(0.24f);
        else if (isMouseOverButton)
            base = backgroundColour.brighter(0.12f);

        // A dark lower lip and a lit top edge give the controls a raised hardware feel.
        const float radius = button.getHeight() < 30 ? 6.0f : 8.0f;
        const auto stateColour = active ? button.findColour(juce::TextButton::buttonOnColourId) : Palette::Accent;
        const auto shadow = bounds.translated(0.0f, isButtonDown ? 0.5f : 2.5f);
        g.setColour(juce::Colours::black.withAlpha(button.isEnabled() ? 0.62f : 0.25f));
        g.fillRoundedRectangle(shadow, radius);

        const auto face = bounds.withTrimmedBottom(isButtonDown ? 0.5f : 2.0f)
                               .translated(0.0f, isButtonDown ? 1.5f : 0.0f);
        // A narrow inset bevel adds the machined rim seen on hardware controls.
        juce::ColourGradient faceGradient(base.brighter(0.2f), face.getX(), face.getY(),
                                          base.darker(0.12f), face.getX(), face.getBottom(), false);
        g.setGradientFill(faceGradient);
        g.fillRoundedRectangle(face, radius);

        const auto innerFace = face.reduced(2.0f);
        juce::ColourGradient innerGradient(base.brighter(isButtonDown ? 0.04f : 0.13f),
                                           innerFace.getX(), innerFace.getY(),
                                           base.darker(isButtonDown ? 0.16f : 0.24f),
                                           innerFace.getX(), innerFace.getBottom(), false);
        g.setGradientFill(innerGradient);
        g.fillRoundedRectangle(innerFace, juce::jmax(2.0f, radius - 2.0f));

        const auto texture = darkBackgroundTexture();
        if (texture.isValid())
        {
            juce::Path textureClip;
            textureClip.addRoundedRectangle(face, radius);
            g.saveState();
            g.reduceClipRegion(textureClip);
            g.setOpacity(button.isEnabled() ? 0.17f : 0.07f);
            g.drawImage(texture, face, juce::RectanglePlacement::stretchToFit);
            g.restoreState();
        }

        const float glowStrength = !button.isEnabled() ? 0.0f : isButtonDown ? 0.68f : active ? 0.52f : isMouseOverButton ? 0.32f : 0.12f;
        juce::Path halo;
        halo.addRoundedRectangle(face.reduced(0.8f), radius);
        juce::DropShadow(stateColour.withAlpha(glowStrength * 0.34f), isButtonDown || active ? 8 : 5, {}).
            drawForPath(g, halo);

        g.setColour(base.brighter(0.54f).withAlpha(button.isEnabled() ? (isButtonDown ? 0.34f : 0.74f) : 0.2f));
        g.drawLine(face.getX() + radius, face.getY() + 1.0f,
                   face.getRight() - radius, face.getY() + 1.0f, isButtonDown ? 0.7f : 1.15f);
        g.setColour(juce::Colours::black.withAlpha(button.isEnabled() ? 0.32f : 0.15f));
        g.drawLine(face.getX() + radius, face.getBottom() - 1.0f,
                   face.getRight() - radius, face.getBottom() - 1.0f, 1.0f);
        g.setColour((active || isMouseOverButton || isButtonDown) ? stateColour.withAlpha(0.95f)
                                                                 : base.brighter(0.20f).withAlpha(0.74f));
        g.drawRoundedRectangle(face, radius, isMouseOverButton || isButtonDown || active ? 1.3f : 0.9f);

        if (active || isButtonDown)
        {
            const float indicatorWidth = juce::jlimit(12.0f, 30.0f, bounds.getWidth() * 0.18f);
            const auto indicator = juce::Rectangle<float>(bounds.getCentreX() - indicatorWidth * 0.5f,
                                                           bounds.getBottom() - 4.0f,
                                                           indicatorWidth, 2.0f);
            juce::Path indicatorPath;
            indicatorPath.addRoundedRectangle(indicator, 1.0f);
            juce::DropShadow(stateColour.withAlpha(glowStrength), 8, {}).drawForPath(g, indicatorPath);
            g.setColour(stateColour.withAlpha(button.isEnabled() ? 0.95f : 0.3f));
            g.fillRoundedRectangle(indicator, 1.0f);
        }
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        const auto area = label.getBorderSize().subtractedFrom(label.getLocalBounds()).toFloat();
        if (area.isEmpty() || label.getText().isEmpty())
            return;

        juce::GlyphArrangement glyphs;
        glyphs.addFittedText(label.getFont(), label.getText(), area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                             label.getJustificationType(), 4);
        juce::Path textPath;
        glyphs.createPath(textPath);
        const float glowAlpha = label.isEnabled() ? (label.isMouseOverOrDragging() ? 0.20f : 0.075f) : 0.025f;
        if (!textPath.isEmpty())
            juce::DropShadow(Palette::Accent.withAlpha(glowAlpha), label.isMouseOverOrDragging() ? 6 : 3, {}).drawForPath(g, textPath);

        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.5f));
        glyphs.draw(g);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool isMouseOverButton, bool isButtonDown) override
    {
        const auto text = button.getButtonText();
        if (text.isEmpty())
            return;

        const auto font = getTextButtonFont(button, button.getHeight());
        const int yIndent = juce::jmin(4, button.proportionOfHeight(0.3f));
        const int cornerSize = juce::jmin(button.getHeight(), button.getWidth()) / 2;
        const int fontHeight = juce::roundToInt(font.getHeight() * 0.6f);
        const int leftIndent = juce::jmin(fontHeight, 2 + cornerSize / (button.isConnectedOnLeft() ? 4 : 2));
        const int rightIndent = juce::jmin(fontHeight, 2 + cornerSize / (button.isConnectedOnRight() ? 4 : 2));
        const int textWidth = button.getWidth() - leftIndent - rightIndent;
        if (textWidth <= 0)
            return;

        const auto textBounds = juce::Rectangle<float>(static_cast<float>(leftIndent), static_cast<float>(yIndent),
                                                       static_cast<float>(textWidth),
                                                       static_cast<float>(button.getHeight() - yIndent * 2));
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText(font, text, textBounds.getX(), textBounds.getY(), textBounds.getWidth(), textBounds.getHeight(),
                             juce::Justification::centred, 2);

        const bool active = button.getToggleState();
        const auto glowColour = active ? button.findColour(juce::TextButton::buttonOnColourId) : Palette::Accent;
        const float glowAlpha = !button.isEnabled() ? 0.035f : isButtonDown ? 0.44f : active ? 0.38f : isMouseOverButton ? 0.27f : 0.13f;
        juce::Path textPath;
        glyphs.createPath(textPath);
        if (!textPath.isEmpty())
            juce::DropShadow(glowColour.withAlpha(glowAlpha), active || isMouseOverButton || isButtonDown ? 7 : 4, {}).drawForPath(g, textPath);

        g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                              : juce::TextButton::textColourOffId)
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        glyphs.draw(g);
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

        const bool engaged = slider.isMouseOverOrDragging();
        juce::Path outerShape;
        outerShape.addEllipse(centreX - radius, centreY - radius, diameter, diameter);
        juce::DropShadow(Palette::Accent.withAlpha(engaged ? 0.52f : 0.19f), engaged ? 13 : 8, {}).drawForPath(g, outerShape);
        g.setColour(juce::Colours::black.withAlpha(0.82f * alpha));
        g.fillEllipse(centreX - radius, centreY - radius + 3.0f, diameter, diameter);
        juce::ColourGradient body(Palette::SurfaceHover.brighter(0.12f), centreX - radius * 0.28f, centreY - radius,
                                  Palette::Background, centreX + radius * 0.14f, centreY + radius, false);
        g.setGradientFill(body);
        g.fillEllipse(centreX - radius, centreY - radius, diameter, diameter);

        juce::Path glossClip;
        glossClip.addEllipse(centreX - radius + 2.0f, centreY - radius + 2.0f, diameter - 4.0f, diameter - 4.0f);
        g.saveState();
        g.reduceClipRegion(glossClip);
        // Concentric, low-contrast machining catches light as the control turns.
        for (int groove = 1; groove <= 4; ++groove)
        {
            const float inset = 4.0f + static_cast<float>(groove) * 2.3f;
            g.setColour(Palette::Text.withAlpha(engaged ? 0.042f : 0.024f));
            g.drawEllipse(centreX - radius + inset, centreY - radius + inset,
                          diameter - inset * 2.0f, diameter - inset * 2.0f, 0.7f);
        }
        juce::Path upperGloss;
        upperGloss.addEllipse(centreX - radius * 0.74f, centreY - radius * 0.84f, radius * 1.48f, radius * 1.05f);
        juce::ColourGradient gloss(Palette::Text.withAlpha(engaged ? 0.32f : 0.24f), centreX - radius * 0.12f, centreY - radius * 0.68f,
                                   Palette::Surface.withAlpha(0.0f), centreX + radius * 0.1f, centreY + radius * 0.22f, true);
        g.setGradientFill(gloss);
        g.fillPath(upperGloss);
        const float splitY = centreY + radius * 0.1f;
        juce::ColourGradient lowerShade(Palette::Surface.withAlpha(0.18f), centreX, splitY - 1.0f,
                                        Palette::Background.withAlpha(0.28f), centreX, centreY + radius * 0.84f, false);
        g.setGradientFill(lowerShade);
        g.fillEllipse(centreX - radius * 0.91f, splitY, radius * 1.82f, radius * 0.82f);
        g.restoreState();

        const float ringWidth = engaged ? 2.6f : 2.0f;
        juce::Path outerRing;
        outerRing.addEllipse(centreX - radius - 0.5f, centreY - radius - 0.5f,
                             diameter + 1.0f, diameter + 1.0f);
        juce::DropShadow(Palette::Accent.withAlpha(engaged ? 0.68f : 0.32f), engaged ? 12 : 7, {}).drawForPath(g, outerRing);
        g.setColour(Palette::Accent.withAlpha(engaged ? 0.98f : 0.74f));
        g.drawEllipse(centreX - radius + 1.0f, centreY - radius + 1.0f,
                      diameter - 2.0f, diameter - 2.0f, ringWidth);
        const auto glint = juce::Point<float>(centreX + radius * 0.55f, centreY - radius * 0.53f);
        juce::ColourGradient specular(Palette::Text.withAlpha(0.9f), glint.x, glint.y,
                                      Palette::Text.withAlpha(0.0f), glint.x + 4.0f, glint.y + 4.0f, true);
        g.setGradientFill(specular);
        g.fillEllipse(glint.x - 4.0f, glint.y - 4.0f, 8.0f, 8.0f);

        juce::Path arc;
        arc.addCentredArc(centreX, centreY, radius - 4.0f, radius - 4.0f,
                          0.0f, startAngle, endAngle, true);
        g.setColour(Palette::Border.withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius - 4.0f, radius - 4.0f,
                               0.0f, startAngle, angle, true);
        g.setColour(Palette::Accent.withAlpha(engaged ? 0.22f : 0.12f));
        g.strokePath(valueArc, juce::PathStrokeType(8.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
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

// Text-free, vector-drawn navigation icon: independent of the host's font and code page.
class MenuIconButton final : public juce::TextButton
{
public:
    MenuIconButton() : juce::TextButton(juce::String{}) {}

    void paintButton(juce::Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        if (auto* lf = dynamic_cast<LookAndFeel*>(&getLookAndFeel()))
            lf->drawButtonBackground(g, *this, findColour(getToggleState() ? juce::TextButton::buttonOnColourId
                                                                            : juce::TextButton::buttonColourId),
                                     isMouseOverButton, isButtonDown);
        const auto bounds = getLocalBounds().toFloat().reduced(10.0f, 9.0f);
        const float width = juce::jmin(19.0f, bounds.getWidth());
        const float height = juce::jmin(14.0f, bounds.getHeight());
        const float x = bounds.getCentreX() - width * 0.5f;
        const float y = bounds.getCentreY() - height * 0.5f;
        const auto colour = isMouseOverButton || isButtonDown ? Palette::Accent : Palette::Text;
        g.setColour(colour.withAlpha(isEnabled() ? 1.0f : 0.45f));
        for (int line = 0; line < 3; ++line)
            g.fillRoundedRectangle(x, y + static_cast<float>(line) * (height - 2.0f) * 0.5f,
                                   width, 2.0f, 1.0f);
    }
};
}
