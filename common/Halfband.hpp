// epsilon-common — 2x/4x oversampling with linear-phase halfband filters (65 taps, Kaiser window, beta 8).
// The up-sampler's latency is half the total. License: MIT.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

namespace epsilon {

static constexpr int kHbHalf = 16, kHbC = 2 * kHbHalf;
inline const float* halfbandTaps() {
    static float t[kHbHalf]; static bool done = false;
    if (!done) {
        done = true;
        auto bessel0 = [](double x) { double s = 1, term = 1; for (int k = 1; k < 30; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); s += term; } return s; };
        const double beta = 8.0, N = 2 * kHbC, PI = 3.14159265358979323846; double sum = 0.5;
        for (int j = 0; j < kHbHalf; ++j) {
            const int k = 2 * j + 1; const double sinc = std::sin(PI * k / 2.0) / (PI * k / 2.0);
            const double r = (double)(k + kHbC) / N * 2 - 1, w = bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel0(beta);
            t[j] = (float)(0.5 * sinc * w); sum += 2 * t[j];
        }
        for (int j = 0; j < kHbHalf; ++j) t[j] = (float)(t[j] / sum);
    }
    return t;
}
struct HbUp {
    float buf[128] = {}; int pos = 0;
    void reset() { std::memset(buf, 0, sizeof buf); pos = 0; }
    inline void process(float x, float& y0, float& y1) {
        pos = (pos + 1) & 63; buf[pos] = buf[pos + 64] = x;
        const float* h = halfbandTaps(); const float* b = buf + pos + 64;
        y0 = b[-kHbHalf]; float s = 0;
        for (int j = 0; j < kHbHalf; ++j) s += h[j] * (b[-kHbHalf - j] + b[-kHbHalf + j + 1]);
        y1 = 2.f * s;
    }
};
struct HbDown {
    float buf[256] = {}; int pos = 0;
    void reset() { std::memset(buf, 0, sizeof buf); pos = 0; }
    inline float process(float x0, float x1) {
        pos = (pos + 1) & 127; buf[pos] = buf[pos + 128] = x0;
        pos = (pos + 1) & 127; buf[pos] = buf[pos + 128] = x1;
        const float* h = halfbandTaps(); const float* c = buf + pos + 128 - kHbC - 1;
        float s = 0.5f * c[0];
        for (int j = 0; j < kHbHalf; ++j) s += h[j] * (c[-(2 * j + 1)] + c[2 * j + 1]);
        return s;
    }
};
// 1x, 2x or 4x; latency in base-rate samples 0 / 32 / 48 (half of it in the up-sampler)
// factor 1, 2 or 4; latency in base-rate samples 0 / 32 / 48 (half of it in the up-sampler)
struct Oversampler {
    HbUp u1, u2; HbDown d1, d2;
    void reset() { u1.reset(); u2.reset(); d1.reset(); d2.reset(); }
    static int latency(int os) { return os == 1 ? 0 : (os == 2 ? 32 : 48); }
    static int upLatency(int os) { return latency(os) / 2; }
    inline void up(float x, int os, float* y) {
        if (os == 1) { y[0] = x; return; }
        float a, b; u1.process(x, a, b);
        if (os == 2) { y[0] = a; y[1] = b; return; }
        u2.process(a, y[0], y[1]); u2.process(b, y[2], y[3]);
    }
    inline float down(const float* y, int os) {
        if (os == 1) return y[0];
        if (os == 2) return d1.process(y[0], y[1]);
        const float a = d2.process(y[0], y[1]), b = d2.process(y[2], y[3]);
        return d1.process(a, b);
    }
};

} // namespace epsilon
