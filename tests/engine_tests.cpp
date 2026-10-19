// Plain C++ engine tests, no JUCE:
//   g++ -std=c++17 -O2 -I src tests/engine_tests.cpp -o /tmp/jm_tests && /tmp/jm_tests
#include "../src/engine/LoopEngine.h"
#include "../src/engine/Loudness.h"
#include "../src/engine/PadGeometry.h"
#include "../src/engine/Transport.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// At 1000 Hz and 60 BPM one beat is 1000 samples, so a 1-bar loop of 4000
// frames is read exactly one frame per sample and the output can be compared
// against the source sample for sample.
constexpr double kSr = 1000.0;
constexpr double kPi = 3.14159265358979323846; // M_PI isn't standard (MSVC lacks it)
constexpr int    kFade = 5; // 5 ms

struct Fixture {
    std::vector<float> a, b;          // a: 1 bar, 4000 frames; b: 2 bars, 8000 frames
    jm::Loop loops[2];
    jm::LoopEngine e;
    Fixture()
    {
        for (int k = 0; k < 4000; ++k) a.push_back((float) (k % 97) / 97.0f - 0.5f);
        for (int k = 0; k < 8000; ++k) b.push_back((float) ((k * 7) % 89) / 89.0f - 0.5f);
        loops[0] = { a.data(), a.data(), 4000, 1 };
        loops[1] = { b.data(), b.data(), 8000, 2 };
        e.prepare(kSr);
        e.setLoops(loops, 2);
        e.select(0);
    }
    float A(long k) const { return a[(size_t) (((k % 4000) + 4000) % 4000)]; }
    float B(long k) const { return b[(size_t) (((k % 8000) + 8000) % 8000)]; }
};

// Render n samples as host blocks of `block`, the host beat advancing from `beat`.
static std::vector<float> play(jm::LoopEngine& e, double beat, int n, double bpm = 60.0,
                               bool playing = true, int block = 100)
{
    std::vector<float> L(n, 0.0f), R(n, 0.0f);
    for (int s = 0; s < n; s += block)
    {
        const int m = std::min(block, n - s);
        e.beginBlock(playing, beat + s * bpm / 60.0 / kSr, bpm);
        e.render(L.data() + s, R.data() + s, m);
    }
    return L;
}

static bool near(float x, float y) { return std::abs(x - y) < 1e-4f; }

int main()
{
    // 1. Transport start at bar 1 plays the loop from its first sample and wraps seamlessly.
    {
        Fixture f;
        auto out = play(f.e, 0.0, 9000);
        bool ok = true;
        for (int i = 0; i < 9000; ++i) ok &= near(out[i], f.A(i));
        CHECK(ok, "start at beat 0: output equals the loop, wrapping every bar");
    }

    // 2. Starting mid-bar chases the song position (fades in, then exact).
    {
        Fixture f;
        auto out = play(f.e, 2.5, 2000);
        bool ok = true;
        for (int i = kFade; i < 2000; ++i) ok &= near(out[i], f.A(2500 + i));
        CHECK(ok, "start at beat 2.5: loop plays from its third beat and a half");
        CHECK(near(out[0], f.A(2500) / kFade), "start mid-loop fades in (first sample at 1/5 level)");
    }

    // 3. Varispeed: at 120 BPM the loop is read twice as fast.
    {
        Fixture f;
        auto out = play(f.e, 0.0, 3000, 120.0);
        bool ok = true;
        for (int i = 0; i < 3000; ++i) ok &= near(out[i], f.A(2 * i));
        CHECK(ok, "host tempo 120: two frames per sample");
    }

    // 4. Transport stop: a short fade, then silence.
    {
        Fixture f;
        play(f.e, 0.0, 1000);
        auto out = play(f.e, 1.0, 500, 60.0, false);
        bool silent = true;
        for (int i = kFade; i < 500; ++i) silent &= out[i] == 0.0f;
        CHECK(silent, "stopped transport is silent after the fade");
        CHECK(std::abs(out[0]) > 0.0f || f.A(1000) == 0.0f, "stop fades out rather than cutting");
    }

    // 5. Note-on mid-loop restarts at 1.1.1 from that sample.
    {
        Fixture f;
        std::vector<float> L(3000, 0.0f), R(3000, 0.0f);
        f.e.beginBlock(true, 0.0, 60.0);
        f.e.render(L.data(), R.data(), 1500);
        f.e.restart();
        f.e.render(L.data() + 1500, R.data() + 1500, 1500);
        bool ok = true;
        for (int k = kFade; k < 1500; ++k) ok &= near(L[1500 + k], f.A(k));
        CHECK(ok, "note-on at beat 1.5: loop restarts from its first sample");
        // ...and stays anchored there across later blocks
        auto more = play(f.e, 3.0, 1000);
        bool ok2 = true;
        for (int i = 0; i < 1000; ++i) ok2 &= near(more[i], f.A(1500 + i));
        CHECK(ok2, "restart anchor survives later host blocks");
    }

    // 6. A note-on where the loop already is at 1.1.1 changes nothing (no doubled downbeat).
    {
        Fixture f;
        play(f.e, 0.0, 4000);
        std::vector<float> L(100, 0.0f), R(100, 0.0f);
        f.e.beginBlock(true, 4.0, 60.0);
        f.e.restart();
        f.e.render(L.data(), R.data(), 100);
        bool ok = true;
        for (int i = 0; i < 100; ++i) ok &= near(L[i], f.A(i));
        CHECK(ok, "note-on on the bar line is seamless");
    }

    // 7. Switching loops keeps the position in the bar.
    {
        Fixture f;
        play(f.e, 0.0, 3000);
        f.e.beginBlock(true, 3.0, 60.0);
        f.e.select(1);
        std::vector<float> L(2000, 0.0f), R(2000, 0.0f);
        f.e.render(L.data(), R.data(), 2000);
        bool ok = true;
        for (int i = kFade; i < 2000; ++i) ok &= near(L[i], f.B(3000 + i));
        CHECK(ok, "switch at beat 3 lands on beat 3 of the 2-bar loop");
        int loop[4]; float gain[4]; double phase[4];
        CHECK(f.e.sounding(loop, gain, phase, 4) == 1 && loop[0] == 1, "the new loop is the one sounding");
    }

    // 8. A host jump (loop region) crossfades to the new position.
    {
        Fixture f;
        play(f.e, 0.0, 2000);
        auto out = play(f.e, 0.5, 1000);
        bool ok = true, bounded = true;
        for (int i = kFade; i < 1000; ++i) ok &= near(out[i], f.A(500 + i));
        for (int i = 0; i < kFade; ++i) bounded &= std::abs(out[i]) <= 1.0f;
        CHECK(ok, "jump back to beat 0.5 plays from there");
        CHECK(bounded, "jump crossfade stays bounded");
    }

    // 9. A jump by a whole number of loops (region of 2 bars, 1-bar loop) is seamless.
    {
        Fixture f;
        play(f.e, 0.0, 8000);
        auto out = play(f.e, 0.0, 200);
        bool ok = true;
        for (int i = 0; i < 200; ++i) ok &= near(out[i], f.A(i));
        CHECK(ok, "loop-region wrap on a loop boundary needs no crossfade");
    }

    // 10. Note-on while stopped is ignored; starting syncs to the song position.
    {
        Fixture f;
        play(f.e, 0.0, 1300);
        play(f.e, 1.3, 200, 60.0, false);
        f.e.restart();
        auto out = play(f.e, 9.0, 1000);
        bool ok = true;
        for (int i = kFade; i < 1000; ++i) ok &= near(out[i], f.A(1000 + i));
        CHECK(ok, "transport start at beat 9 plays the loop's second beat");
    }

    // 11. No drift: odd block sizes at a real rate and tempo stay phase-locked.
    {
        std::vector<float> a(65333, 0.0f);
        jm::Loop lp { a.data(), a.data(), 65333, 1 };
        jm::LoopEngine e;
        e.prepare(48000.0);
        e.setLoops(&lp, 1);
        e.select(0);
        std::vector<float> L(37), R(37);
        double beat = 0.0;
        const double bpm = 174.0;
        for (int blk = 0; blk < 100000; ++blk) // ~77 s
        {
            e.beginBlock(true, beat, bpm);
            e.render(L.data(), R.data(), 37);
            beat += 37 * bpm / 60.0 / 48000.0;
        }
        const double expect = std::fmod(beat, 4.0) / 4.0;
        int loop; float gain; double phase = -1.0;
        e.sounding(&loop, &gain, &phase, 1);
        CHECK(std::abs(phase - expect) < 1e-9, "phase locked to the host beat after 100k blocks");
    }

    // 12. A mix plays its loops together at their shares, all phase-locked.
    {
        Fixture f;
        const jm::MixVoice mix[2] = { { 0, 0.5f }, { 1, 0.5f } };
        f.e.setMix(mix, 2);
        auto out = play(f.e, 0.0, 9000);
        bool ok = true;
        for (int i = 0; i < 9000; ++i) ok &= near(out[i], 0.5f * f.A(i) + 0.5f * f.B(i));
        CHECK(ok, "half-and-half mix of a 1-bar and a 2-bar loop from bar 1");
    }

    // 13. Changing the shares ramps over the fade time, then holds the new mix.
    {
        Fixture f;
        const jm::MixVoice mix[2] = { { 0, 0.5f }, { 1, 0.5f } };
        f.e.setMix(mix, 2);
        play(f.e, 0.0, 1000);
        const jm::MixVoice next[2] = { { 0, 0.8f }, { 1, 0.2f } };
        f.e.setMix(next, 2);
        auto out = play(f.e, 1.0, 1000);
        bool ok = true;
        for (int i = kFade; i < 1000; ++i) ok &= near(out[i], 0.8f * f.A(1000 + i) + 0.2f * f.B(1000 + i));
        CHECK(ok, "new shares reached after the ramp");
        CHECK(! near(out[0], 0.8f * f.A(1000) + 0.2f * f.B(1000)) || near(f.A(1000), f.B(1000)),
              "shares ramp rather than jump");
    }

    // 14. A note-on restarts every loop in the mix.
    {
        Fixture f;
        const jm::MixVoice mix[2] = { { 0, 0.25f }, { 1, 0.75f } };
        f.e.setMix(mix, 2);
        std::vector<float> L(3000, 0.0f), R(3000, 0.0f);
        f.e.beginBlock(true, 0.0, 60.0);
        f.e.render(L.data(), R.data(), 1700);
        f.e.restart();
        f.e.render(L.data() + 1700, R.data() + 1700, 1300);
        bool ok = true;
        for (int k = kFade; k < 1300; ++k) ok &= near(L[1700 + k], 0.25f * f.A(k) + 0.75f * f.B(k));
        CHECK(ok, "note-on restarts the whole mix at 1.1.1");
    }

    // 15. Circle/polygon areas.
    {
        const double r = 0.1, pi = std::acos(-1.0);
        const jm::Poly big { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        const jm::Poly small { { -0.05, -0.05 }, { 0.05, -0.05 }, { 0.05, 0.05 }, { -0.05, 0.05 } };
        const jm::Poly half { { 0, -1 }, { 1, -1 }, { 1, 1 }, { 0, 1 } };
        CHECK(std::abs(jm::circlePolygonArea({ 0, 0 }, r, big) - pi * r * r) < 1e-12, "circle inside a polygon: whole disc");
        CHECK(std::abs(jm::circlePolygonArea({ 0, 0 }, r, small) - 0.01) < 1e-12, "polygon inside a circle: whole polygon");
        CHECK(std::abs(jm::circlePolygonArea({ 0, 0 }, r, half) - pi * r * r / 2) < 1e-12, "half-plane: half the disc");
    }

    // 16. Overlap weights: one cell, an even split, a four-way corner, the pad edge.
    {
        jm::PadMap two, four;
        two.setSites({ { 0.25, 0.5 }, { 0.75, 0.5 } });
        four.setSites({ { 0.25, 0.25 }, { 0.75, 0.25 }, { 0.25, 0.75 }, { 0.75, 0.75 } });
        jm::MixVoice m[8];
        int n = two.mix({ 0.25, 0.5 }, jm::kPuckRadius, m, 8);
        CHECK(n == 1 && m[0].loop == 0 && m[0].weight == 1.0f, "circle inside one cell: that break alone");
        n = two.mix({ 0.5, 0.5 }, jm::kPuckRadius, m, 8);
        CHECK(n == 2 && std::abs(m[0].weight - 0.5f) < 1e-6f && std::abs(m[1].weight - 0.5f) < 1e-6f,
              "circle on the border: an even split");
        n = four.mix({ 0.5, 0.5 }, jm::kPuckRadius, m, 8);
        bool quarter = n == 4;
        for (int k = 0; k < n; ++k) quarter &= std::abs(m[k].weight - 0.25f) < 1e-6f;
        CHECK(quarter, "circle on a four-cell corner: a quarter each");
        n = two.mix({ 0.5, 0.0 }, jm::kPuckRadius, m, 8);
        CHECK(n == 2 && std::abs(m[0].weight - 0.5f) < 1e-6f, "circle hanging over the pad edge: still an even split");
    }

    // 17. Random pads: weights add up to 1 and match a Monte Carlo area count.
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        std::vector<jm::Pt> sites;
        for (int i = 0; i < 97; ++i) sites.push_back({ u(rng), u(rng) });
        jm::PadMap pad;
        pad.setSites(sites);
        bool sums = true, matches = true;
        for (int trial = 0; trial < 200; ++trial)
        {
            const jm::Pt c { u(rng), u(rng) };
            jm::MixVoice m[8];
            const int n = pad.mix(c, jm::kPuckRadius, m, 8);
            float total = 0.0f;
            for (int k = 0; k < n; ++k) total += m[k].weight;
            sums &= std::abs(total - 1.0f) < 1e-6f;
            if (trial % 20 == 0) // area shares by sampling the disc
            {
                std::vector<int> hits(sites.size(), 0);
                int inside = 0;
                for (int s = 0; s < 40000; ++s)
                {
                    const double x = c.x + jm::kPuckRadius * (2 * u(rng) - 1), y = c.y + jm::kPuckRadius * (2 * u(rng) - 1);
                    if (std::hypot(x - c.x, y - c.y) > jm::kPuckRadius) continue;
                    ++inside;
                    size_t best = 0;
                    for (size_t i = 1; i < sites.size(); ++i)
                        if (std::hypot(x - sites[i].x, y - sites[i].y) < std::hypot(x - sites[best].x, y - sites[best].y)) best = i;
                    ++hits[best];
                }
                for (int k = 0; k < n; ++k)
                    matches &= std::abs(m[k].weight - (float) hits[(size_t) m[k].loop] / (float) inside) < 0.02f;
            }
        }
        CHECK(sums, "mix weights add up to 1");
        CHECK(matches, "mix weights match the sampled area shares");
    }

    // 18. Equal power: gains are the square roots of the shares.
    {
        jm::MixVoice m[3] = { { 0, 0.5f }, { 1, 0.25f }, { 2, 0.25f } };
        jm::equalPower(m, 3);
        CHECK(std::abs(m[0].weight * m[0].weight + m[1].weight * m[1].weight + m[2].weight * m[2].weight - 1.0f) < 1e-6f,
              "equal-power gains: squares add up to 1");
        CHECK(std::abs(m[1].weight - 0.5f) < 1e-6f, "a quarter share plays at half amplitude");
    }

    // 19. A mix asked for mid-step waits for the next 16th, its ramp ending on the step.
    {
        Fixture f;
        play(f.e, 0.0, 1100);               // up to beat 1.1
        f.e.select(1);
        auto out = play(f.e, 1.1, 1000);
        bool before = true, after = true;
        for (int i = 0; i < 150 - kFade; ++i) before &= near(out[i], f.A(1100 + i));
        for (int i = 150; i < 1000; ++i)     after  &= near(out[i], f.B(1100 + i));
        CHECK(before, "old mix holds until the ramp before the next step");
        CHECK(after, "new mix complete on the step at beat 1.25");
    }

    // 20. A newer request replaces the waiting one without delaying it.
    {
        Fixture f;
        play(f.e, 0.0, 1100);
        f.e.select(1);
        play(f.e, 1.1, 100);                // to beat 1.2, still waiting
        const jm::MixVoice half[2] = { { 0, 0.5f }, { 1, 0.5f } };
        f.e.setMix(half, 2);
        auto out = play(f.e, 1.2, 500);
        bool ok = true;
        for (int i = 50; i < 500; ++i) ok &= near(out[i], 0.5f * f.A(1200 + i) + 0.5f * f.B(1200 + i));
        CHECK(ok, "latest mix lands on the same step (beat 1.25)");
    }

    // 21. A note-on before the step takes the waiting mix at once.
    {
        Fixture f;
        play(f.e, 0.0, 1100);
        f.e.select(1);
        std::vector<float> L(600, 0.0f), R(600, 0.0f);
        f.e.beginBlock(true, 1.1, 60.0);
        f.e.render(L.data(), R.data(), 80); // to beat 1.18
        f.e.restart();
        f.e.render(L.data() + 80, R.data() + 80, 520);
        bool ok = true;
        for (int k = kFade; k < 520; ++k) ok &= near(L[80 + k], f.B(k));
        CHECK(ok, "note-on restarts straight into the waiting mix");
    }

    // 22. Steps are counted from 1.1.1, so after a restart the grid moves with it.
    {
        Fixture f;
        play(f.e, 0.0, 1100);                 // up to beat 1.1
        std::vector<float> L(1000, 0.0f), R(1000, 0.0f);
        f.e.beginBlock(true, 1.1, 60.0);
        f.e.restart();                        // 1.1.1 now at beat 1.1
        f.e.render(L.data(), R.data(), 100);  // to beat 1.2
        f.e.select(1);
        f.e.render(L.data() + 100, R.data() + 100, 900);
        bool before = true, after = true;
        for (int i = 100; i < 250 - kFade; ++i) before &= near(L[i], f.A(i));
        for (int i = 250; i < 1000; ++i)        after  &= near(L[i], f.B(i));
        CHECK(before && after, "after a restart at beat 1.1 the next step is at beat 1.35");
    }

    // 23. Timeline: holding the pad with the transport stopped auditions from
    //     1.1.1, letting go stops, a new press starts over.
    {
        jm::TransportFollower t;
        auto b = t.next(true, false, 7.3, 60.0, false, 100, kSr);
        CHECK(! b.playing, "stopped and not held: silent");
        b = t.next(true, false, 7.3, 60.0, true, 100, kSr);
        CHECK(b.playing && b.beat == 0.0, "press: audition from beat 0 (1.1.1), not the song position");
        b = t.next(true, false, 7.3, 60.0, true, 100, kSr);
        CHECK(b.playing && near((float) b.beat, 0.1f), "held: the audition runs on at the host tempo");
        b = t.next(true, false, 7.3, 60.0, false, 100, kSr);
        CHECK(! b.playing, "release: stops");
        t.next(true, false, 7.3, 60.0, true, 100, kSr);
        b = t.next(true, false, 7.3, 60.0, true, 100, kSr);
        CHECK(near((float) b.beat, 0.1f), "a new press starts over from 1.1.1");
    }

    // 24. Timeline hand-overs: the transport starting under a held pad takes
    //     over on the song position; stopping it under a held pad carries on.
    {
        jm::TransportFollower t;
        t.next(true, false, 0.0, 60.0, true, 100, kSr);
        auto b = t.next(true, true, 5.0, 60.0, true, 100, kSr);
        CHECK(b.playing && b.beat == 5.0 && b.resync, "transport start under a held pad resyncs to the song");
        b = t.next(true, true, 5.1, 60.0, true, 100, kSr);
        CHECK(! b.resync, "only the first transport block resyncs");
        b = t.next(true, false, 0.0, 60.0, true, 100, kSr);
        CHECK(b.playing && near((float) b.beat, 5.2f), "transport stop under a held pad carries on seamlessly");
        jm::TransportFollower solo;
        solo.next(false, false, 0.0, 60.0, false, 100, kSr);
        b = solo.next(false, false, 0.0, 60.0, false, 100, kSr);
        CHECK(b.playing && near((float) b.beat, 0.1f), "no transport at all: free run");
    }

    // 25. Audition through the engine: a press plays the loop from its first
    //     sample at full level; a resync moves it onto the song position.
    {
        Fixture f;
        jm::TransportFollower t;
        std::vector<float> L(1200, 0.0f), R(1200, 0.0f);
        for (int blk = 0; blk < 12; ++blk)
        {
            const bool host = blk >= 6;                          // transport starts at beat 2.5 halfway
            const auto b = t.next(true, host, 2.5 + (blk - 6) * 0.1, 60.0, true, 100, kSr);
            f.e.beginBlock(b.playing, b.beat, 60.0, b.resync);
            f.e.render(L.data() + blk * 100, R.data() + blk * 100, 100);
        }
        bool press = true, song = true;
        for (int i = 0; i < 600; ++i) press &= near(L[i], f.A(i));
        for (int i = 600 + kFade; i < 1200; ++i) song &= near(L[i], f.A(2500 + i - 600));
        CHECK(press, "press plays from the loop's first sample");
        CHECK(song, "transport start under the press moves to the song position");
    }

    // 26. Loudness: EBU Tech 3341's reference signals, 1 kHz stereo sines in
    //     level steps, read within its 0.1 LU, at 48 and 44.1 kHz.
    {
        auto sines = [](double rate, std::vector<std::pair<double, double>> steps) // {dBFS, seconds}
        {
            std::vector<float> x;
            for (auto [db, secs] : steps)
                for (long i = 0, n = std::lround(secs * rate); i < n; ++i)
                    x.push_back((float) (std::pow(10.0, db / 20.0) * std::sin(2.0 * kPi * 1000.0 * (double) x.size() / rate)));
            return x;
        };
        for (double rate : { 48000.0, 44100.0 })
        {
            auto a = sines(rate, { { -23.0, 20.0 } });
            auto b = sines(rate, { { -33.0, 20.0 } });
            auto c = sines(rate, { { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 } });
            auto d = sines(rate, { { -72.0, 10.0 }, { -36.0, 10.0 }, { -23.0, 60.0 }, { -36.0, 10.0 }, { -72.0, 10.0 } });
            auto at = [&](const std::vector<float>& x) { return jm::loopLoudness(x.data(), x.data(), (int) x.size(), rate); };
            CHECK(std::abs(at(a) + 23.0) < 0.1, "-23 dBFS sine reads -23 LUFS");
            CHECK(std::abs(at(b) + 33.0) < 0.1, "-33 dBFS sine reads -33 LUFS");
            CHECK(std::abs(at(c) + 23.0) < 0.1, "relative gate drops the quiet ends");
            CHECK(std::abs(at(d) + 23.0) < 0.1, "absolute gate drops the near-silence");
        }
    }

    // 27. The break gain lands a loop on the target; silence is left alone.
    {
        std::vector<float> x;
        for (int i = 0; i < 88200; ++i) x.push_back((float) (0.3 * std::sin(i * 0.05) * std::exp(-(i % 11025) / 2000.0)));
        const float g = jm::gainFor(jm::loopLoudness(x.data(), x.data(), (int) x.size(), 44100.0));
        for (auto& v : x) v *= g;
        CHECK(std::abs(jm::loopLoudness(x.data(), x.data(), (int) x.size(), 44100.0) - jm::kBreakLufs) < 0.01,
              "gained loop reads the target");
        std::vector<float> quiet(44100, 0.0f);
        CHECK(jm::exactlyEqual(jm::gainFor(jm::loopLoudness(quiet.data(), quiet.data(), 44100, 44100.0)), 1.0f),
              "silence keeps unity gain");
    }

    // 28. An immediate mix (a fine drag) ramps in at once, replacing a waiting one.
    {
        Fixture f;
        play(f.e, 0.0, 1100);               // up to beat 1.1
        f.e.select(1);                      // would wait for beat 1.25
        const jm::MixVoice b { 1, 1.0f };
        f.e.setMix(&b, 1, true);
        auto out = play(f.e, 1.1, 300);
        bool ok = true;
        for (int i = kFade; i < 300; ++i) ok &= near(out[i], f.B(1100 + i));
        CHECK(ok, "immediate mix complete one fade after the request");
    }

    // 29. Mute and unmute act at once, mid-step, over one fade; the loop runs
    //     on underneath, so it comes back in place.
    {
        Fixture f;
        play(f.e, 0.0, 1100);
        f.e.setMuted(true);                 // at beat 1.1, between steps
        auto out = play(f.e, 1.1, 400);
        bool silent = true;
        for (int i = kFade + 1; i < 400; ++i) silent &= near(out[i], 0.0f);
        CHECK(silent, "muted one fade after the request, not on the next step");
        f.e.setMuted(false);                // at beat 1.5
        out = play(f.e, 1.5, 400);
        bool back = true;
        for (int i = kFade + 1; i < 400; ++i) back &= near(out[i], f.A(1500 + i));
        CHECK(back, "unmuted one fade after, the loop where it would have been");
    }

    // 30. Muted while stopped: a start is silent from its first sample.
    {
        Fixture f;
        f.e.setMuted(true);
        auto out = play(f.e, 0.0, 600);
        bool silent = true;
        for (float v : out) silent &= near(v, 0.0f);
        CHECK(silent, "start while muted is silent");
    }

    if (failures == 0) std::printf("all engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
