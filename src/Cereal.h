#pragma once
#include <juce_graphics/juce_graphics.h>
#include <map>

// The breaks' marks, drawn as Froot Loops: an Ö (a ring with two dots over
// it) for the breaks under the selection circle, a small round dot for the
// others. The look is a RingStyle; kRingStyles holds the ones on offer
// (docs/ring-styles.png shows them all), kRingStyle the one in use.
namespace jmui {

// The Froot Loops colours, pastel, in rainbow order: one per similarity
// cluster (breaks/colours.json), so similar breaks share or neighbour one.
const juce::Colour frootLoops[] = {
    juce::Colour { 0xffff8484 },  // red
    juce::Colour { 0xffffae5e },  // orange
    juce::Colour { 0xffffdd55 },  // yellow
    juce::Colour { 0xff8edb6e },  // green
    juce::Colour { 0xff6ec1f5 },  // blue
    juce::Colour { 0xffb192f2 },  // purple
    juce::Colour { 0xffff94ce },  // pink
};
constexpr int kFrootLoops = (int) std::size(frootLoops);
// One colour past the cereal ones: the dark grey of the marks while muted.
constexpr int kMutedColour = kFrootLoops;
const juce::Colour mutedGrey { 0xff555555 };
inline juce::Colour frootLoop(int colour)
{
    return colour == kMutedColour ? mutedGrey : frootLoops[(size_t) juce::jlimit(0, kFrootLoops - 1, colour)];
}

// The colour a cereal's shadows take: darker, richer and turned a little
// towards red, so yellow shades to gold instead of olive.
juce::Colour shadowOf(juce::Colour base);

struct RingStyle {
    const char* name;
    float tube;        // the ring rounded like a tube: darker towards both edges (0 = flat)
    float light;       // light from the top left, shade towards the bottom right
    float gloss;       // a specular streak on the upper left of the ring
    float glossWidth;  // its width, as a fraction of the radius
    float grain;       // speckled texture, opacity
    float grainSize;   // its coarseness (1 = fine)
    float sugar;       // sugar crystals: bright specks, density
    float bumps;       // an uneven, puffed edge, as a fraction of the radius
    float jelly;       // translucent candy: a lit body and a saturated rim
    float outline;     // a thin edge in the shadow colour
    float shadow;      // the drop shadow's strength
};

extern const RingStyle kRingStyles[];
extern const int kNumRingStyles;
extern const RingStyle& kRingStyle;   // the one the pad draws

// Per-colour speckle textures, made once.
class Grains {
public:
    Grains();
    const juce::Image& operator[](int colour) const { return images[(size_t) juce::jlimit(0, kMutedColour, colour)]; }
private:
    juce::Image images[kFrootLoops + 1];
};

// An Ö of radius r centred (the ring) on c, its dots turned by angle about
// the ring's centre (radians, clockwise, 0 = over it as on the letter).
// seed makes each mark's bumps and sugar its own and keeps them still from
// frame to frame.
void drawRingShadow(juce::Graphics&, juce::Point<float> c, float r, const RingStyle&, int seed, float angle = 0.0f);
void drawRing(juce::Graphics&, juce::Point<float> c, float r, int colour, const Grains&, const RingStyle&, int seed,
              float angle = 0.0f);
// Its parts: the O, and one umlaut dot (k = 0 left, 1 right of the upright Ö)
// and where it sits.
void drawO(juce::Graphics&, juce::Point<float> c, float r, int colour, const Grains&, const RingStyle&, int seed);
void drawOShadow(juce::Graphics&, juce::Point<float> c, float r, const RingStyle&, int seed);
void drawUmlaut(juce::Graphics&, juce::Point<float> at, float r, int colour, const Grains&, const RingStyle&, int seed);
void drawUmlautShadow(juce::Graphics&, juce::Point<float> at, float r, const RingStyle&, int seed);
juce::Point<float> umlautAt(juce::Point<float> c, float r, int k, float angle);

// A small round piece, the mark of a break the circle doesn't cover.
void drawCrumb(juce::Graphics&, juce::Point<float> c, float r, int colour, const Grains&, const RingStyle&);

// The same marks, pre-rendered: each Ö's O and dots are drawn once per
// colour (and per bump/sugar pattern, for the styles that have them) at
// three sizes, at the screen's real pixel density, shadows included, then
// drawn scaled down to the size they need, the dots wherever the angle puts
// them (the light stays on the top left). Painting 65 shaded Ös (the largest circle) from scratch
// took 18 ms at 480 px and 84 at 1440 (software rendering); from the cache,
// one image draw each.
class CerealSprites {
public:
    // ringRadius: an Ö at rest; crumbRadius: a crumb (both in pixels).
    // Clears the cache when anything that changes the pictures changes.
    void configure(float ringRadius, float crumbRadius, float pixelScale, const RingStyle& style);
    void drawRing(juce::Graphics&, juce::Point<float> c, float r, int colour, int seed, float angle = 0.0f);
    void drawCrumb(juce::Graphics&, juce::Point<float> c, int colour);

private:
    struct Sprite { juce::Image image; juce::Point<float> centre; float radius = 0.0f; }; // centre in sprite pixels
    const Sprite& sprite(float r, int colour, int seed, int part);
    void draw(juce::Graphics&, const juce::Image&, juce::Point<float> centre, juce::Point<float> c, float k) const;

    Grains grains;
    const RingStyle* style = nullptr;
    float rest = 0.0f, crumb = 0.0f, density = 0.0f;
    int variants = 1;
    std::map<int, Sprite> cache;
    juce::Image crumbs[kFrootLoops];
};

} // namespace jmui
