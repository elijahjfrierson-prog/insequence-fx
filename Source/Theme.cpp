#include "App.h"

namespace isfx
{
ISFXLookAndFeel::ISFXLookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::TextButton::buttonColourId, panel2);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, panel2);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.8f));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
    setColour (juce::Slider::backgroundColourId, line);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, panel2);
    setColour (juce::ListBox::backgroundColourId, panel);
    setColour (juce::ScrollBar::thumbColourId, line.brighter (0.3f));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf0222632));
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, accent2.withAlpha (0.5f));
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::TextEditor::backgroundColourId, panel2);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, line);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::ProgressBar::backgroundColourId, panel2);
}

void ISFXLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto col = b.getToggleState() ? theme::accent : c;
    if (over) col = col.brighter (0.12f);
    if (down) col = col.darker (0.2f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (b.getToggleState() ? theme::accent.brighter (0.3f) : theme::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void drawLogo (juce::Graphics& g, juce::Rectangle<float> r)
{
    // mark: three rounded "frame" bars rising left to right, the last one lit
    auto m = r.removeFromLeft (r.getHeight() * 0.8f).reduced (0, r.getHeight() * 0.22f);
    const float bw = m.getWidth() * 0.2f;
    for (int i = 0; i < 3; ++i)
    {
        float h = m.getHeight() * (0.42f + 0.29f * (float) i);
        juce::Path p; p.addRoundedRectangle (m.getX() + (float) i * m.getWidth() * 0.4f, m.getBottom() - h, bw, h, bw * 0.5f);
        g.setColour (i == 2 ? theme::accent : theme::text.withAlpha (0.28f + 0.3f * (float) i));
        g.fillPath (p);
    }
    r.removeFromLeft (9);
    auto drawWord = [&] (const juce::String& w, const juce::Font& f, juce::Colour c, float x)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, w, x, r.getCentreY() + f.getAscent() * 0.5f - f.getDescent() * 0.25f);
        juce::Path p; ga.createPath (p);
        g.setColour (c); g.fillPath (p);
        return ga.getBoundingBox (0, -1, true).getRight();
    };
    juce::Font word (juce::FontOptions (14.5f, juce::Font::plain)); word.setExtraKerningFactor (0.22f);
    juce::Font fx (juce::FontOptions (11.0f, juce::Font::bold)); fx.setExtraKerningFactor (0.12f);
    float end = drawWord ("INSEQUENCE", word, theme::text, r.getX());
    drawWord ("FX", fx, theme::accent, end + 7);
}
}
