// epsilon-common — fit a small set of EQ bands to a target curve (dB over log frequency).
// Used for drawing a curve (EQ Sketch) and for matching a reference spectrum (EQ Match).
// Method: greedy placement at the largest remaining error, then Levenberg-Marquardt on all band
// parameters (log frequency, gain, log Q) against the dB error. Generic least-squares, MIT license.
#pragma once
#include "EqBand.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace epsilon {
namespace eq {

struct FitBand { int shape; float freq, gainDb, q; };

struct FitOptions {
    int maxBands = 8;
    float tolDb = 0.5f;          // stop when the worst error is below this
    bool shelves = true;         // allow low/high shelves at the ends
    float minHz = 20.f, maxHz = 20000.f, maxGainDb = 30.f, minQ = 0.1f, maxQ = 12.f;
    float fs = 48000.f;
};

class CurveFitter {
public:
    // hz: increasing frequencies (log spacing recommended); target: wanted dB at each
    std::vector<FitBand> fit(const std::vector<float>& hz, const std::vector<float>& target, const FitOptions& o) {
        f = hz; t = target; opt = o; bands.clear();
        const int n = (int)f.size();
        if (n < 4) return bands;
        while ((int)bands.size() < opt.maxBands) {
            std::vector<float> m = model(bands);
            int worst = 0; float we = 0;
            for (int i = 0; i < n; ++i) { const float e = t[i] - m[i]; if (std::fabs(e) > std::fabs(we)) { we = e; worst = i; } }
            if (std::fabs(we) < opt.tolDb) break;
            bands.push_back(guess(m, worst));
            refine(30);
        }
        refine(60);
        std::vector<FitBand> out;
        for (const FitBand& b : bands) if (std::fabs(b.gainDb) >= 0.25f) out.push_back(b);
        return out;
    }
    float rmsError(const std::vector<FitBand>& bs) const {
        std::vector<float> m = model(bs); double s = 0;
        for (size_t i = 0; i < f.size(); ++i) s += (t[i] - m[i]) * (t[i] - m[i]);
        return (float)std::sqrt(s / std::max<size_t>(1, f.size()));
    }

private:
    float bandDb(const FitBand& b, float hz) const {
        Spec s; s.shape = b.shape; s.freq = b.freq; s.gainDb = b.gainDb; s.q = b.q; s.slopeDb = 12;
        Design d; design(s, opt.fs, d);
        return 20.f * std::log10(std::max(1e-9f, std::abs(response(d, hz))));
    }
    std::vector<float> model(const std::vector<FitBand>& bs) const {
        std::vector<float> m(f.size(), 0.f);
        for (const FitBand& b : bs) for (size_t i = 0; i < f.size(); ++i) m[i] += bandDb(b, f[i]);
        return m;
    }
    FitBand guess(const std::vector<float>& m, int at) const {
        const int n = (int)f.size();
        const float e = t[at] - m[at];
        // half-height width of the error peak
        int lo = at, hi = at;
        while (lo > 0 && (t[lo - 1] - m[lo - 1]) * e > 0.5f * e * e) --lo;
        while (hi < n - 1 && (t[hi + 1] - m[hi + 1]) * e > 0.5f * e * e) ++hi;
        if (opt.shelves && (lo == 0 || hi == n - 1) && hi - lo < n - 1) {
            const bool low = lo == 0;
            const float edge = low ? f[hi] : f[lo];
            return { low ? kLowShelf : kHighShelf, clampF(edge, opt.minHz, opt.maxHz), clampF(e, -opt.maxGainDb, opt.maxGainDb), 0.7071f };
        }
        const float bw = std::max(0.1f, std::log2(f[hi] / f[lo]) + 1e-3f);
        const float p = std::exp2(bw);
        const float q = std::sqrt(p) / (p - 1.f);
        return { kBell, f[at], clampF(e, -opt.maxGainDb, opt.maxGainDb), clampF(q, opt.minQ, opt.maxQ) };
    }
    static float clampF(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

    // parameters: per band (log2 freq, gain, log2 q)
    void toVec(std::vector<double>& v) const { v.clear(); for (const FitBand& b : bands) { v.push_back(std::log2(b.freq)); v.push_back(b.gainDb); v.push_back(std::log2(b.q)); } }
    void fromVec(const std::vector<double>& v) {
        for (size_t k = 0; k < bands.size(); ++k) {
            bands[k].freq = clampF((float)std::exp2(v[3 * k]), opt.minHz, opt.maxHz);
            bands[k].gainDb = clampF((float)v[3 * k + 1], -opt.maxGainDb, opt.maxGainDb);
            bands[k].q = clampF((float)std::exp2(v[3 * k + 2]), opt.minQ, opt.maxQ);
        }
    }
    double cost(const std::vector<FitBand>& bs) const {
        std::vector<float> m = model(bs); double s = 0;
        for (size_t i = 0; i < f.size(); ++i) s += (t[i] - m[i]) * (t[i] - m[i]);
        return s;
    }
    void refine(int iters) {
        const int n = (int)f.size(), P = 3 * (int)bands.size();
        if (P == 0) return;
        double lambda = 1e-2;
        std::vector<double> v; toVec(v);
        double c = cost(bands);
        std::vector<double> J((size_t)n * P), r(n), A((size_t)P * P), g(P), dv(P);
        for (int it = 0; it < iters; ++it) {
            std::vector<float> m = model(bands);
            for (int i = 0; i < n; ++i) r[i] = t[i] - m[i];
            // numerical Jacobian, band by band (other bands don't change a band's own dB curve)
            for (int k = 0; k < (int)bands.size(); ++k) for (int j = 0; j < 3; ++j) {
                FitBand b = bands[k];
                const double h = j == 1 ? 0.05 : 0.01;
                float* field = j == 0 ? &b.freq : (j == 1 ? &b.gainDb : &b.q);
                const float orig = *field;
                if (j == 1) *field = orig + (float)h; else *field = (float)(orig * std::exp2(h));
                for (int i = 0; i < n; ++i) J[(size_t)i * P + 3 * k + j] = (bandDb(b, f[i]) - bandDb(bands[k], f[i])) / h;
            }
            for (int a = 0; a < P; ++a) {
                g[a] = 0; for (int i = 0; i < n; ++i) g[a] += J[(size_t)i * P + a] * r[i];
                for (int b2 = 0; b2 <= a; ++b2) { double s = 0; for (int i = 0; i < n; ++i) s += J[(size_t)i * P + a] * J[(size_t)i * P + b2]; A[(size_t)a * P + b2] = A[(size_t)b2 * P + a] = s; }
            }
            bool improved = false;
            for (int tries = 0; tries < 6 && !improved; ++tries) {
                std::vector<double> M(A);
                for (int a = 0; a < P; ++a) M[(size_t)a * P + a] *= 1.0 + lambda, M[(size_t)a * P + a] += 1e-9;
                if (!solve(M, g, dv, P)) { lambda *= 10; continue; }
                std::vector<double> nv(v); for (int a = 0; a < P; ++a) nv[a] += dv[a];
                std::vector<FitBand> keep = bands; fromVec(nv);
                const double nc = cost(bands);
                if (nc < c) { c = nc; toVec(v); lambda = std::max(1e-6, lambda * 0.3); improved = true; }
                else { bands = keep; lambda *= 8; }
            }
            if (!improved) break;
        }
    }
    static bool solve(std::vector<double>& M, std::vector<double> b, std::vector<double>& x, int P) {
        for (int c = 0; c < P; ++c) {
            int piv = c; for (int r = c + 1; r < P; ++r) if (std::fabs(M[(size_t)r * P + c]) > std::fabs(M[(size_t)piv * P + c])) piv = r;
            if (std::fabs(M[(size_t)piv * P + c]) < 1e-12) return false;
            if (piv != c) { for (int k = 0; k < P; ++k) std::swap(M[(size_t)c * P + k], M[(size_t)piv * P + k]); std::swap(b[c], b[piv]); }
            for (int r = c + 1; r < P; ++r) {
                const double fct = M[(size_t)r * P + c] / M[(size_t)c * P + c];
                for (int k = c; k < P; ++k) M[(size_t)r * P + k] -= fct * M[(size_t)c * P + k];
                b[r] -= fct * b[c];
            }
        }
        x.assign(P, 0);
        for (int r = P - 1; r >= 0; --r) { double s = b[r]; for (int k = r + 1; k < P; ++k) s -= M[(size_t)r * P + k] * x[k]; x[r] = s / M[(size_t)r * P + r]; }
        return true;
    }

    std::vector<float> f, t;
    FitOptions opt;
    std::vector<FitBand> bands;
};

} // namespace eq
} // namespace epsilon
