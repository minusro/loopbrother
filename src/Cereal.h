#pragma once
#include <juce_graphics/juce_graphics.h>
#include <map>

// The breaks' marks, drawn as Froot Loops: a cereal ring, full size for the
// breaks under the selection circle, small for the others. The look is a RingStyle; kRingStyles holds the ones on offer
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

// A ring of radius r centred on c. seed makes each mark's bumps and sugar
// its own and keeps them still from frame to frame.
void drawO(juce::Graphics&, juce::Point<float> c, float r, int colour, const Grains&, const RingStyle&, int seed);
void drawOShadow(juce::Graphics&, juce::Point<float> c, float r, const RingStyle&, int seed);

// The same marks, pre-rendered: each ring is drawn once per colour (and per
// bump/sugar pattern, for the styles that have them) at three sizes, at the
// screen's real pixel density, shadow included, then drawn scaled down to
// the size it needs. Painting 65 shaded marks (the largest circle) from
// scratch took 18 ms at 480 px and 84 at 1440 (software rendering); from
// the cache, one image draw each.
class CerealSprites {
public:
    // ringRadius: an Ö at rest, in pixels. Clears the cache when anything
    // that changes the pictures changes.
    void configure(float ringRadius, float pixelScale, const RingStyle& style);
    // squeeze < 1 flattens it along axis (radians, on screen).
    void drawO(juce::Graphics&, juce::Point<float> c, float r, int colour, int seed, float axis = 0.0f, float squeeze = 1.0f);

private:
    struct Sprite { juce::Image image; juce::Point<float> centre; float radius = 0.0f; }; // centre in sprite pixels
    const Sprite& sprite(float r, int colour, int seed);

    Grains grains;
    const RingStyle* style = nullptr;
    float rest = 0.0f, density = 0.0f;
    int variants = 1;
    std::map<int, Sprite> cache;
};

} // namespace jmui
