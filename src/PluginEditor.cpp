#include "PluginEditor.h"
#include "Assets.h"

namespace jmui {

// ---------------------------------------------------------------- pad
XYPad::XYPad(JungleProcessor& p) : proc(p), lib(p.library())
{
    px = p.apvts.getParameter("x");
    py = p.apvts.getParameter("y");
    psize = p.apvts.getParameter("size");

    // Which cells meet at each corner of each cell: the ones whose breaks
    // are nearest to it (a Voronoi corner is equally far from all of them).
    for (const auto& poly : lib.cells())
    {
        std::vector<std::vector<int>> owners;
        for (const auto& v : poly)
        {
            double nearest = std::numeric_limits<double>::max();
            std::vector<double> dist;
            for (int i = 0; i < lib.size(); ++i)
            {
                dist.push_back(std::hypot(v.x - lib.entry(i).pos.x, v.y - lib.entry(i).pos.y));
                nearest = std::min(nearest, dist.back());
            }
            std::vector<int> meet;
            for (int i = 0; i < lib.size(); ++i)
                if (dist[(size_t) i] < nearest + 1e-5) meet.push_back(i);
            owners.push_back(std::move(meet));
        }
        corners.push_back(std::move(owners));
    }
}

XYPad::~XYPad()
{
    stopTimer();
    proc.setPadHeld(false);
    proc.setFineDrag(false);
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
    const juce::Point<float> u { px->convertFrom0to1(px->getValue()), py->convertFrom0to1(py->getValue()) };
    const float s = psize->convertFrom0to1(psize->getValue());
    if (u != puck || ! juce::exactlyEqual(s, size))
    {
        puck = u;
        size = s;
        mixCount = lib.pad().mix({ u.x, u.y }, jm::circleRadius(s), mix, kMaxMix);
        changed = true;
    }
    const int n = juce::jmin(proc.uiCount(), kMaxMix);
    changed |= n != voiceCount || proc.muteHeld() != muted;
    muted = proc.muteHeld();
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
    const float scale = b.getWidth() / (float) JungleEditor::kDesignSize; // sizes below are at 480 px
    g.fillAll(cream);
    sprites.configure(kRingRadius * scale, g.getInternalContext().getPhysicalPixelScaleFactor(), *style);

    // Where everything is drawn: the layout, moved by the ripples. A pushed
    // cell's O moves with it; a border corner moves by the average of
    // the cells that meet there, so the borders bend but never come apart.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const bool rippling = ! drops.empty();
    std::vector<juce::Point<float>> shift((size_t) lib.size());
    if (rippling)
        for (int i = 0; i < lib.size(); ++i) shift[(size_t) i] = cellShift(i, now);
    std::vector<juce::Path> rippled;
    if (rippling)
        for (size_t c = 0; c < lib.cells().size(); ++c)
        {
            const auto& poly = lib.cells()[c];
            juce::Path path;
            for (size_t i = 0; i < poly.size(); ++i)
            {
                juce::Point<float> d;
                for (const int m : corners[c][i]) d += shift[(size_t) m];
                d /= (float) juce::jmax<size_t>(1, corners[c][i].size());
                const auto s = toScreen(juce::Point<float> { (float) poly[i].x, (float) poly[i].y } + d);
                if (i == 0) path.startNewSubPath(s);
                else        path.lineTo(s);
            }
            path.closeSubPath();
            rippled.push_back(std::move(path));
        }
    const auto& cells = rippling ? rippled : cellPaths;

    // cells under the circle, washed in their break's colour by their share
    std::vector<char> covered((size_t) lib.size(), 0);
    for (int m = 0; m < mixCount; ++m)
        if (mix[m].loop >= 0 && mix[m].loop < (int) cells.size())
        {
            covered[(size_t) mix[m].loop] = 1;
            g.setColour(frootLoop(lib.entry(mix[m].loop).colour).withAlpha(0.10f + 0.35f * mix[m].weight));
            g.fillPath(cells[(size_t) mix[m].loop]);
        }
    // A small O on every cell the circle doesn't cover. Pushed by a ripple,
    // it squashes along the push, up to kBob, like a ring tipping as it bobs
    // in milk, and swings across and along it a few times as it settles.
    for (int i = 0; i < lib.size(); ++i)
        if (! covered[(size_t) i])
        {
            float axis = 0.0f, squeeze = 1.0f;
            if (rippling) squeeze = cellBob(i, now, axis);
            sprites.drawO(g, toScreen(lib.entry(i).pos + shift[(size_t) i]), kSmallO * scale, lib.entry(i).colour, i,
                          -axis, squeeze); // on screen, y down
        }

    // ...and a full-size O on each one it does, its size following the
    // break's meter while it plays: half in silence, double at the peak.
    struct Mark { juce::Point<float> c; float r; int loop; };
    std::vector<Mark> marks;
    for (int m = 0; m < mixCount; ++m)
    {
        const int loop = mix[m].loop;
        if (loop < 0 || loop >= lib.size()) continue;
        float r = kRingRadius * scale;
        if (! muted) // silenced, they rest at their own size
            for (int i = 0; i < voiceCount; ++i)
                if (voices[i].loop == loop) r = kRingRadius * scale * (0.5f + 1.5f * lib.level(loop, voices[i].phase));
        marks.push_back({ toScreen(lib.entry(loop).pos), r, loop }); // the circle's own marks hold still
    }
    for (const auto& k : marks) // dark grey while muted
        sprites.drawO(g, k.c, k.r, muted ? kMutedColour : lib.entry(k.loop).colour, k.loop);
}

// ---------------------------------------------------------------- ripples
// Each step of a drag pushes the cells the circle's centre passed through
// since the last step the way the circle went, and the cells beside that
// stretch outwards, away from it, as a wake. Each step of a resize (Shift)
// pushes the cells outside the circle round its edge straight away from
// its centre as far as the edge went out, or towards it as far as it came
// in. A pushed cell bobs as if in milk: out with the push, back past where
// it was, out again, kRippleSwings half-swings of kRippleSwell seconds,
// dying away over kRippleDecay, and its small O squashes along the push and
// across it in step (cellBob). While the circle keeps crossing a cell the
// pushes add up and it leans forward; once passed, it swings to rest. Pad
// units: kRipplePush is how far a crossed cell moves per unit the circle
// moved, kRippleMax the most any cell moves; cells beside the path or round
// the edge get kSidePush of that, falling off with their distance from the
// stretch (across it and past its ends) or from the edge (kSideWidth), none
// beyond kSideReach.
constexpr float kRippleSwell = 0.22f, kRipplePush = 0.14f, kRippleMax = 0.014f;
constexpr float kSidePush = 1.5f, kSideWidth = 0.1f, kSideReach = 0.2f;
constexpr int   kRippleSwings = 4;
constexpr float kRippleDecay = 0.45f;
constexpr float kRippleLife = kRippleSwings * kRippleSwell;

// Where a push has its cell t seconds after it, as a share of the push: a
// damped sine, 0 at the start and at the end of each half-swing.
static float rippleSwing(float t)
{
    if (t <= 0.0f || t >= kRippleLife) return 0.0f;
    return std::sin(juce::MathConstants<float>::pi * t / kRippleSwell) * std::exp(-t / kRippleDecay);
}

int XYPad::cellAt(juce::Point<float> u) const
{
    int best = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (int i = 0; i < lib.size(); ++i)
    {
        const float d = u.getDistanceSquaredFrom(lib.entry(i).pos);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

void XYPad::addDrop(juce::Point<float> from, juce::Point<float> to)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto step = to - from;
    const float length = step.getDistanceFromOrigin();
    Drop drop { now, {} };
    auto pushed = [&drop](int c) { for (const auto& p : drop.pushes) if (p.first == c) return true; return false; };
    // The cells along the way, sampled finely enough to catch a corner.
    const int samples = juce::jmax(1, (int) std::ceil(length / 0.004f));
    for (int i = 0; i <= samples; ++i)
    {
        const int c = cellAt(from + step * ((float) i / (float) samples));
        if (! pushed(c)) drop.pushes.push_back({ c, step });
    }
    // The cells beside it, outwards: the further from the path, the less.
    if (length > 0.0f)
    {
        const auto along = step / length;
        const juce::Point<float> across { -along.y, along.x };
        for (int i = 0; i < lib.size(); ++i)
        {
            if (pushed(i)) continue;
            const auto rel = lib.entry(i).pos - from;
            const float on = rel.getDotProduct(along), off = rel.getDotProduct(across);
            const float past = on < 0.0f ? -on : std::max(0.0f, on - length); // beyond the stretch's ends
            if (std::hypot(off, past) > kSideReach) continue;
            const float fall = std::exp(-(off * off + past * past) / (kSideWidth * kSideWidth));
            drop.pushes.push_back({ i, across * ((off >= 0.0f ? 1.0f : -1.0f) * length * kSidePush * fall) });
        }
    }
    ripple(std::move(drop), now);
    lastDropAt = to;
}

void XYPad::addSizeDrop(float from, float to)
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    Drop drop { now, {} };
    // The cells outside the circle round its edge, straight away from its
    // centre (or towards it): the further from the edge, the less.
    for (int i = 0; i < lib.size(); ++i)
    {
        const auto rel = lib.entry(i).pos - puck;
        const float r = rel.getDistanceFromOrigin(), off = r - to; // how far outside the edge
        if (off < 0.0f || off > kSideReach) continue;
        const float fall = std::exp(-(off * off) / (kSideWidth * kSideWidth));
        drop.pushes.push_back({ i, rel * ((to - from) * kSidePush * fall / r) });
    }
    ripple(std::move(drop), now);
    lastDropRadius = to;
}

void XYPad::ripple(Drop drop, double now)
{
    drops.push_back(std::move(drop));
    if (drops.size() > 64) drops.erase(drops.begin());
    lastDrop = now;
    if (! isTimerRunning()) startTimerHz(60);
}

juce::Point<float> XYPad::cellShift(int cell, double now) const
{
    juce::Point<float> d;
    for (const auto& drop : drops)
    {
        const float swing = rippleSwing((float) (now - drop.born));
        if (juce::exactlyEqual(swing, 0.0f)) continue;
        for (const auto& [c, push] : drop.pushes)
            if (c == cell) d += push * (kRipplePush * swing);
    }
    const float m = d.getDistanceFromOrigin(); // a fast flick moves cells further, but only so far
    return m > 0.0f ? d * (kRippleMax * std::tanh(m / kRippleMax) / m) : d;
}

float XYPad::cellBob(int cell, double now, float& axis) const
{
    // Each push's squash as a vector at twice its axis's angle, so pushes
    // either way along one axis add up, and a swing across it (negative)
    // turns the axis a quarter.
    juce::Point<float> q;
    for (const auto& drop : drops)
    {
        const float swing = rippleSwing((float) (now - drop.born));
        if (juce::exactlyEqual(swing, 0.0f)) continue;
        for (const auto& [c, push] : drop.pushes)
            if (c == cell)
            {
                const float m = push.getDistanceFromOrigin() * kRipplePush * swing;
                const float twice = 2.0f * std::atan2(push.y, push.x);
                q += { m * std::cos(twice), m * std::sin(twice) };
            }
    }
    const float m = q.getDistanceFromOrigin();
    axis = m > 0.0f ? 0.5f * std::atan2(q.y, q.x) : 0.0f;
    return 1.0f - kBob * std::tanh(m / kBobAt);
}

void XYPad::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    drops.erase(std::remove_if(drops.begin(), drops.end(), [&](const Drop& d) { return now - d.born > kRippleLife; }),
                drops.end());
    if (drops.empty()) stopTimer();
    repaint();
}

void XYPad::setPuck(juce::Point<float> u)
{
    px->setValueNotifyingHost(px->convertTo0to1(u.x));
    py->setValueNotifyingHost(py->convertTo0to1(u.y));
    refresh();
}

void XYPad::setCircleSize(float s)
{
    psize->setValueNotifyingHost(psize->convertTo0to1(juce::jlimit(jm::kCircleSizeMin, jm::kCircleSizeMax, s)));
    refresh();
}

void XYPad::mouseDown(const juce::MouseEvent& e)
{
    for (auto* p : { px, py, psize }) p->beginChangeGesture();
    isDragging = true;
    last = e.position;
    e.source.enableUnboundedMouseMovement(true);
    proc.setFineDrag(e.mods.isAltDown());
    if (! e.mods.isAltDown()) setPuck(toUnit(e.position));
    proc.setPadHeld(true); // after the move, so an audition starts on the pressed spot
    lastDropAt = puck;
    lastDropRadius = (float) jm::circleRadius(size);
}

void XYPad::mouseDrag(const juce::MouseEvent& e)
{
    const bool fine = e.mods.isAltDown();
    proc.setFineDrag(fine); // before the change, so it applies at once
    const float speed = fine ? kFineSpeed : 1.0f;
    if (e.mods.isShiftDown())
    {
        // Like a knob: right or up grows the circle, left or down shrinks
        // it, its radius by as many pixels as the mouse moved.
        const auto d = e.position - last;
        setCircleSize(size + (d.x - d.y) * speed / (float) (jm::kPuckRadius * getWidth()));
    }
    else
    {
        setPuck(toUnit(toScreen(puck) + (e.position - last) * speed));
    }
    last = e.position;

    // A push where the circle is now and then, as far as it went since, or
    // as far as its edge went out or in.
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const auto moved = puck - lastDropAt;
    const float radius = (float) jm::circleRadius(size);
    if (now - lastDrop > 0.06 && moved.getDistanceFromOrigin() * (float) getWidth() > 1.0f) addDrop(lastDropAt, puck);
    else if (now - lastDrop > 0.06 && std::abs(radius - lastDropRadius) * (float) getWidth() > 1.0f)
        addSizeDrop(lastDropRadius, radius);
}

void XYPad::mouseUp(const juce::MouseEvent& e)
{
    proc.setPadHeld(false);
    proc.setFineDrag(false);
    isDragging = false;
    for (auto* p : { px, py, psize }) p->endChangeGesture();
    // Show the pointer again on the circle's centre. The unbounded drag ends
    // first: ending it afterwards, JUCE would put the pointer back near
    // where it left the pad.
    auto source = e.source; // a handle: the copy moves the same pointer
    if (source.isUnboundedMouseMovementEnabled())
    {
        source.enableUnboundedMouseMovement(false);
        source.setScreenPosition(localPointToGlobal(toScreen(puck)));
    }
}

// ---------------------------------------------------------------- window
void WindowConstrainer::checkBounds(juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                                    const juce::Rectangle<int>& limits,
                                    bool top, bool left, bool bottom, bool right)
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - lastCheck > kDragGapMs) corner = false; // a new drag
    lastCheck = now;
    if ((top || bottom) && (left || right))
    {
        corner = true;
        cTop = top, cLeft = left, cBottom = bottom, cRight = right;
    }
    else if (corner && (top || bottom || left || right))
        top = cTop, left = cLeft, bottom = cBottom, right = cRight;
    // JUCE's fixed-ratio rules then do the rest: an edge sets the size, a
    // corner takes the larger of the two sides (so the pointer stays on the
    // window's edge) and both place the window.
    ComponentBoundsConstrainer::checkBounds(bounds, previous, limits, top, left, bottom, right);
}

// ---------------------------------------------------------------- editor
JungleEditor::JungleEditor(JungleProcessor& p) : AudioProcessorEditor(&p), proc(p), pad(p)
{
    // Read the saved size before anything resizes the editor: resized()
    // records every width it sees.
    const int width = juce::jlimit(kMinSize, kMaxSize, p.editorSize());
    addAndMakeVisible(pad);
    window.setSizeLimits(kMinSize, heightFor(kMinSize), kMaxSize, heightFor(kMaxSize));
    setConstrainer(&window);
    setResizable(true, false);
    setSize(width, heightFor(width));
    logoFace = juce::Typeface::createSystemTypefaceFor(Assets::CherryBombOneRegular_ttf,
                                                       (size_t) Assets::CherryBombOneRegular_ttfSize);
    pad.refresh();
    catImage = juce::ImageCache::getFromMemory(Assets::loopscat_png, Assets::loopscat_pngSize);
    cat = catTarget(); // already where it belongs when the window opens
    catX = juce::jmap(juce::Random::getSystemRandom().nextFloat(), kCatLeft, kCatRight);
    catTick = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz(60); // the meters' snap needs it
}

juce::Rectangle<float> JungleEditor::catBounds() const
{
    const float scale = (float) getWidth() / kDesignSize, bottom = (float) getHeight();
    const float h = kCatHeight * scale, cw = h * (float) catImage.getWidth() / (float) juce::jmax(1, catImage.getHeight());
    return { catX * (float) getWidth() - 0.5f * cw, bottom - cat * kCatShown * h, cw, h };
}

float JungleEditor::catSwayAt(double beat)
{
    // Each side lingers kCatHold of a beat round its beat; the swing over to
    // the next eases in and out (half a cosine) through the rest, so it
    // never stops sharp. With no hold it's a plain pendulum, cos(pi * beat).
    const double s = beat + 0.5 * kCatHold, whole = std::floor(s);
    const float side = ((long long) whole & 1) == 0 ? 1.0f : -1.0f; // the side it last reached
    const float x = juce::jmax(0.0f, ((float) (s - whole) - kCatHold) / (1.0f - kCatHold));
    return side * std::cos(juce::MathConstants<float>::pi * x);
}

juce::AffineTransform JungleEditor::catTurned(float angle) const
{
    return juce::AffineTransform::rotation(angle, catX * (float) getWidth(), (float) getHeight());
}

float JungleEditor::catTarget() const
{
    const float t = juce::jlimit(0.0f, 1.0f, (pad.circleSize() / jm::kCircleSizeMax - kCatFrom) / (1.0f - kCatFrom));
    return t * t * (3.0f - 2.0f * t);
}

void JungleEditor::timerCallback()
{
    pad.refresh();
    updateMute();

    // The cat eases towards how far up the circle's size puts it.
    const double tick = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float catTo = catTarget();
    const double dt = tick - catTick;
    if (! juce::exactlyEqual(cat, catTo))
    {
        if (cat <= 0.0f) // coming up: somewhere new
            catX = juce::jmap(juce::Random::getSystemRandom().nextFloat(), kCatLeft, kCatRight);
        const float next = cat + (catTo - cat) * (float) (1.0 - std::exp(-dt / kCatEase));
        cat = std::abs(next - catTo) < 0.002f ? catTo : next;
        repaint();
    }
    catTick = tick;
    // ...and sways on the beat while the loops play.
    catSwaying += ((proc.uiPlaying() ? 1.0f : 0.0f) - catSwaying) * (float) (1.0 - std::exp(-dt / kCatEase));
    if (catSwaying < 0.002f) catSwaying = proc.uiPlaying() ? catSwaying : 0.0f;
    const float angle = catSwaying > 0.0f ? kCatSway * catSwaying * catSwayAt(proc.uiBeatAt(tick * 1000.0)) : 0.0f;
    if (std::abs(angle - catAngle) > 1e-4f || (juce::exactlyEqual(angle, 0.0f) && ! juce::exactlyEqual(catAngle, 0.0f)))
    {
        if (cat > 0.0f) // tiny steps wait, but it always comes to rest
            repaint(catBounds().transformedBy(catTurned(catAngle))
                        .getUnion(catBounds().transformedBy(catTurned(angle)))
                        .getSmallestIntegerContainer().expanded(2));
        catAngle = angle;
    }
}

JungleEditor::~JungleEditor()
{
    stopTimer();
    proc.setMuteHeld(false);
}

void JungleEditor::paint(juce::Graphics& g) { g.fillAll(cream); }

void JungleEditor::paintOverChildren(juce::Graphics& g)
{
    const float scale = (float) getWidth() / kDesignSize, w = (float) getWidth();
    auto bar = statusBar().toFloat();

    // The bowl's front: its lip arcs up over the pad's lower corners. Biscuit,
    // a shade under the pad so the logo's white edge shows, and very slightly
    // deeper towards its sides and foot, where the round bowl turns away from
    // the light.
    juce::Path lip, front;
    for (int i = 0; i <= 96; ++i)
    {
        const float x = w * (float) i / 96.0f;
        const juce::Point<float> p { x, bar.getY() - bowlLip(x, w, scale) };
        if (i == 0) lip.startNewSubPath(p);
        else        lip.lineTo(p);
    }
    front = lip;
    front.lineTo(w, bar.getBottom());
    front.lineTo(0.0f, bar.getBottom());
    front.closeSubPath();
    g.setColour(biscuit);
    g.fillPath(front);
    const auto turn = biscuit.withMultipliedSaturation(1.4f).withMultipliedBrightness(0.93f); // warm, not grey
    juce::ColourGradient sides(turn.withAlpha(0.45f), 0.0f, 0.0f, turn.withAlpha(0.45f), w, 0.0f, false);
    sides.addColour(0.2, turn.withAlpha(0.12f));
    sides.addColour(0.5, turn.withAlpha(0.0f));
    sides.addColour(0.8, turn.withAlpha(0.12f));
    g.setGradientFill(sides);
    g.fillPath(front);
    g.setGradientFill(juce::ColourGradient(turn.withAlpha(0.0f), 0.0f, bar.getY(), turn.withAlpha(0.3f), 0.0f, bar.getBottom(), false));
    g.fillPath(front);
    // The rim: a white band along the lip, with a soft shade along its inner
    // edge, where the bowl's wall turns down into it.
    const float rim = 3.5f * scale;
    auto inner = lip;
    inner.applyTransform(juce::AffineTransform::translation(0.0f, -(0.5f * rim + 0.8f * scale)));
    g.setColour(biscuit.darker(0.12f).withAlpha(0.6f));
    g.strokePath(inner, juce::PathStrokeType(1.2f * scale));
    g.setColour(white);
    g.strokePath(lip, juce::PathStrokeType(rim, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    bar.removeFromTop(0.5f * rim); // the logo sits on the bowl below the rim

    // The logo as outlines, pressed into the bowl: filled a shade deeper
    // than it (biscuitShade), with a white copy below by a quarter of the
    // thickness of the O's bottom, showing as a white edge along the
    // letters' lower sides, as on a pressed-in letter lit from above. Centred
    // across on its ink and up and down on its capitals (the umlauts riding
    // above), then set letter by letter with its baseline on the curve
    // concentric with the lip through the middle letter's baseline (so the
    // lowest point stays put), each letter turned to the curve.
    const auto font = logoFont ? logoFont->withHeight(kLogoHeight * scale)
                               : juce::Font(juce::FontOptions(logoFace).withHeight(kLogoHeight * scale)
                                                .withKerningFactor(kLogoKerning));
    auto outline = [&font](const juce::String& s)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, s, 0.0f, 0.0f);
        juce::Path p;
        glyphs.createPath(p);
        return p;
    };
    const auto capitals = outline("L").getBounds(); // a capital's height, no accent
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, logo(), 0.0f, 0.0f); // baseline at 0
    const float dx = bar.getCentreX() - outline(logo()).getBounds().getCentreX();
    const float dy = bar.getCentreY() - capitals.getCentreY();
    const float lipY = (float) statusBar().getY(), base = dy; // the baseline, mid-window
    const float inside = base - lipY;                           // how far in from the lip
    juce::Path text;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
    {
        const auto& glyph = glyphs.getGlyph(i);
        if (glyph.isWhitespace()) continue;
        juce::Path letter;
        glyph.createPath(letter);
        const float cx = 0.5f * (glyph.getLeft() + glyph.getRight()), at = cx + dx;
        const float rise = bowlLip(at, w, scale, inside);
        const float slope = 0.5f * (bowlLip(at + 1.0f, w, scale, inside) - bowlLip(at - 1.0f, w, scale, inside));
        letter.applyTransform(juce::AffineTransform::translation(-cx, 0.0f)   // its baseline's centre...
                                  .rotated(-std::atan(slope))              // ...turned to the curve...
                                  .translated(at, base - rise));           // ...and set on it
        text.addPath(letter);
    }
    // The O's bottom, measured up its middle from its lowest ink to its hole.
    const auto o = outline("O");
    const auto ob = o.getBounds();
    const float x = ob.getCentreX(), step = 0.05f;
    float y = ob.getBottom();
    while (y > ob.getCentreY() && ! o.contains(x, y)) y -= step;  // into the ink
    const float inkBottom = y;
    while (y > ob.getCentreY() && o.contains(x, y)) y -= step;    // through to the hole
    const float bottom = inkBottom - y;
    auto edge = text;
    edge.applyTransform(juce::AffineTransform::translation(0.0f, bottom / 4));
    g.setColour(white);
    g.fillPath(edge);
    g.setColour(biscuitShade);
    g.fillPath(text);

    // The cat, over the bowl and the logo: it slides up from the window's
    // bottom edge, hidden below it, as the circle reaches its largest, until
    // its middle is on the edge, centred where it picked as it came up, and
    // sways on the beat.
    if (cat > 0.0f && catImage.isValid())
    {
        juce::Graphics::ScopedSaveState keep(g);
        g.setOpacity(1.0f);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.addTransform(catTurned(catAngle));
        g.drawImage(catImage, catBounds(), juce::RectanglePlacement::stretchToFit);
    }
}

void JungleEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    const int padSide = juce::jmin(w, h);
    pad.setBounds(0, 0, padSide, padSide); // the status bar takes the rest
    if (std::abs(h - heightFor(w)) > 1)
    {
        // A host that ignored the constraint: fix the ratio once it has
        // finished applying its size (a resize from inside it would not
        // reach the window).
        triggerAsyncUpdate();
        return;
    }
    side = w;
    proc.setEditorSize(w);
}

void JungleEditor::handleAsyncUpdate()
{
    auto r = getLocalBounds();
    if (std::abs(r.getHeight() - heightFor(r.getWidth())) <= 1) return;
    // The sides that differ from the last good size are the ones dragged.
    const juce::Rectangle<int> was { side, heightFor(side) };
    window.checkBounds(r, was, {}, false, false, r.getHeight() != was.getHeight(), r.getWidth() != side);
    setSize(r.getWidth(), r.getHeight());
}

void JungleEditor::updateMute()
{
    const bool command = juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
    proc.setMuteHeld(command && (isMouseOver(true) || pad.dragging()));
}

} // namespace jmui
