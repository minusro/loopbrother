#include "Cereal.h"

namespace jmui {

namespace C = juce::Colours;

juce::Colour shadowOf(juce::Colour base)
{
    return base.withRotatedHue(-0.05f).withMultipliedSaturation(1.3f).withMultipliedBrightness(0.6f);
}

//                                  tube  light gloss width grain size  sugar bumps jelly outl  shadow
const RingStyle kRingStyles[] = {
    { "Glossy bevel",               1.0f, 1.0f, 0.7f, 0.14f, 0.5f, 1.0f, 0.0f, 0.0f,  0.0f, 0.5f, 0.16f },
    { "Matte cereal",               0.8f, 0.8f, 0.0f, 0.0f,  0.9f, 1.6f, 0.0f, 0.03f, 0.0f, 0.3f, 0.14f },
    { "Frosted sugar",              0.7f, 0.8f, 0.15f, 0.3f, 0.7f, 1.2f, 0.6f, 0.025f, 0.0f, 0.3f, 0.14f },
    { "Puffed and rough",           0.9f, 0.9f, 0.0f, 0.0f,  1.0f, 2.6f, 0.0f, 0.07f, 0.0f, 0.35f, 0.16f },
    { "Candy shine",                1.3f, 1.3f, 1.0f, 0.2f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.8f, 0.25f },
    { "Jelly",                      0.5f, 0.7f, 0.9f, 0.12f, 0.0f, 1.0f, 0.0f, 0.0f,  1.0f, 0.6f, 0.12f },
    { "Soft clay",                  1.0f, 0.6f, 0.3f, 0.35f, 0.15f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f },
    { "Flat pastel",                0.0f, 0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f, 0.0f,  0.0f, 0.3f, 0.0f },
};
const int kNumRingStyles = (int) std::size(kRingStyles);
const RingStyle& kRingStyle = kRingStyles[0];

Grains::Grains()
{
    // Tileable value noise in octaves, from blotches 16 px across down to
    // single pixels: scaled up for a coarse grain it stays soft, never
    // squares. Light where it is above zero, the colour's shadow below.
    constexpr int n = 128;
    std::vector<float> field((size_t) n * n, 0.0f);
    juce::Random rnd(1);
    const std::pair<int, float> octaves[] = { { 16, 0.35f }, { 8, 0.3f }, { 4, 0.25f }, { 2, 0.2f }, { 1, 0.2f } };
    for (const auto& [cell, amp] : octaves)
    {
        const int m = n / cell; // lattice points per side; wrapping keeps it tileable
        std::vector<float> lattice((size_t) m * m);
        for (auto& v : lattice) v = rnd.nextFloat() * 2.0f - 1.0f;
        auto at = [&](int i, int j) { return lattice[(size_t) (((j % m) + m) % m * m + ((i % m) + m) % m)]; };
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float fx = (float) x / cell, fy = (float) y / cell;
                const int i = (int) fx, j = (int) fy;
                auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
                const float u = smooth(fx - i), v = smooth(fy - j);
                const float top = at(i, j) + u * (at(i + 1, j) - at(i, j));
                const float bottom = at(i, j + 1) + u * (at(i + 1, j + 1) - at(i, j + 1));
                field[(size_t) (y * n + x)] += amp * (top + v * (bottom - top));
            }
    }
    for (int k = 0; k < kFrootLoops; ++k)
    {
        const auto dark = shadowOf(frootLoops[k]);
        auto& img = images[k];
        img = juce::Image(juce::Image::ARGB, n, n, true);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float v = juce::jlimit(-1.0f, 1.0f, field[(size_t) (y * n + x)]);
                img.setPixelAt(x, y, v < 0.0f ? dark.withAlpha(-0.45f * v) : C::white.withAlpha(0.4f * v));
            }
    }
}

namespace {

constexpr float kTau = juce::MathConstants<float>::twoPi;

// A closed round outline: radius r about c, made uneven by `bumps` in a
// pattern that seed picks and keeps.
juce::Path blob(juce::Point<float> c, float r, float bumps, int seed)
{
    juce::Path p;
    if (bumps <= 0.0f)
    {
        p.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
        return p;
    }
    juce::Random rnd(seed);
    float phase[4];
    for (auto& v : phase) v = rnd.nextFloat() * kTau;
    constexpr int n = 64;
    for (int i = 0; i < n; ++i)
    {
        const float a = kTau * (float) i / n;
        const float k = 1.0f + bumps * (0.5f * std::sin(3 * a + phase[0]) + 0.3f * std::sin(5 * a + phase[1])
                                        + 0.2f * std::sin(8 * a + phase[2]) + 0.15f * std::sin(13 * a + phase[3]));
        const juce::Point<float> q { c.x + r * k * std::sin(a), c.y - r * k * std::cos(a) };
        if (i == 0) p.startNewSubPath(q);
        else        p.lineTo(q);
    }
    p.closeSubPath();
    return p;
}

// The Ö: a thick ring (the cereal) with the dots over it as on the letter.
struct UmlautO {
    juce::Path ring, dots, all;
    float hole, d;
    juce::Point<float> dot[2];
    UmlautO(juce::Point<float> c, float r, float bumps, int seed) : hole(0.48f * r), d(0.24f * r)
    {
        ring = blob(c, r, bumps, seed);
        ring.addPath(blob(c, hole, 0.7f * bumps, seed + 7));
        ring.setUsingNonZeroWinding(false); // the hole stays empty
        const float y = c.y - r - 1.55f * d;
        dot[0] = { c.x - 0.5f * r, y };
        dot[1] = { c.x + 0.5f * r, y };
        dots.addPath(blob(dot[0], d, bumps, seed + 13));
        dots.addPath(blob(dot[1], d, bumps, seed + 17));
        all = ring;
        all.addPath(dots);
    }
};

void sphere(juce::Graphics& g, juce::Point<float> at, float d, juce::Colour dark, const RingStyle& s)
{
    if (s.light <= 0.0f) return;
    juce::ColourGradient lit(C::white.withAlpha(juce::jmin(1.0f, 0.6f * s.light)), at.translated(-0.35f * d, -0.35f * d),
                             dark.withAlpha(juce::jmin(1.0f, 0.4f * s.light)), at.translated(d, d), true);
    lit.addColour(0.45, C::white.withAlpha(0.0f));
    g.setGradientFill(lit);
    g.fillEllipse(at.x - d, at.y - d, 2 * d, 2 * d);
}

void grainOver(juce::Graphics& g, const juce::Path& p, juce::Point<float> c, float r, const juce::Image& grain, const RingStyle& s)
{
    if (s.grain <= 0.0f) return;
    juce::Graphics::ScopedSaveState keep(g);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality); // smooth blotches, not squares
    juce::FillType texture(grain, juce::AffineTransform::scale(s.grainSize * r / 14.0f).translated(c));
    texture.setOpacity(juce::jmin(1.0f, s.grain));
    g.setFillType(texture);
    g.fillPath(p);
}

} // namespace

void drawRingShadow(juce::Graphics& g, juce::Point<float> c, float r, const RingStyle& s, int seed)
{
    if (s.shadow <= 0.0f) return;
    juce::DropShadow(C::black.withAlpha(s.shadow), juce::jmax(1, juce::roundToInt(0.35f * r)),
                     { juce::roundToInt(0.08f * r), juce::roundToInt(0.16f * r) })
        .drawForPath(g, UmlautO(c, r, s.bumps, seed).all);
}

void drawRing(juce::Graphics& g, juce::Point<float> c, float r, int colour, const Grains& grains, const RingStyle& s, int seed)
{
    const auto base = frootLoop(colour), dark = shadowOf(base);
    const UmlautO o(c, r, s.bumps, seed);
    g.setColour(base);
    g.fillPath(o.all);

    if (s.jelly > 0.0f) // candy: the rim deeper in colour, the middle of the tube lit through
    {
        const auto rich = base.withMultipliedSaturation(1.6f).withMultipliedBrightness(0.95f);
        juce::ColourGradient body(rich.withAlpha(0.7f * s.jelly), c, rich.withAlpha(0.8f * s.jelly), { c.x + r, c.y }, true);
        body.addColour(o.hole / r, rich.withAlpha(0.7f * s.jelly));
        body.addColour((o.hole / r + 1.0f) / 2.0f, C::white.withAlpha(0.45f * s.jelly));
        g.setGradientFill(body);
        g.fillPath(o.ring);
    }
    if (s.tube > 0.0f)
    {
        juce::ColourGradient tube(dark.withAlpha(0.0f), c, dark.withAlpha(juce::jmin(1.0f, 0.35f * s.tube)), { c.x + r, c.y }, true);
        tube.addColour(o.hole / r, dark.withAlpha(juce::jmin(1.0f, 0.25f * s.tube)));
        tube.addColour((o.hole / r + 1.0f) / 2.0f, C::white.withAlpha(juce::jmin(1.0f, 0.10f * s.tube)));
        g.setGradientFill(tube);
        g.fillPath(o.ring);
    }
    for (const auto& at : o.dot) sphere(g, at, o.d, dark, s);
    if (s.light > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(C::white.withAlpha(juce::jmin(1.0f, 0.35f * s.light)), c.x - r, c.y - r,
                                               dark.withAlpha(juce::jmin(1.0f, 0.22f * s.light)), c.x + r, c.y + r, false));
        g.fillPath(o.all);
    }
    if (s.gloss > 0.0f)
    {
        juce::Path streak;
        streak.addCentredArc(c.x, c.y, (o.hole + r) / 2, (o.hole + r) / 2, 0.0f, -1.45f, -0.45f, true);
        g.setColour(C::white.withAlpha(juce::jmin(1.0f, 0.7f * s.gloss)));
        g.strokePath(streak, juce::PathStrokeType(s.glossWidth * r, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    grainOver(g, o.all, c, r, grains[colour], s);
    if (s.sugar > 0.0f) // crystals scattered over the ring, the same ones every frame
    {
        juce::Random rnd(seed * 31 + 5);
        const int count = juce::jlimit(0, 160, juce::roundToInt(s.sugar * 60.0f * (r / 10.0f) * (r / 10.0f)));
        for (int i = 0; i < count; ++i)
        {
            const float a = rnd.nextFloat() * kTau;
            const float rr = std::sqrt(o.hole * o.hole + rnd.nextFloat() * (r * r - o.hole * o.hole));
            const float sz = r * (0.025f + 0.035f * rnd.nextFloat());
            g.setColour(C::white.withAlpha(0.55f + 0.4f * rnd.nextFloat()));
            g.fillEllipse(c.x + rr * std::sin(a) - sz, c.y - rr * std::cos(a) - sz, 2 * sz, 2 * sz);
        }
    }
    if (s.outline > 0.0f)
    {
        g.setColour(dark.withAlpha(juce::jmin(1.0f, s.outline)));
        g.strokePath(o.all, juce::PathStrokeType(juce::jmax(0.6f, 0.05f * r)));
    }
}

void drawCrumb(juce::Graphics& g, juce::Point<float> c, float r, int colour, const Grains& grains, const RingStyle& s)
{
    const auto base = frootLoop(colour), dark = shadowOf(base);
    if (s.shadow > 0.0f) // too small to need a blurred one
    {
        g.setColour(C::black.withAlpha(0.8f * s.shadow));
        g.fillEllipse(c.x - r + 0.1f * r, c.y - r + 0.3f * r, 2 * r, 2 * r);
    }
    g.setColour(base);
    g.fillEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    sphere(g, c, r, dark, s);
    juce::Path p;
    p.addEllipse(c.x - r, c.y - r, 2 * r, 2 * r);
    grainOver(g, p, c, r * 4.0f, grains[colour], s);
    if (s.outline > 0.0f)
    {
        g.setColour(dark.withAlpha(juce::jmin(1.0f, s.outline)));
        g.drawEllipse(c.x - r, c.y - r, 2 * r, 2 * r, juce::jmax(0.5f, 0.1f * r));
    }
}

// ---------------------------------------------------------------- sprites
void CerealSprites::configure(float ringRadius, float crumbRadius, float pixelScale, const RingStyle& s)
{
    if (style == &s && juce::exactlyEqual(rest, ringRadius) && juce::exactlyEqual(crumb, crumbRadius)
        && juce::exactlyEqual(density, pixelScale))
        return;
    style = &s;
    rest = ringRadius;
    crumb = crumbRadius;
    density = pixelScale;
    variants = s.bumps > 0.0f || s.sugar > 0.0f ? 4 : 1; // patterns per colour
    rings.clear();
    for (auto& img : crumbs) img = {};
}

// Ö sizes on hand: half, full and double the rest size (a loop's silence and
// peak). Each is drawn from the smallest one at least as large, so never
// enlarged and never shrunk by more than half.
const CerealSprites::Sprite& CerealSprites::ring(float r, int colour, int seed)
{
    int level = 0;
    while (level < 2 && rest * std::ldexp(0.5f, level) < r) ++level;
    const int variant = (seed % variants + variants) % variants;
    const int key = (colour * 4 + variant) * 3 + level;
    auto it = rings.find(key);
    if (it != rings.end()) return it->second;

    Sprite sp;
    sp.radius = rest * std::ldexp(0.5f, level);
    const float R = sp.radius * density;                 // in sprite pixels
    const int w = (int) std::ceil(3.2f * R) + 4, h = (int) std::ceil(4.0f * R) + 4;
    sp.centre = { 0.5f * (float) w - 0.15f * R, 2.3f * R + 2.0f };
    const int s = colour * 101 + variant;
    sp.image = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(sp.image);
    jmui::drawRingShadow(g, sp.centre, R, *style, s);
    jmui::drawRing(g, sp.centre, R, colour, grains, *style, s);
    return rings.emplace(key, std::move(sp)).first->second;
}

void CerealSprites::draw(juce::Graphics& g, const juce::Image& img, juce::Point<float> centre, juce::Point<float> c, float k) const
{
    juce::Graphics::ScopedSaveState keep(g);
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed(img, juce::AffineTransform::translation(-centre).scaled(k).translated(c));
}

void CerealSprites::drawRing(juce::Graphics& g, juce::Point<float> c, float r, int colour, int seed)
{
    const auto& sp = ring(r, colour, seed);
    draw(g, sp.image, sp.centre, c, r / (sp.radius * density));
}

void CerealSprites::drawCrumb(juce::Graphics& g, juce::Point<float> c, int colour)
{
    auto& img = crumbs[(size_t) juce::jlimit(0, kFrootLoops - 1, colour)];
    const float R = crumb * density;
    const int side = (int) std::ceil(3.0f * R) + 4;
    const juce::Point<float> centre { 0.5f * (float) side - 0.2f * R, 0.5f * (float) side - 0.3f * R };
    if (! img.isValid())
    {
        img = juce::Image(juce::Image::ARGB, side, side, true);
        juce::Graphics sg(img);
        jmui::drawCrumb(sg, centre, R, colour, grains, *style);
    }
    draw(g, img, centre, c, 1.0f / density);
}

} // namespace jmui
