#pragma once
// Pad geometry: the Voronoi cells of the breaks and how much of the selection
// circle falls in each. Plain C++17, no JUCE.
#include <algorithm>
#include <cmath>
#include <vector>
#include "LoopEngine.h"

namespace jm {

// Radius of the selection circle at size 1, in pad units (the pad is
// [0, 1]^2): 12.4 px on the 480 px pad. The Circle Size parameter scales it
// from a quarter up to the circle that covers half the pad from its middle
// (radius sqrt(0.5 / pi), about 15.5 times), where it plays about 50 to 60
// breaks at once.
constexpr double kPuckRadius = 12.375 / 480.0;

constexpr double kHalfPadRadius = 0.3989422804;  // sqrt(0.5 / pi)
constexpr float  kCircleSizeMin = 0.25f, kCircleSizeMax = (float) (kHalfPadRadius / kPuckRadius);
inline double circleRadius(float size) { return kPuckRadius * size; }

struct Pt { double x, y; };

// The similarity layout (breaks/layout.json) spans [0, 1]^2; on the pad the
// breaks sit drawn in by kSiteInset on every side, so the marks of the
// outermost ones have room inside the edges. Their cells still reach the
// edges.
constexpr double kSiteInset = 0.06;
inline Pt padSite(Pt layout)
{
    return { kSiteInset + (1.0 - 2.0 * kSiteInset) * layout.x, kSiteInset + (1.0 - 2.0 * kSiteInset) * layout.y };
}
using Poly = std::vector<Pt>; // convex, counter-clockwise

// Keep the part of `poly` on a's side of the perpendicular bisector of a-b.
inline Poly clipToNearer(const Poly& poly, Pt a, Pt b)
{
    const Pt n { b.x - a.x, b.y - a.y };
    const double c = (n.x * (a.x + b.x) + n.y * (a.y + b.y)) * 0.5; // inside: n.p <= c
    Poly out;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        const Pt p = poly[i], q = poly[(i + 1) % poly.size()];
        const double dp = n.x * p.x + n.y * p.y - c, dq = n.x * q.x + n.y * q.y - c;
        if (dp <= 0.0) out.push_back(p);
        if ((dp < 0.0 && dq > 0.0) || (dp > 0.0 && dq < 0.0))
        {
            const double t = dp / (dp - dq);
            out.push_back({ p.x + t * (q.x - p.x), p.y + t * (q.y - p.y) });
        }
    }
    return out;
}

// Each site's Voronoi cell, clipped to the box [lo, hi]^2.
inline std::vector<Poly> voronoiCells(const std::vector<Pt>& sites, double lo, double hi)
{
    std::vector<Poly> cells;
    for (size_t i = 0; i < sites.size(); ++i)
    {
        Poly cell { { lo, lo }, { hi, lo }, { hi, hi }, { lo, hi } };
        for (size_t j = 0; j < sites.size() && ! cell.empty(); ++j)
            if (j != i) cell = clipToNearer(cell, sites[i], sites[j]);
        cells.push_back(std::move(cell));
    }
    return cells;
}

// Signed area of the circle (origin, radius r) intersected with triangle
// (origin, a, b): walk a->b, splitting where it crosses the circle; chords
// inside count as triangles, arcs outside as circular sectors.
inline double circleTriangleArea(Pt a, Pt b, double r)
{
    const double r2 = r * r;
    const Pt d { b.x - a.x, b.y - a.y };
    const double A = d.x * d.x + d.y * d.y;
    if (A <= 0.0) return 0.0;
    const double B = a.x * d.x + a.y * d.y, C = a.x * a.x + a.y * a.y - r2;
    Pt pts[4] = { a };
    int n = 1;
    const double disc = B * B - A * C;
    if (disc > 0.0)
    {
        const double s = std::sqrt(disc);
        for (const double t : { (-B - s) / A, (-B + s) / A })
            if (t > 0.0 && t < 1.0) pts[n++] = { a.x + t * d.x, a.y + t * d.y };
    }
    pts[n++] = b;
    double area = 0.0;
    for (int i = 0; i + 1 < n; ++i)
    {
        const Pt p = pts[i], q = pts[i + 1];
        const Pt m { (p.x + q.x) * 0.5, (p.y + q.y) * 0.5 };
        const double cross = p.x * q.y - p.y * q.x;
        if (m.x * m.x + m.y * m.y <= r2) area += 0.5 * cross;
        else area += 0.5 * r2 * std::atan2(cross, p.x * q.x + p.y * q.y);
    }
    return area;
}

// Area of the circle (centre c, radius r) inside a convex polygon.
inline double circlePolygonArea(Pt c, double r, const Poly& poly)
{
    double area = 0.0;
    for (size_t i = 0; i < poly.size(); ++i)
    {
        const Pt p = poly[i], q = poly[(i + 1) % poly.size()];
        area += circleTriangleArea({ p.x - c.x, p.y - c.y }, { q.x - c.x, q.y - c.y }, r);
    }
    return std::abs(area);
}

// Equal-power gains from area shares: gain = sqrt(share), so the squared
// gains add up to 1 and a half-and-half blend of two unrelated breaks is as
// loud as either one alone.
inline void equalPower(MixVoice* v, int n)
{
    for (int i = 0; i < n; ++i) v[i].weight = std::sqrt(v[i].weight);
}

// The pad: break positions and their cells. The cells used for mixing extend
// past the pad so a circle hanging over its edge is still fully assigned.
class PadMap {
public:
    void setSites(const std::vector<Pt>& sites)
    {
        sites_ = sites;
        cells_ = voronoiCells(sites, -1.0, 2.0);
        reach_.assign(sites.size(), 0.0);
        for (size_t i = 0; i < sites.size(); ++i)
            for (const Pt& v : cells_[i])
                reach_[i] = std::max(reach_[i], std::hypot(v.x - sites[i].x, v.y - sites[i].y));
    }

    int size() const { return (int) sites_.size(); }

    // The breaks the circle at c overlaps, each weighted by the share of the
    // circle's area in its cell: the weights add up to 1. If more than `max`
    // cells overlap, the largest shares are kept and renormalised. No
    // allocation: safe on the audio thread.
    int mix(Pt c, double r, MixVoice* out, int max) const
    {
        int n = 0;
        double total = 0.0;
        for (int i = 0; i < size(); ++i)
        {
            const Pt s = sites_[(size_t) i];
            if (std::hypot(s.x - c.x, s.y - c.y) > r + reach_[(size_t) i]) continue;
            const double a = circlePolygonArea(c, r, cells_[(size_t) i]);
            if (a <= 1e-12) continue;
            if (n < max) out[n++] = { i, (float) a };
            else
            {
                int q = 0; // replace the smallest share
                for (int k = 1; k < n; ++k) if (out[k].weight < out[q].weight) q = k;
                if (a > out[q].weight) out[q] = { i, (float) a };
            }
        }
        if (n == 0) return 0;
        int big = 0;
        for (int k = 0; k < n; ++k) { total += out[k].weight; if (out[k].weight > out[big].weight) big = k; }
        float rest = 0.0f;
        for (int k = 0; k < n; ++k)
            if (k != big) { out[k].weight = (float) (out[k].weight / total); rest += out[k].weight; }
        out[big].weight = 1.0f - rest; // the shares add up to exactly 1
        return n;
    }

private:
    std::vector<Pt>   sites_;
    std::vector<Poly> cells_;
    std::vector<double> reach_; // farthest cell corner from its site
};

} // namespace jm
