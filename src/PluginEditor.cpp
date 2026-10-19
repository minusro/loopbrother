#include "PluginEditor.h"

namespace jmui {

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    addMouseListener(&hideCursor, false);
}

XYPad::~XYPad() { removeMouseListener(&hideCursor); }

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
    cellPaths.clear();
    for (const auto& poly : lib.cells())
    {
        juce::Path path;
        for (size_t i = 0; i < poly.size(); ++i)
        {
            const auto s = toScreen({ (float) poly[i].x, (float) poly[i].y });
            if (i == 0) path.startNewSubPath(s);
            else        path.lineTo(s);
        }
        path.closeSubPath();
        cellPaths.push_back(std::move(path));
    }
}

void XYPad::refresh()
{
    bool changed = false;
    const juce::Point<float> u { px->getValue(), py->getValue() };
    if (u != puck)
    {
        puck = u;
        mixCount = lib.pad().mix({ u.x, u.y }, jm::kPuckRadius, mix, kMaxMix);
        changed = true;
    }
    const int n = juce::jmin(proc.uiCount(), kMaxMix);
    changed |= n != voiceCount;
    for (int i = 0; i < n; ++i)
    {
        const Voice v { proc.uiLoop(i), proc.uiGain(i), proc.uiPhase(i) };
        changed |= v.loop != voices[i].loop || std::abs(v.gain - voices[i].gain) > 1e-3f
                || std::abs(v.phase - voices[i].phase) > 1e-4;
        voices[i] = v;
    }
    voiceCount = n;
    if (changed) repaint();
}

void XYPad::paint(juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour(panel);
    g.fillRect(b);

    // cells under the circle, tinted by their share of the mix
    for (int m = 0; m < mixCount; ++m)
        if (mix[m].loop < (int) cellPaths.size())
        {
            g.setColour(accent.withAlpha(0.04f + 0.18f * mix[m].weight));
            g.fillPath(cellPaths[(size_t) mix[m].loop]);
        }
    g.setColour(dark);
    for (const auto& path : cellPaths)
        g.strokePath(path, juce::PathStrokeType(1.0f));

    // breaks: dim dots, the ones in the mix brighter
    for (int i = 0; i < lib.size(); ++i)
    {
        bool inMix = false;
        for (int m = 0; m < mixCount; ++m) inMix |= mix[m].loop == i;
        const auto c = toScreen(lib.entry(i).pos);
        const float r = inMix ? 3.0f : 2.0f;
        g.setColour(inMix ? light : dim);
        g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    }

    // the breaks being heard, each with a ring tracing its playhead
    for (int i = 0; i < voiceCount; ++i)
    {
        const auto& v = voices[i];
        if (v.loop < 0 || v.loop >= lib.size()) continue;
        const auto c = toScreen(lib.entry(v.loop).pos);
        const float rr = 9.0f, a = 0.35f + 0.65f * juce::jlimit(0.0f, 1.0f, v.gain);
        g.setColour(accent.withAlpha(a));
        g.fillEllipse(c.x - 4.5f, c.y - 4.5f, 9.0f, 9.0f);
        g.setColour(dark);
        g.drawEllipse(c.x - rr, c.y - rr, 2 * rr, 2 * rr, 2.0f);
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, 0.0f,
                          (float) (v.phase * juce::MathConstants<double>::twoPi), true);
        g.setColour(accent.withAlpha(a));
        g.strokePath(arc, juce::PathStrokeType(2.0f));
    }

    // the selection circle, drawn at the radius that does the mixing
    const auto p = toScreen(puck);
    const float pr = (float) jm::kPuckRadius * b.getWidth();
    g.setColour(ink);
    g.drawEllipse(p.x - pr, p.y - pr, 2 * pr, 2 * pr, 1.5f);
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
