// Plain C++ engine tests, no JUCE:
//   g++ -std=c++17 -O2 -I src tests/engine_tests.cpp -o /tmp/jm_tests && /tmp/jm_tests
#include "../src/engine/LoopEngine.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// At 1000 Hz and 60 BPM one beat is 1000 samples, so a 1-bar loop of 4000
// frames is read exactly one frame per sample and the output can be compared
// against the source sample for sample.
constexpr double kSr = 1000.0;
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
        CHECK(f.e.sounding() == 1, "the new loop is the one sounding");
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
        CHECK(std::abs(e.phase() - expect) < 1e-9, "phase locked to the host beat after 100k blocks");
    }

    if (failures == 0) std::printf("all engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
