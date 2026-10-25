// EQ band engine v2 tests: legacy match with Mica 1.x, slopes, shelves, special shapes, digital cascade.
#include "../common/CurveFit.hpp"
#include "../common/EqBand.hpp"
#include <cstdio>
#include <vector>

using namespace epsilon::eq;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static float db(cpx h) { return 20.f * std::log10(std::max(1e-30f, std::abs(h))); }
static float dbAt(const Spec& s, float hz, float fs = 48000.f) { Design d; design(s, fs, d); return db(response(d, hz)); }
static Spec mk(int shape, float f, float g, float q, int slope) { Spec s; s.shape = shape; s.freq = f; s.gainDb = g; s.q = q; s.slopeDb = slope; return s; }

// Mica 1.x analog prototypes (slope index 0..3 = 6, 12, 24, 48 dB/oct)
static cpx legacy(int shape, int slope, float fc, float gainDb, float q, float hz) {
    const cpx s(0.f, hz / fc);
    const float A = std::pow(10.f, gainDb / 40.f), sq = std::sqrt(A);
    static const float k1[1] = { 1.41421356f }, k2[2] = { 1.84775907f, 0.76536686f }, k4[4] = { 1.96157056f, 1.66293922f, 1.11114047f, 0.39018064f };
    switch (shape) {
    case kBell:      return (s * s + s * (A / q) + 1.f) / (s * s + s / (A * q) + 1.f);
    case kLowShelf:  return A * (s * s + s * (sq / q) + A) / (A * s * s + s * (sq / q) + 1.f);
    case kHighShelf: return A * (A * s * s + s * (sq / q) + 1.f) / (s * s + s * (sq / q) + A);
    default: {
        const bool hp = shape == kLowCut;
        if (slope == 0) return hp ? s / (s + 1.f) : cpx(1.f, 0.f) / (s + 1.f);
        const int st = slope <= 1 ? 1 : (slope == 2 ? 2 : 4); const float* kb = st == 1 ? k1 : (st == 2 ? k2 : k4);
        cpx h(1.f, 0.f);
        for (int i = 0; i < st; ++i) { const float k = i == st - 1 ? kb[i] * 0.70710678f / q : kb[i]; h *= (hp ? s * s : cpx(1.f, 0.f)) / (s * s + k * s + 1.f); }
        return h;
    }
    }
}

static void testLegacy() {
    const int slopes[4] = { 6, 12, 24, 48 };
    float worst = 0;
    for (int shape : { kBell, kLowShelf, kHighShelf, kLowCut, kHighCut })
        for (int si = 0; si < 4; ++si) for (float g : { -18.f, -3.f, 0.f, 7.5f, 24.f }) for (float q : { 0.3f, 0.707f, 2.f, 12.f })
            for (float f = 20; f < 22000; f *= 1.19f) {
                if ((shape == kLowShelf || shape == kHighShelf) && si != 1) continue;   // 1.x shelves were always 12 dB/oct
                Design d; design(mk(shape, 900.f, g, q, slopes[si]), 48000.f, d);
                const cpx a = response(d, f), b = legacy(shape, si, 900.f, g, q, f);
                worst = std::max(worst, std::abs(a - b) / std::max(1e-3f, std::abs(b)));
            }
    CHECK(worst < 1e-4f, "legacy mismatch %g", worst);
}

static void testSlopes() {
    for (int sl = 6; sl <= 96; sl += 6) for (int shape : { kLowCut, kHighCut }) {
        const float f0 = shape == kLowCut ? 100.f : 10000.f, far = shape == kLowCut ? 100.f / 16 : 10000.f * 16;
        const float a = dbAt(mk(shape, f0, 0, 0.707f, sl), far), b = dbAt(mk(shape, f0, 0, 0.707f, sl), shape == kLowCut ? far / 2 : far * 2);
        CHECK(std::fabs((a - b) - sl) < 0.5f, "%s %d dB/oct: measured %.2f", shapeName(shape), sl, a - b);
        CHECK(std::fabs(dbAt(mk(shape, f0, 0, 0.707f, sl), f0) + 3.0103f) < 0.01f, "%d: -3 dB at fc (%.3f)", sl, dbAt(mk(shape, f0, 0, 0.707f, sl), f0));
        CHECK(std::fabs(dbAt(mk(shape, f0, 0, 0.707f, sl), shape == kLowCut ? f0 * 8 : f0 / 8)) < (sl == 6 ? 0.1f : 0.05f), "%d passband flat", sl);
    }
    const float bw = dbAt(mk(kLowCut, 1000, 0, 0.707f, kBrickwall), 1000.f / 1.26f), pass = dbAt(mk(kLowCut, 1000, 0, 0.707f, kBrickwall), 1000.f * 1.12f);
    CHECK(bw < -60.f && pass > -0.01f, "brickwall: %.1f dB a third of an octave below, %.3f dB two semitones above", bw, pass);
}

static void testShelves() {
    for (int sl = 6; sl <= 96; sl += 6) for (float g : { -24.f, -6.f, 6.f, 18.f }) {
        CHECK(std::fabs(dbAt(mk(kLowShelf, 500, g, 0.707f, sl), 2) - g) < 0.05f, "low shelf %d %g: DC %.3f", sl, g, dbAt(mk(kLowShelf, 500, g, 0.707f, sl), 2));
        CHECK(std::fabs(dbAt(mk(kLowShelf, 500, g, 0.707f, sl), 500) - g / 2) < 0.02f, "low shelf mid %.3f", dbAt(mk(kLowShelf, 500, g, 0.707f, sl), 500));
        CHECK(std::fabs(dbAt(mk(kHighShelf, 500, g, 0.707f, sl), 20000) - g) < 0.2f || sl == 6, "high shelf top %.3f", dbAt(mk(kHighShelf, 500, g, 0.707f, sl), 20000));
        CHECK(std::fabs(dbAt(mk(kHighShelf, 500, g, 0.707f, sl), 500) - g / 2) < 0.02f, "high shelf mid");
        CHECK(std::fabs(dbAt(mk(kTiltShelf, 500, g, 0.707f, sl), 500)) < 0.02f && std::fabs(dbAt(mk(kTiltShelf, 500, g, 0.707f, sl), 2) + g / 2) < 0.05f, "tilt shelf");
    }
}

static void testSpecial() {
    CHECK(dbAt(mk(kNotch, 1000, 0, 2, 12), 1000) < -80.f && std::fabs(dbAt(mk(kNotch, 1000, 0, 2, 12), 4000)) < 0.3f, "notch");
    for (int sl = 6; sl <= 96; sl += 6) {
        CHECK(std::fabs(dbAt(mk(kBandPass, 1000, 0, 1, sl), 1000)) < 0.01f, "band pass %d centre %.3f", sl, dbAt(mk(kBandPass, 1000, 0, 1, sl), 1000));
        CHECK(dbAt(mk(kBandPass, 1000, 0, 1, sl), 64) < -10.f, "band pass %d skirt", sl);
        float worst = 0; for (float f = 10; f < 24000; f *= 1.3f) worst = std::max(worst, std::fabs(dbAt(mk(kAllPass, 700, 0, 0.9f, sl), f)));
        CHECK(worst < 1e-3f, "all pass %d not flat: %g", sl, worst);
    }
    for (float g : { -30.f, -6.f, 12.f }) {
        const float a = dbAt(mk(kFlatTilt, 1000, g, 1, 12), 250), b = dbAt(mk(kFlatTilt, 1000, g, 1, 12), 4000);
        CHECK(std::fabs((b - a) - g / 10 * 4) < 0.15f * std::fabs(g) / 10 * 4 + 0.05f, "flat tilt %g: %.3f over 4 octaves", g, b - a);
        CHECK(std::fabs(dbAt(mk(kFlatTilt, 1000, g, 1, 12), 1000)) < 0.01f, "flat tilt pivot");
    }
}

static void testCascade() {
    const float fs = 48000.f;
    // digital response matches the analog design well below Nyquist, and the cascade matches its own formula
    for (int shape = 0; shape < kNumShapes; ++shape) for (int sl : { 6, 12, 30, 96 }) {
        Spec s = mk(shape, 1000, 9, 1.3f, sl); Design d; design(s, fs, d);
        float worst = 0; for (float f = 20; f < 3000; f *= 1.2f) if (db(response(d, f)) > -40) worst = std::max(worst, std::fabs(db(Cascade<1>::digitalResponse(d, fs, f)) - db(response(d, f))));
        CHECK(worst < 0.4f || shape == kNotch, "%s %d: digital vs analog %.3f dB", shapeName(shape), sl, worst);
        // measure the cascade with a sine
        Cascade<1> c; c.set(d, fs);
        for (float f : { 300.f, 1000.f, 5000.f }) {
            double pi = 0, po = 0;
            for (int i = 0; i < 48000; ++i) { const float x = std::sin(2 * kPi * f * i / fs), y = c.process(x, 0); if (i > 24000) { pi += x * x; po += y * y; } }
            const float meas = 10 * std::log10(po / pi), want = db(Cascade<1>::digitalResponse(d, fs, f));
            CHECK(std::fabs(meas - want) < 0.05f || want < -40, "%s %d at %g: %.3f vs %.3f", shapeName(shape), sl, f, meas, want);
        }
        CHECK(c.finite(), "finite");
    }
}

static void testFit() {
    std::vector<float> hz, tg;
    for (float f = 20; f <= 20000; f *= 1.06f) hz.push_back(f);
    // a curve made of three known bands is recovered within 0.5 dB
    const FitBand truth[3] = { { kLowShelf, 120, 4, 0.7071f }, { kBell, 900, -6, 2.5f }, { kBell, 6000, 3.5f, 1.f } };
    for (float f : hz) { float v = 0; for (const FitBand& b : truth) v += dbAt(mk(b.shape, b.freq, b.gainDb, b.q, 12), f); tg.push_back(v); }
    CurveFitter cf; FitOptions o; o.maxBands = 6; o.tolDb = 0.2f;
    const auto bs = cf.fit(hz, tg, o);
    CHECK(cf.rmsError(bs) < 0.3f && bs.size() <= 6, "fit rms %.3f with %zu bands", cf.rmsError(bs), bs.size());
    // a flat target needs no bands; a free-hand wiggle is approximated
    std::vector<float> flat(hz.size(), 0.f);
    CHECK(cf.fit(hz, flat, o).empty(), "flat");
    std::vector<float> wig; for (float f : hz) wig.push_back(5.f * std::sin(std::log2(f / 20.f) * 1.3f));
    o.maxBands = 10; const auto wb = cf.fit(hz, wig, o);
    CHECK(cf.rmsError(wb) < 1.0f, "wiggle rms %.3f with %zu bands", cf.rmsError(wb), wb.size());
}

int main() {
    testFit();
    testLegacy(); testSlopes(); testShelves(); testSpecial(); testCascade();
    std::printf(fails ? "%d FAILED\n" : "all eq tests passed\n", fails);
    return fails ? 1 : 0;
}
