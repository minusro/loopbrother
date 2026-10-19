#pragma once
// Break loop playback. Plain C++17, no JUCE: tests/engine_tests.cpp builds it
// with bare g++.
#include <algorithm>
#include <cmath>

namespace jm {

// The tempo every break in breaks/ is tuned to.
constexpr double kLoopBpm = 162.0;

// One break: stereo samples spanning a whole number of 4/4 bars.
struct Loop {
    const float* left  = nullptr;
    const float* right = nullptr;
    int frames = 0;
    int bars   = 1;
    double beats() const { return 4.0 * bars; }
};

// Plays one loop at a time, phase-locked to the host beat. The read position
// is a pure function of the beat (never a sample counter), so tempo changes and
// transport jumps cannot make it drift. Loops are varispeed: a loop always
// spans its bars in host beats, so at another tempo it plays faster or slower,
// pitch included.
//
// Timeline: when the transport starts, loop 1.1.1 sits on beat 0 of the song
// (a 2-bar loop starts over on bars 1, 3, 5 ...). restart() moves 1.1.1 to the
// current beat. Every change of what sounds (switching loops, restarting, a
// jump, start/stop) crossfades over a few milliseconds, so nothing clicks.
//
// Per block: beginBlock() with the host transport, select(), then render() in
// segments, calling restart() between segments where a note-on lands.
class LoopEngine {
public:
    static constexpr int kMaxTaps = 4;

    void prepare(double sampleRate, double fadeSeconds = 0.005)
    {
        sr_       = sampleRate;
        fadeStep_ = (float) (1.0 / std::max(1.0, fadeSeconds * sampleRate));
        numTaps_  = 0;
        playing_  = false;
    }

    void setLoops(const Loop* loops, int count)
    {
        loops_    = loops;
        count_    = count;
        selected_ = std::min(selected_, std::max(0, count - 1));
        numTaps_  = 0;
    }

    // Host transport at the first sample of the block.
    void beginBlock(bool playing, double beat, double bpm)
    {
        bpm_ = bpm > 0.0 ? bpm : kLoopBpm;
        if (! playing)
        {
            if (playing_)
                for (int t = 0; t < numTaps_; ++t) taps_[t].target = 0.0f;
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
            bool liveContinues = false;
            for (int t = 0; t < numTaps_; ++t)
            {
                Tap& tp = taps_[t];
                if (jumped && wholeLoops(d, loops_[tp.loop].beats()))
                {
                    liveContinues |= tp.target > 0.0f;
                    continue;
                }
                tp.shift += d;
                tp.target = 0.0f;
            }
            beat_    = beat;
            playing_ = true;
            if (started) anchor_ = 0.0;
            if (! liveContinues) addLive(! atLoopTop());
        }
        beat_ = beat;
    }

    // Which loop sounds. A change crossfades to the new loop at the same
    // position in the bar.
    void select(int index)
    {
        if (index < 0 || index >= count_ || index == selected_) return;
        selected_ = index;
        if (! playing_) return;
        for (int t = 0; t < numTaps_; ++t) taps_[t].target = 0.0f;
        addLive(! atLoopTop());
    }

    // Note-on: the loop starts again from 1.1.1 at the current beat. Ignored
    // while the transport is stopped (starting it syncs to the song position).
    void restart()
    {
        if (! playing_) return;
        const double shift = -beat_;
        for (int t = 0; t < numTaps_; ++t) // already at 1.1.1 (a note on the bar line)
        {
            Tap& tp = taps_[t];
            if (tp.target > 0.0f && tp.loop == selected_ && wholeLoops(tp.shift - shift, loops_[tp.loop].beats()))
            {
                anchor_  = beat_;
                tp.shift = shift;
                return;
            }
        }
        for (int t = 0; t < numTaps_; ++t) taps_[t].target = 0.0f;
        anchor_ = beat_;
        addLive(false); // first sample of a loop: the crop already fades it in
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
                if (tp.gain < tp.target)      tp.gain = std::min(tp.target, tp.gain + fadeStep_);
                else if (tp.gain > tp.target) tp.gain = std::max(tp.target, tp.gain - fadeStep_);
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
        int keep = 0; // drop taps that have faded out
        for (int t = 0; t < numTaps_; ++t)
            if (taps_[t].gain > 0.0f || taps_[t].target > 0.0f) taps_[keep++] = taps_[t];
        numTaps_ = keep;
    }

    // For the UI: the loop being heard (-1 when silent) and its position 0..1.
    int sounding() const
    {
        for (int t = numTaps_ - 1; t >= 0; --t)
            if (taps_[t].target > 0.0f) return taps_[t].loop;
        return -1;
    }
    double phase() const
    {
        for (int t = numTaps_ - 1; t >= 0; --t)
            if (taps_[t].target > 0.0f)
            {
                const double ph = (beat_ + taps_[t].shift) / loops_[taps_[t].loop].beats();
                return ph - std::floor(ph);
            }
        return 0.0;
    }
    int  selected() const { return selected_; }
    bool playing() const  { return playing_; }

private:
    // One reading of a loop: position in beats = beat_ + shift.
    struct Tap { int loop; double shift; float gain; float target; };

    void addLive(bool fadeIn)
    {
        if (count_ <= 0) return;
        if (numTaps_ == kMaxTaps) // make room: drop the quietest fading tap
        {
            int q = 0;
            for (int t = 1; t < numTaps_; ++t)
                if (taps_[t].gain < taps_[q].gain) q = t;
            taps_[q] = taps_[--numTaps_];
        }
        taps_[numTaps_++] = { selected_, -anchor_, fadeIn ? 0.0f : 1.0f, 1.0f };
    }

    // Half a millisecond, in beats: below this two positions count as the same.
    double tolerance() const { return 0.0005 * bpm_ / 60.0; }

    // Is a position offset of d beats a whole number of loops?
    bool wholeLoops(double d, double beats) const
    {
        double r = std::fmod(std::abs(d), beats);
        return r < tolerance() || beats - r < tolerance();
    }

    // Is the selected loop within its first millisecond? Its crop starts from
    // silence there, so it can come in at full level.
    bool atLoopTop() const
    {
        if (count_ <= 0) return true;
        const double beats = loops_[selected_].beats();
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
    int    count_    = 0;
    int    selected_ = 0;
    double sr_       = 48000.0;
    double bpm_      = kLoopBpm;
    double beat_     = 0.0;     // host beat at the next sample to render
    double anchor_   = 0.0;     // beat where the loop's 1.1.1 sits
    bool   playing_  = false;
    float  fadeStep_ = 1.0f / 240.0f;
    Tap    taps_[kMaxTaps] {};
    int    numTaps_  = 0;
};

} // namespace jm
