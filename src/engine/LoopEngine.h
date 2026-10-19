#pragma once
// Break loop playback. Plain C++17, no JUCE: tests/engine_tests.cpp builds it
// with bare g++.
#include <algorithm>
#include <cmath>
#include <functional>

namespace jm {

// The tempo every break in breaks/ is tuned to.
constexpr double kLoopBpm = 162.0;

// Bit-for-bit comparison, for change detection (spelled out to keep
// -Wfloat-equal quiet where exactness is the point).
inline bool exactlyEqual(float a, float b) { return std::equal_to<float>()(a, b); }

// One break: stereo samples spanning a whole number of 4/4 bars.
struct Loop {
    const float* left  = nullptr;
    const float* right = nullptr;
    int frames = 0;
    int bars   = 1;
    double beats() const { return 4.0 * bars; }
};

// A loop in the mix and its share of the level (the shares add up to 1).
struct MixVoice { int loop; float weight; };

// Plays a mix of loops, each phase-locked to the host beat. A loop's read
// position is a pure function of the beat (never a sample counter), so tempo
// changes and transport jumps cannot make it drift. Loops are varispeed: a
// loop always spans its bars in host beats, so at another tempo it plays
// faster or slower, pitch included.
//
// Timeline: when the transport starts, loop 1.1.1 sits on beat 0 of the song
// (a 2-bar loop starts over on bars 1, 3, 5 ...). restart() moves 1.1.1 to the
// current beat for every loop in the mix. Every change of what sounds (the
// mix, a restart, a jump, start/stop) ramps over a few milliseconds, so
// nothing clicks.
//
// Per block: setMix(), beginBlock() with the host transport, then render() in
// segments, calling restart() between segments where a note-on lands.
class LoopEngine {
public:
    static constexpr int kMaxMix  = 8;   // loops sounding at once
    static constexpr int kMaxTaps = 24;  // the mix plus the tails fading out

    void prepare(double sampleRate, double fadeSeconds = 0.005)
    {
        sr_          = sampleRate;
        fadeSamples_ = (float) std::max(1.0, fadeSeconds * sampleRate);
        numTaps_     = 0;
        playing_     = false;
    }

    void setLoops(const Loop* loops, int count)
    {
        loops_    = loops;
        count_    = count;
        numTaps_  = 0;
        mixCount_ = 0;
    }

    // Which loops sound and at what share. Changes ramp to the new mix at the
    // same position in the bar.
    void setMix(const MixVoice* v, int n)
    {
        MixVoice next[kMaxMix];
        int count = 0;
        for (int i = 0; i < n && count < kMaxMix; ++i)
            if (v[i].loop >= 0 && v[i].loop < count_ && v[i].weight > 0.0f) next[count++] = v[i];
        bool same = count == mixCount_;
        for (int i = 0; same && i < count; ++i)
            same = next[i].loop == mix_[i].loop && exactlyEqual(next[i].weight, mix_[i].weight);
        if (same) return;
        std::copy(next, next + count, mix_);
        mixCount_ = count;
        if (playing_) applyMix();
    }
    void select(int index) { const MixVoice v { index, 1.0f }; setMix(&v, 1); }

    // Host transport at the first sample of the block.
    void beginBlock(bool playing, double beat, double bpm)
    {
        bpm_ = bpm > 0.0 ? bpm : kLoopBpm;
        if (! playing)
        {
            if (playing_)
                for (int t = 0; t < numTaps_; ++t) retire(taps_[t]);
            playing_ = false;
            return; // beat_ runs on so the fade-out tail stays continuous
        }
        const bool started = ! playing_;
        const bool jumped  = playing_ && std::abs(beat - beat_) > tolerance();
        if (started || jumped)
        {
            // Whatever is sounding carries on along its old timeline and fades
            // out - unless the jump lands on the same spot in its loop (a host
            // loop region a whole number of loops long), which needs nothing.
            const double d = beat_ - beat;
            for (int t = 0; t < numTaps_; ++t)
            {
                Tap& tp = taps_[t];
                if (jumped && tp.live && wholeLoops(d, loops_[tp.loop].beats())) continue;
                retire(tp);
                tp.shift += d;
            }
            beat_    = beat;
            playing_ = true;
            if (started) anchor_ = 0.0;
            applyMix();
        }
        beat_ = beat;
    }

    // Note-on: every loop in the mix starts again from 1.1.1 at the current
    // beat. Ignored while the transport is stopped (starting it syncs to the
    // song position).
    void restart()
    {
        if (! playing_) return;
        const double shift = -beat_;
        for (int t = 0; t < numTaps_; ++t)
        {
            Tap& tp = taps_[t];
            if (! tp.live) continue;
            if (wholeLoops(tp.shift - shift, loops_[tp.loop].beats())) tp.shift = shift; // already at 1.1.1
            else retire(tp);
        }
        anchor_ = beat_;
        applyMix(); // re-adds the retired loops at their first sample, which the crop already fades in
    }

    // Adds n samples into L and R.
    void render(float* L, float* R, int n)
    {
        const double dBeat = bpm_ / 60.0 / sr_;
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            for (int t = 0; t < numTaps_; ++t)
            {
                Tap& tp = taps_[t];
                if (tp.gain < tp.target)      tp.gain = std::min(tp.target, tp.gain + tp.step);
                else if (tp.gain > tp.target) tp.gain = std::max(tp.target, tp.gain - tp.step);
                if (tp.gain <= 0.0f) continue;
                const Loop& lp = loops_[tp.loop];
                double ph = (beat_ + tp.shift) / lp.beats();
                ph -= std::floor(ph);
                const double x  = ph * lp.frames;
                const int    i0 = std::min(lp.frames - 1, (int) x);
                const float  f  = (float) (x - i0);
                l += tp.gain * cubic(lp.left,  lp.frames, i0, f);
                r += tp.gain * cubic(lp.right, lp.frames, i0, f);
            }
            L[i] += l;
            R[i] += r;
            beat_ += dBeat;
        }
        int keep = 0; // drop tails that have faded out
        for (int t = 0; t < numTaps_; ++t)
            if (taps_[t].live || taps_[t].gain > 0.0f) taps_[keep++] = taps_[t];
        numTaps_ = keep;
    }

    // For the UI: the loops in the mix, their current level and position 0..1.
    // Returns how many were written (0 when silent).
    int sounding(int* loop, float* gain, double* phase, int max) const
    {
        int n = 0;
        for (int t = 0; t < numTaps_ && n < max; ++t)
        {
            const Tap& tp = taps_[t];
            if (! tp.live) continue;
            const double ph = (beat_ + tp.shift) / loops_[tp.loop].beats();
            loop[n] = tp.loop;
            gain[n] = tp.gain;
            phase[n] = ph - std::floor(ph);
            ++n;
        }
        return n;
    }
    bool playing() const { return playing_; }

private:
    // One reading of a loop: position in beats = beat_ + shift. Live taps carry
    // the mix; retired ones fade out along their own timeline.
    struct Tap { int loop; double shift; float gain, target, step; bool live; };

    // Every change of level is a linear ramp lasting one fade time.
    void setTarget(Tap& tp, float target)
    {
        if (exactlyEqual(target, tp.target)) return;
        tp.target = target;
        tp.step   = std::abs(target - tp.gain) / fadeSamples_;
    }
    void retire(Tap& tp) { tp.live = false; setTarget(tp, 0.0f); }

    // Match live taps to the mix: keep and re-level the ones still in it,
    // retire the rest, start the loops that have no tap yet.
    void applyMix()
    {
        bool used[kMaxMix] = {};
        for (int t = 0; t < numTaps_; ++t)
        {
            Tap& tp = taps_[t];
            if (! tp.live) continue;
            int m = 0;
            while (m < mixCount_ && (used[m] || mix_[m].loop != tp.loop)) ++m;
            if (m < mixCount_) { used[m] = true; setTarget(tp, mix_[m].weight); }
            else retire(tp);
        }
        for (int m = 0; m < mixCount_; ++m)
            if (! used[m]) addLive(mix_[m].loop, mix_[m].weight, ! atLoopTop(mix_[m].loop));
    }

    void addLive(int loop, float weight, bool fadeIn)
    {
        if (numTaps_ == kMaxTaps) // make room: drop the quietest tail
        {
            int q = -1;
            for (int t = 0; t < numTaps_; ++t)
                if (! taps_[t].live && (q < 0 || taps_[t].gain < taps_[q].gain)) q = t;
            if (q < 0) return;
            taps_[q] = taps_[--numTaps_];
        }
        Tap tp { loop, -anchor_, fadeIn ? 0.0f : weight, 0.0f, 0.0f, true };
        setTarget(tp, weight);
        taps_[numTaps_++] = tp;
    }

    // Half a millisecond, in beats: below this two positions count as the same.
    double tolerance() const { return 0.0005 * bpm_ / 60.0; }

    // Is a position offset of d beats a whole number of loops?
    bool wholeLoops(double d, double beats) const
    {
        const double r = std::fmod(std::abs(d), beats);
        return r < tolerance() || beats - r < tolerance();
    }

    // Is the loop within its first millisecond? Its crop starts from silence
    // there, so it can come in at full level.
    bool atLoopTop(int loop) const
    {
        const double beats = loops_[loop].beats();
        double pos = std::fmod(beat_ - anchor_, beats);
        if (pos < 0.0) pos += beats;
        return pos * 60.0 / bpm_ < 0.001;
    }

    // Catmull-Rom between i and i+1; indices wrap because the loop is cyclic.
    static float cubic(const float* d, int n, int i, float f)
    {
        auto at = [d, n](int k) { k %= n; if (k < 0) k += n; return d[k]; };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    const Loop* loops_ = nullptr;
    int      count_       = 0;
    MixVoice mix_[kMaxMix] {};
    int      mixCount_    = 0;
    double   sr_          = 48000.0;
    double   bpm_         = kLoopBpm;
    double   beat_        = 0.0;     // host beat at the next sample to render
    double   anchor_      = 0.0;     // beat where the loops' 1.1.1 sits
    bool     playing_     = false;
    float    fadeSamples_ = 240.0f;
    Tap      taps_[kMaxTaps] {};
    int      numTaps_     = 0;
};

} // namespace jm
