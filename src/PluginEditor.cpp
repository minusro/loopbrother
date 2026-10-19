#include "PluginEditor.h"

namespace jmui {

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
}

juce::Point<float> XYPad::toScreen(juce::Point<float> u) const
{
    return { u.x * (float) getWidth(), (1.0f - u.y) * (float) getHeight() };
}

juce::Point<float> XYPad::toUnit(juce::Point<float> s) const
{
    return { juce::jlimit(0.0f, 1.0f, s.x / (float) getWidth()),
             juce::jlimit(0.0f, 1.0f, 1.0f - s.y / (float) getHeight()) };
}

void XYPad::resized()
{
    buildCells();
    buildHighlight();
}

// Precompute which break owns each pixel and draw the cell borders once.
void XYPad::buildCells()
{
    mapSize = juce::jmax(1, juce::jmin(getWidth(), getHeight()));
    cellOf.assign((size_t) (mapSize * mapSize), 0);
    for (int y = 0; y < mapSize; ++y)
        for (int x = 0; x < mapSize; ++x)
            cellOf[(size_t) (y * mapSize + x)] =
                lib.nearest({ (x + 0.5f) / (float) mapSize, 1.0f - (y + 0.5f) / (float) mapSize });

    cells = juce::Image(juce::Image::ARGB, mapSize, mapSize, true);
    juce::Image::BitmapData bits(cells, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < mapSize; ++y)
        for (int x = 0; x < mapSize; ++x)
        {
            const int c = cellOf[(size_t) (y * mapSize + x)];
            const bool edge = (x + 1 < mapSize && cellOf[(size_t) (y * mapSize + x + 1)] != c)
                           || (y + 1 < mapSize && cellOf[(size_t) ((y + 1) * mapSize + x)] != c);
            bits.setPixelColour(x, y, edge ? dark : panel);
        }
}

void XYPad::buildHighlight()
{
    if (mapSize <= 0 || cellOf.empty()) return;
    highlight = juce::Image(juce::Image::ARGB, mapSize, mapSize, true);
    if (selected < 0) return;
    juce::Image::BitmapData bits(highlight, juce::Image::BitmapData::writeOnly);
    const auto fill = accent.withAlpha(0.10f);
    for (int y = 0; y < mapSize; ++y)
        for (int x = 0; x < mapSize; ++x)
            if (cellOf[(size_t) (y * mapSize + x)] == selected)
                bits.setPixelColour(x, y, fill);
}

void XYPad::refresh()
{
    const juce::Point<float> u { px->getValue(), py->getValue() };
    const int sel = lib.size() > 0 ? lib.nearest(u) : -1;
    const int snd = proc.uiSounding();
    const double ph = proc.uiPhase();
    const bool changed = u != puck || sel != selected || snd != sounding
                      || (snd >= 0 && std::abs(ph - phase) > 1e-4);
    puck = u;
    sounding = snd;
    phase = ph;
    if (sel != selected) { selected = sel; buildHighlight(); }
    if (changed) repaint();
}

void XYPad::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
    if (cells.isValid())     g.drawImage(cells, b);
    if (highlight.isValid()) g.drawImage(highlight, b);

    // breaks: dim dots, the selected one brighter
    for (int i = 0; i < lib.size(); ++i)
    {
        const auto c = toScreen(lib.entry(i).pos);
        const float r = i == selected ? 3.5f : 2.0f;
        g.setColour(i == selected ? light : dim);
        g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    }

    // the break being heard, with a ring tracing its playhead through the loop
    if (sounding >= 0 && sounding < lib.size())
    {
        const auto c = toScreen(lib.entry(sounding).pos);
        const float rr = 9.0f;
        g.setColour(accent);
        g.fillEllipse(c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        g.setColour(dark);
        g.drawEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr, 2.0f);
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, 0.0f,
                          (float) (phase * juce::MathConstants<double>::twoPi), true);
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(2.0f));
    }

    // the puck
    const auto p = toScreen(puck);
    g.setColour(ink);
    g.drawEllipse(p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 1.5f);
    g.fillEllipse(p.x - 1.5f, p.y - 1.5f, 3.0f, 3.0f);

    g.setColour(line);
    g.drawRect(b, 1.0f);
}

void XYPad::setFromMouse(juce::Point<float> s)
{
    const auto u = toUnit(s);
    px->setValueNotifyingHost(px->convertTo0to1(u.x));
    py->setValueNotifyingHost(py->convertTo0to1(u.y));
    refresh();
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    px->beginChangeGesture();
    py->beginChangeGesture();
    setFromMouse(e.position);
}

void XYPad::mouseDrag(const juce::MouseEvent& e) { setFromMouse(e.position); }

void XYPad::mouseUp(const juce::MouseEvent&)
{
    px->endChangeGesture();
    py->endChangeGesture();
}

// ---------------------------------------------------------------- editor
JungleEditor::JungleEditor(JungleProcessor& p) : AudioProcessorEditor(&p), pad(p)
{
    addAndMakeVisible(pad);
    setSize(512, 512);
    pad.refresh();
    startTimerHz(30);
}

JungleEditor::~JungleEditor() { stopTimer(); }

void JungleEditor::paint(juce::Graphics& g) { g.fillAll(bg); }

void JungleEditor::resized() { pad.setBounds(getLocalBounds().reduced(16)); }

} // namespace jmui
