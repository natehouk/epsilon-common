// epsilon-common — EQ band engine v2.
//
// One band = a list of analog sections (first or second order), designed from public, generic techniques:
// Butterworth pole placement, Butterworth-style higher-order shelving (zeros and poles on two circles),
// RBJ-style bell, notch and all-pass prototypes, and a tilt built from octave-spaced first-order shelves.
// The same section list gives:
//   - the exact analog response (display, linear-phase and natural-phase FIR design): response()
//   - a zero-latency digital filter: Cascade, trapezoidal (TPT) state-variable filters, pre-warped per section.
// Each second-order section is   (b2 s^2 + b1 s + b0) / (s^2 + k s + 1),   first-order (b1 s + b0) / (s + 1),
// with s normalised to the section frequency fc. The band's overall gain is folded into the first section.
// License: MIT.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

namespace epsilon {
namespace eq {

typedef std::complex<float> cpx;
static constexpr float kPi = 3.14159265358979f;

// shape numbers are stable (stored in sessions): append only
enum Shape { kBell, kLowShelf, kHighShelf, kLowCut, kHighCut, kNotch, kBandPass, kTiltShelf, kFlatTilt, kAllPass, kNumShapes };
static constexpr int kBrickwall = 0;              // slopeDb value meaning "brickwall" (cuts only)
static constexpr int kBrickOrder = 32;            // a brickwall is a 32nd-order Butterworth (192 dB/oct)
static constexpr int kMaxSlopeDb = 96;
static constexpr int kMaxSections = 32;

inline const char* shapeName(int s) {
    static const char* const n[kNumShapes] = { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };
    return s >= 0 && s < kNumShapes ? n[s] : "";
}
inline bool hasGain(int s) { return s == kBell || s == kLowShelf || s == kHighShelf || s == kTiltShelf || s == kFlatTilt; }
inline bool hasSlope(int s) { return s == kLowShelf || s == kHighShelf || s == kLowCut || s == kHighCut || s == kBandPass || s == kTiltShelf || s == kAllPass; }
inline bool hasQ(int s) { return s != kFlatTilt; }
inline bool isCut(int s) { return s == kLowCut || s == kHighCut; }

struct Spec {
    int shape = kBell;
    float freq = 1000.f;      // Hz
    float gainDb = 0.f;       // bell / shelves / tilt
    float q = 0.70710678f;
    int slopeDb = 12;         // 6..96 in steps of 6, or kBrickwall (cuts)
};

struct Section {
    int order;                // 1 or 2
    float fc;                 // Hz
    float k;                  // damping (second order)
    float b2, b1, b0;
};

struct Design {
    int n = 0;
    Section s[kMaxSections];
    void add(int order, float fc, float k, float b2, float b1, float b0) { if (n < kMaxSections) s[n++] = { order, fc, k, b2, b1, b0 }; }
    void scale(float g) { if (n > 0) { s[0].b2 *= g; s[0].b1 *= g; s[0].b0 *= g; } }
};

// Butterworth damping k for the second-order sections of an order-m filter, most damped first
inline float butterK(int m, int i) { return 2.f * std::sin((2.f * (float)(m / 2 - i) - 1.f) * kPi / (2.f * (float)m)); }

inline cpx sectionResponse(const Section& x, float hz) {
    const cpx s(0.f, hz / x.fc);
    if (x.order == 1) return (x.b1 * s + x.b0) / (s + 1.f);
    return (x.b2 * s * s + x.b1 * s + x.b0) / (s * s + x.k * s + 1.f);
}
inline cpx response(const Design& d, float hz) {
    cpx h(1.f, 0.f);
    for (int i = 0; i < d.n; ++i) h *= sectionResponse(d.s[i], hz);
    return h;
}

inline int orderOf(int slopeDb) { return slopeDb == kBrickwall ? kBrickOrder : std::max(1, std::min(kMaxSlopeDb, slopeDb) / 6); }

// Butterworth cut of order m (hp = low cut); Q shapes the most resonant section (0.707 = flat)
inline void butterCut(Design& d, bool hp, int m, float fc, float q) {
    const int pairs = m / 2;
    for (int i = 0; i < pairs; ++i) {
        float k = butterK(m, i);
        if (i == pairs - 1) k *= 0.70710678f / q;
        if (hp) d.add(2, fc, k, 1.f, 0.f, 0.f); else d.add(2, fc, k, 0.f, 0.f, 1.f);
    }
    if (m & 1) { if (hp) d.add(1, fc, 0, 0, 1.f, 0.f); else d.add(1, fc, 0, 0, 0.f, 1.f); }
}

// order-m low shelf, linear gain g below fc, 1 above; half the gain (in dB) at fc
inline void lowShelf(Design& d, int m, float fc, float g, float q) {
    const float gm = std::pow(g, 1.f / m), f = fc / std::pow(g, 1.f / (2.f * m));
    const int pairs = m / 2;
    for (int i = 0; i < pairs; ++i) {
        float k = butterK(m, i);
        if (i == pairs - 1) k *= 0.70710678f / q;
        d.add(2, f, k, 1.f, k * gm, gm * gm);
    }
    if (m & 1) d.add(1, f, 0, 0, 1.f, gm);
}

inline void design(const Spec& sp, float fs, Design& d) {
    d.n = 0;
    const float fc = std::min(std::max(sp.freq, 1.f), fs);   // the analog design may sit above Nyquist; Cascade clamps
    const float q = std::min(std::max(sp.q, 0.025f), 40.f);
    const float A = std::pow(10.f, sp.gainDb / 40.f);
    switch (sp.shape) {
    case kBell: d.add(2, fc, 1.f / (q * A), 1.f, A / q, 1.f); break;
    case kNotch: d.add(2, fc, 1.f / q, 1.f, 0.f, 1.f); break;
    case kLowShelf: lowShelf(d, orderOf(std::max(6, sp.slopeDb)), fc, A * A, q); break;
    case kHighShelf: case kTiltShelf: {
        const float g = A * A;            // high shelf = g * lowShelf(1/g)
        lowShelf(d, orderOf(std::max(6, sp.slopeDb)), fc, 1.f / g, q);
        d.scale(sp.shape == kTiltShelf ? std::sqrt(g) : g);   // tilt: -gain/2 below, +gain/2 above
        break;
    }
    case kLowCut: case kHighCut: butterCut(d, sp.shape == kLowCut, orderOf(sp.slopeDb), fc, q); break;
    case kBandPass: {
        const int m = orderOf(std::max(6, sp.slopeDb));
        if (m == 1) { const float k = 1.f / q; d.add(2, fc, k, 0.f, k, 0.f); break; }
        const float bw = 2.f * std::asinh(1.f / (2.f * q)) / std::log(2.f);      // octaves between the edges
        const float lo = fc * std::exp2(-0.5f * bw), hi = fc * std::exp2(0.5f * bw);
        butterCut(d, true, m, lo, 0.70710678f); butterCut(d, false, m, hi, 0.70710678f);
        const float at = std::abs(response(d, fc));
        if (at > 1e-12f) d.scale(1.f / at);                                        // 0 dB at the centre
        break;
    }
    case kAllPass: {
        const int m = orderOf(std::max(6, sp.slopeDb));
        const int pairs = m / 2;
        for (int i = 0; i < pairs; ++i) { float k = butterK(m, i); if (i == pairs - 1) k *= 0.70710678f / q; d.add(2, fc, k, 1.f, -k, 1.f); }
        if (m & 1) d.add(1, fc, 0, 0, -1.f, 1.f);
        break;
    }
    case kFlatTilt: {
        // gain = level change across the 10 octaves from 20 Hz to 20 kHz: gain/10 dB per octave, 0 dB at fc
        const float perOct = sp.gainDb / 10.f, g = std::pow(10.f, perOct / 20.f), sg = std::sqrt(g);
        for (int k = -16; k < 16; ++k) {
            const float fm = fc * std::exp2((float)k + 0.5f);
            if (fm < 1.f || fm > 0.45f * fs) continue;
            d.add(1, fm * sg, 0, 0, g, 1.f);           // first-order high shelf: +perOct dB above fm
        }
        const float at = std::abs(response(d, fc));
        if (at > 1e-12f) d.scale(1.f / at);
        break;
    }
    default: break;
    }
}

// ------------------------------------------------------------------ zero-latency digital cascade
// Trapezoidal state-variable filters (Zavalishin / Simper), one per second-order section, pre-warped at the
// section's own frequency; first-order sections are trapezoidal one-poles. Coefficients may change every
// sample block without clicks; states survive coefficient changes.
template <int kCh = 2>
struct Cascade {
    int n = 0;
    int order[kMaxSections];
    float a1[kMaxSections], a2[kMaxSections], a3[kMaxSections], m0[kMaxSections], m1[kMaxSections], m2[kMaxSections];
    float ic1[kCh][kMaxSections], ic2[kCh][kMaxSections];

    Cascade() { std::memset(this, 0, sizeof *this); }
    void reset() { std::memset(ic1, 0, sizeof ic1); std::memset(ic2, 0, sizeof ic2); }
    // Pre-warp: warpHz > 0 maps every section through one bilinear transform tuned at warpHz (the band's
    // frequency, clamped to 0.49 fs), which keeps the band's shape intact and matches Mica 1.x; warpHz = 0
    // tunes each section at its own frequency (clamped to 0.49 fs), which suits spread-out designs (flat tilt).
    static float warpG(const Section& s, float fs, float warpHz) {
        if (warpHz > 0.f) return std::tan(kPi * std::min(warpHz, 0.49f * fs) / fs) * (s.fc / warpHz);
        return std::tan(kPi * std::min(s.fc, 0.49f * fs) / fs);
    }
    void set(const Design& d, float fs, float warpHz = 0.f) {
        const int was = n; n = d.n;
        for (int i = 0; i < n; ++i) {
            const Section& s = d.s[i];
            const float g = warpG(s, fs, warpHz);
            order[i] = s.order;
            if (s.order == 2) {
                a1[i] = 1.f / (1.f + g * (g + s.k)); a2[i] = g * a1[i]; a3[i] = g * a2[i];
                m0[i] = s.b2; m1[i] = s.b1 - s.b2 * s.k; m2[i] = s.b0 - s.b2;
            } else {
                a1[i] = g / (1.f + g); m0[i] = s.b1; m1[i] = s.b0 - s.b1;    // y = b1 x + (b0 - b1) lp
            }
        }
        for (int i = was; i < n; ++i) for (int c = 0; c < kCh; ++c) ic1[c][i] = ic2[c][i] = 0.f;
    }
    inline float process(float x, int c) {
        for (int i = 0; i < n; ++i) {
            if (order[i] == 2) {
                const float v3 = x - ic2[c][i];
                const float v1 = a1[i] * ic1[c][i] + a2[i] * v3;
                const float v2 = ic2[c][i] + a2[i] * ic1[c][i] + a3[i] * v3;
                ic1[c][i] = 2.f * v1 - ic1[c][i]; ic2[c][i] = 2.f * v2 - ic2[c][i];
                x = m0[i] * x + m1[i] * v1 + m2[i] * v2;
            } else {
                const float v = (x - ic1[c][i]) * a1[i], lp = v + ic1[c][i];
                ic1[c][i] = lp + v;
                x = m0[i] * x + m1[i] * lp;
            }
        }
        return x;
    }
    bool finite() const {
        for (int c = 0; c < kCh; ++c) for (int i = 0; i < n; ++i) if (!(std::fabs(ic1[c][i]) < 1e6f) || !(std::fabs(ic2[c][i]) < 1e6f)) return false;
        return true;
    }
    // the digital response this cascade actually has (bilinear transform of each section)
    static cpx digitalResponse(const Design& d, float fs, float hz, float warpHz = 0.f) {
        cpx h(1.f, 0.f);
        const float w = 2.f * kPi * hz / fs;
        const cpx z1 = std::polar(1.f, -w);
        for (int i = 0; i < d.n; ++i) {
            const Section& s = d.s[i];
            const float g = warpG(s, fs, warpHz);
            const cpx sn = (1.f - z1) / ((1.f + z1) * g);                 // s / wc through the pre-warped bilinear transform
            if (s.order == 1) h *= (s.b1 * sn + s.b0) / (sn + 1.f);
            else h *= (s.b2 * sn * sn + s.b1 * sn + s.b0) / (sn * sn + s.k * sn + 1.f);
        }
        return h;
    }
};

} // namespace eq
} // namespace epsilon
