#pragma once
// Integrated loudness by ITU-R BS.1770-4 / EBU R128: K-weighted, gated, in
// LUFS. Plain C++17, no JUCE.
#include <algorithm>
#include <cmath>
#include <vector>

namespace jm {

// Every break plays at this loudness, so moving across the pad changes the
// beat, not the level.
constexpr double kBreakLufs = -15.0;

struct Biquad {
    double b0, b1, b2, a1, a2, z1 = 0.0, z2 = 0.0;
    double operator()(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The K-weighting filter, a head shelf then a high-pass, at any sample rate.
// These analog prototypes reproduce BS.1770's 48 kHz coefficients.
inline void kWeighting(double rate, Biquad& shelf, Biquad& highpass)
{
    const double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double k = std::tan(pi * f0 / rate), vh = std::pow(10.0, gainDb / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416), a0 = 1.0 + k / q + k * k;
        shelf = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                  2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = std::tan(pi * f0 / rate), a0 = 1.0 + k / q + k * k;
        highpass = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
    }
}

// The integrated loudness of a stereo loop as it plays round and round: the
// filters start already settled on the loop's end, and the 400 ms gating
// blocks (one every 100 ms) wrap from the end into the start, so a loop is
// measured the way it is heard. -inf for silence.
inline double loopLoudness(const float* left, const float* right, int frames, double rate)
{
    if (frames <= 0 || rate <= 0.0) return -HUGE_VAL;
    Biquad shelf, highpass;
    kWeighting(rate, shelf, highpass);
    Biquad f[2][2] = { { shelf, highpass }, { shelf, highpass } };
    auto power = [&](int i)
    {
        const double l = f[0][1](f[0][0](left[i])), r = f[1][1](f[1][0](right[i]));
        return l * l + r * r; // both channels weigh 1
    };
    const int settle = std::min(frames, (int) (0.5 * rate));
    for (int i = frames - settle; i < frames; ++i) power(i);
    std::vector<double> sum((size_t) frames + 1, 0.0); // running sum of the weighted power
    for (int i = 0; i < frames; ++i) sum[(size_t) i + 1] = sum[(size_t) i] + power(i);

    const int block = (int) std::lround(0.4 * rate), hop = (int) std::lround(0.01 * rate);
    auto span = [&](int from, int n) // power summed over n frames from `from`, wrapping
    {
        double s = sum[(size_t) frames] * (double) (n / frames);
        n %= frames;
        const int to = from + n;
        return to <= frames ? s + sum[(size_t) to] - sum[(size_t) from]
                            : s + sum[(size_t) frames] - sum[(size_t) from] + sum[(size_t) (to - frames)];
    };
    std::vector<double> z; // each block's mean power
    for (int start = 0; start < frames; start += hop) z.push_back(span(start, block) / block);
    auto lufs = [](double meanPower) { return -0.691 + 10.0 * std::log10(meanPower); };

    auto gatedMean = [&](double gate)
    {
        double s = 0.0;
        int n = 0;
        for (double p : z)
            if (p > 0.0 && lufs(p) > gate) { s += p; ++n; }
        return n > 0 ? s / n : 0.0;
    };
    const double absolute = gatedMean(-70.0);
    if (absolute <= 0.0) return -HUGE_VAL;
    const double relative = gatedMean(std::max(-70.0, lufs(absolute) - 10.0));
    return relative > 0.0 ? lufs(relative) : -HUGE_VAL;
}

// The gain that brings a loop measured at `lufs` to `target`; silence stays as it is.
inline float gainFor(double lufs, double target = kBreakLufs)
{
    return std::isfinite(lufs) ? (float) std::pow(10.0, (target - lufs) / 20.0) : 1.0f;
}

} // namespace jm
