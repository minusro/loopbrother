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

XYPad::~XYPad()
{
    removeMouseListener(&hideCursor);
    proc.setPadHeld(false);
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
    g.setColour(yellow);
    g.fillRect(b);

    // cells under the circle, whitened by their share of the area
    for (int m = 0; m < mixCount; ++m)
        if (mix[m].loop < (int) cellPaths.size())
        {
            g.setColour(white.withAlpha(0.12f + 0.43f * mix[m].weight));
            g.fillPath(cellPaths[(size_t) mix[m].loop]);
        }
    const float scale = b.getWidth() / (float) JungleEditor::kDesignSize; // sizes below are at 480 px
    g.setColour(shade);
    for (const auto& path : cellPaths)
        g.strokePath(path, juce::PathStrokeType(juce::jmax(1.0f, scale)));

    // Dots on the cells under the circle only. While a cell's break plays its
    // dot follows the break's meter: half size in silence, double at the
    // loop's peak.
    for (int m = 0; m < mixCount; ++m)
    {
        const int loop = mix[m].loop;
        if (loop < 0 || loop >= lib.size()) continue;
        float r = 6.0f * scale;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].loop == loop) r = 6.0f * scale * (0.5f + 1.5f * lib.level(loop, voices[i].phase));
        const auto c = toScreen(lib.entry(loop).pos);
        g.setColour(white);
        g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    }

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
    proc.setPadHeld(true); // after the move, so an audition starts on the pressed spot
}

void XYPad::mouseDrag(const juce::MouseEvent& e) { setFromMouse(e.position); }

void XYPad::mouseUp(const juce::MouseEvent&)
{
    proc.setPadHeld(false);
    px->endChangeGesture();
    py->endChangeGesture();
}

// ---------------------------------------------------------------- editor
JungleEditor::JungleEditor(JungleProcessor& p) : AudioProcessorEditor(&p), proc(p), pad(p)
{
    // Read the saved size first: setResizeLimits resizes the still-empty
    // editor to the minimum, and resized() records every square it sees.
    const int size = juce::jlimit(kMinSize, kMaxSize, p.editorSize());
    addAndMakeVisible(pad);
    setResizable(true, false);
    setResizeLimits(kMinSize, kMinSize, kMaxSize, kMaxSize);
    if (p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        getConstrainer()->setFixedAspectRatio(1.0);
    setSize(size, size);
    pad.refresh();
    startTimerHz(30);
}

JungleEditor::~JungleEditor() { stopTimer(); }

void JungleEditor::paint(juce::Graphics& g) { g.fillAll(yellow); }

void JungleEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    pad.setBounds(0, 0, juce::jmin(w, h), juce::jmin(w, h));
    if (w != h)
    {
        // Mid host resize: square up once the host has finished applying it
        // (a resize requested from inside it would not reach the host window).
        triggerAsyncUpdate();
        return;
    }
    side = w;
    proc.setEditorSize(w);
}

void JungleEditor::handleAsyncUpdate()
{
    const int w = getWidth(), h = getHeight();
    if (w == h) return;
    // An edge drag changes one side, a corner drag both: follow the bigger change.
    const int next = std::abs(w - side) >= std::abs(h - side) ? w : h;
    const int size = juce::jlimit(kMinSize, kMaxSize, next);
    setSize(size, size);
}

} // namespace jmui
