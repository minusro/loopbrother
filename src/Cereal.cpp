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
    { "Soft clay, no gloss",        1.0f, 0.6f, 0.0f, 0.0f,  0.15f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f },
};
const int kNumRingStyles = (int) std::size(kRingStyles);
const RingStyle& kRingStyle = kRingStyles[8]; // Soft clay, no gloss

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
    for (int k = 0; k <= kMutedColour; ++k)
    {
        const auto dark = shadowOf(frootLoop(k));
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

// The O: a thick ring, the cereal. Its hole is about half its radius.
constexpr float kHole = 0.48f; // as a fraction of the ring's radius
juce::Path ringPath(juce::Point<float> c, float r, float bumps, int seed)
{
    auto p = blob(c, r, bumps, seed);
    p.addPath(blob(c, kHole * r, 0.7f * bumps, seed + 7));
    p.setUsingNonZeroWinding(false); // the hole stays empty
    return p;
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

static juce::DropShadow shadowFor(float r, const RingStyle& s)
{
    return juce::DropShadow(C::black.withAlpha(s.shadow), juce::jmax(1, juce::roundToInt(0.35f * r)),
                            { juce::roundToInt(0.08f * r), juce::roundToInt(0.16f * r) });
}

void drawOShadow(juce::Graphics& g, juce::Point<float> c, float r, const RingStyle& s, int seed)
{
    if (s.shadow > 0.0f) shadowFor(r, s).drawForPath(g, ringPath(c, r, s.bumps, seed));
}

void drawO(juce::Graphics& g, juce::Point<float> c, float r, int colour, const Grains& grains, const RingStyle& s, int seed)
{
    const auto base = frootLoop(colour), dark = shadowOf(base);
    const auto ring = ringPath(c, r, s.bumps, seed);
    const float hole = kHole * r;
    g.setColour(base);
    g.fillPath(ring);

    if (s.jelly > 0.0f) // candy: the rim deeper in colour, the middle of the tube lit through
    {
        const auto rich = base.withMultipliedSaturation(1.6f).withMultipliedBrightness(0.95f);
        juce::ColourGradient body(rich.withAlpha(0.7f * s.jelly), c, rich.withAlpha(0.8f * s.jelly), { c.x + r, c.y }, true);
        body.addColour(kHole, rich.withAlpha(0.7f * s.jelly));
        body.addColour((kHole + 1.0f) / 2.0f, C::white.withAlpha(0.45f * s.jelly));
        g.setGradientFill(body);
        g.fillPath(ring);
    }
    if (s.tube > 0.0f)
    {
        juce::ColourGradient tube(dark.withAlpha(0.0f), c, dark.withAlpha(juce::jmin(1.0f, 0.35f * s.tube)), { c.x + r, c.y }, true);
        tube.addColour(kHole, dark.withAlpha(juce::jmin(1.0f, 0.25f * s.tube)));
        tube.addColour((kHole + 1.0f) / 2.0f, C::white.withAlpha(juce::jmin(1.0f, 0.10f * s.tube)));
        g.setGradientFill(tube);
        g.fillPath(ring);
    }
    if (s.light > 0.0f)
    {
        g.setGradientFill(juce::ColourGradient(C::white.withAlpha(juce::jmin(1.0f, 0.35f * s.light)), c.x - r, c.y - r,
                                               dark.withAlpha(juce::jmin(1.0f, 0.22f * s.light)), c.x + r, c.y + r, false));
        g.fillPath(ring);
    }
    if (s.gloss > 0.0f)
    {
        juce::Path streak;
        streak.addCentredArc(c.x, c.y, (hole + r) / 2, (hole + r) / 2, 0.0f, -1.45f, -0.45f, true);
        g.setColour(C::white.withAlpha(juce::jmin(1.0f, 0.7f * s.gloss)));
        g.strokePath(streak, juce::PathStrokeType(s.glossWidth * r, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    grainOver(g, ring, c, r, grains[colour], s);
    if (s.sugar > 0.0f) // crystals scattered over the ring, the same ones every frame
    {
        juce::Random rnd(seed * 31 + 5);
        const int count = juce::jlimit(0, 160, juce::roundToInt(s.sugar * 60.0f * (r / 10.0f) * (r / 10.0f)));
        for (int i = 0; i < count; ++i)
        {
            const float a = rnd.nextFloat() * kTau;
            const float rr = std::sqrt(hole * hole + rnd.nextFloat() * (r * r - hole * hole));
            const float sz = r * (0.025f + 0.035f * rnd.nextFloat());
            g.setColour(C::white.withAlpha(0.55f + 0.4f * rnd.nextFloat()));
            g.fillEllipse(c.x + rr * std::sin(a) - sz, c.y - rr * std::cos(a) - sz, 2 * sz, 2 * sz);
        }
    }
    if (s.outline > 0.0f)
    {
        g.setColour(dark.withAlpha(juce::jmin(1.0f, s.outline)));
        g.strokePath(ring, juce::PathStrokeType(juce::jmax(0.6f, 0.05f * r)));
    }
}

// ---------------------------------------------------------------- sprites
void CerealSprites::configure(float ringRadius, float pixelScale, const RingStyle& s)
{
    if (style == &s && juce::exactlyEqual(rest, ringRadius) && juce::exactlyEqual(density, pixelScale))
        return;
    style = &s;
    rest = ringRadius;
    density = pixelScale;
    variants = s.bumps > 0.0f || s.sugar > 0.0f ? 4 : 1; // patterns per colour
    cache.clear();
}

// Sizes on hand: half, full and double the rest size (a loop's silence and
// peak). Each is drawn from the smallest one at least as large, so never
// enlarged and never shrunk by more than half.
const CerealSprites::Sprite& CerealSprites::sprite(float r, int colour, int seed)
{
    int level = 0;
    while (level < 2 && rest * std::ldexp(0.5f, level) < r) ++level;
    const int variant = (seed % variants + variants) % variants;
    const int key = (colour * 4 + variant) * 3 + level;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    Sprite sp;
    sp.radius = rest * std::ldexp(0.5f, level);
    const float R = sp.radius * density;                 // in sprite pixels
    const float half = 1.15f * R;                        // the ring, bumps included
    const int w = (int) std::ceil(2.0f * half + 0.8f * R) + 4;
    sp.centre = { half + 0.2f * R + 2.0f, half + 0.2f * R + 2.0f };
    sp.image = juce::Image(juce::Image::ARGB, w, w, true);
    juce::Graphics g(sp.image);
    const int s = colour * 101 + variant;
    jmui::drawOShadow(g, sp.centre, R, *style, s);
    jmui::drawO(g, sp.centre, R, colour, grains, *style, s);
    return cache.emplace(key, std::move(sp)).first->second;
}

void CerealSprites::drawO(juce::Graphics& g, juce::Point<float> c, float r, int colour, int seed, float axis, float squeeze)
{
    const auto& sp = sprite(r, colour, seed);
    const float k = r / (sp.radius * density);
    juce::Graphics::ScopedSaveState keep(g);
    g.setOpacity(1.0f); // images draw at the context's opacity: whatever was filled last mustn't fade them
    g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
    g.drawImageTransformed(sp.image, juce::AffineTransform::translation(-sp.centre)
                                         .rotated(-axis).scaled(k * squeeze, k).rotated(axis) // flattened along axis
                                         .translated(c));
}

} // namespace jmui
