// epsilon-common — analog character stage: saturation, vintage colour and drift.
//   Warm:     asymmetric soft saturation (mostly even harmonics), like a tube stage
//   Punchy:   symmetric soft clipping (odd harmonics) that rounds peaks, like a transformer pushed hard
//   Vintage:  Warm plus a gentle tone (a little low-end weight, softened top) and slow random drift
// Drive (0-1) sets how hard the stage is pushed; small signals keep unity gain, so drive changes the colour,
// not the level. drift() is a slow random value in [-1, 1] that hosts can also use (e.g. to wobble a
// threshold). Generic waveshaping and filtering only. License: MIT.
#pragma once
#include "EqBand.hpp"
#include <cmath>
#include <cstdint>

namespace epsilon {

class Character {
public:
    enum Mode { kOff, kWarm, kPunchy, kVintage, kNumModes };

    // sampleRate: the rate process() runs at (an oversampled rate, if oversampling); tickRate: the rate tick() is
    // called at (0 = the same)
    void setup(float sampleRate, int mode, float drive01, float tickRate = 0.f) {
        fs = sampleRate; m = mode; drive = std::min(1.f, std::max(0.f, drive01)); tickFs = tickRate > 0.f ? tickRate : sampleRate;
        pre = std::pow(10.f, (2.f + 22.f * drive) / 20.f);           // +2 .. +24 dB into the shaper
        dcCoef = 1.f - std::exp(-2.f * 3.14159265f * 8.f / fs);
        driftCoef = 1.f - std::exp(-2.f * 3.14159265f * 0.15f / tickFs);
        if (m == kVintage && (toneFs != fs)) {
            toneFs = fs;
            eq::Spec lo; lo.shape = eq::kLowShelf; lo.freq = 110.f; lo.gainDb = 0.8f; lo.slopeDb = 12; lo.q = 0.6f;
            eq::Spec hi; hi.shape = eq::kHighCut; hi.freq = 17000.f; hi.slopeDb = 6;
            eq::Design d1, d2; eq::design(lo, fs, d1); eq::design(hi, fs, d2);
            eq::Design d; d.n = 0; for (int i = 0; i < d1.n; ++i) d.s[d.n++] = d1.s[i]; for (int i = 0; i < d2.n; ++i) d.s[d.n++] = d2.s[i];
            tone.set(d, fs);
        }
    }
    void reset() { tone.reset(); dc[0] = dc[1] = 0; drift_[0] = drift_[1] = 0; dTarget[0] = dTarget[1] = 0; noise = 0x9e3779b9u; }
    int mode() const { return m; }

    // advance the drift generator once per sample (call even when the shaper isn't used)
    inline void tick() {
        for (int c = 0; c < 2; ++c) {
            if (++dCount[c] >= (int)(tickFs * 0.25f)) { dCount[c] = 0; noise = noise * 1664525u + 1013904223u; dTarget[c] = ((noise >> 8) & 0xffff) / 32768.f - 1.f; }
            drift_[c] += (dTarget[c] - drift_[c]) * driftCoef;
        }
    }
    float drift(int c) const { return drift_[c]; }

    inline float process(float x, int c) {
        if (m == kOff) return x;
        const float d = drive;
        float y;
        if (m == kPunchy) {
            const float u = x * pre;
            y = u / std::sqrt(1.f + u * u * 0.6f) / pre;                  // smooth symmetric clip
        } else {
            const float bias = 0.18f + 0.12f * d + (m == kVintage ? 0.03f * drift_[c] : 0.f);
            const float u = x * pre;
            const float t = std::tanh(u + bias) - std::tanh(bias);       // asymmetric: even harmonics
            y = t / (pre * (1.f - std::tanh(bias) * std::tanh(bias)));   // unity small-signal gain
            dc[c] += (y - dc[c]) * dcCoef;                               // the asymmetry leaves DC: remove it
            y -= dc[c];
            if (m == kVintage) y = tone.process(y * (1.f + 0.01f * drift_[c]), c);
        }
        return y;
    }

private:
    float fs = 48000.f, tickFs = 48000.f, toneFs = 0.f, drive = 0.f, pre = 1.f, dcCoef = 0.001f, driftCoef = 0.0001f;
    int m = kOff;
    eq::Cascade<2> tone;
    float dc[2] = { 0, 0 }, drift_[2] = { 0, 0 }, dTarget[2] = { 0, 0 };
    int dCount[2] = { 0, 1000 };
    uint32_t noise = 0x9e3779b9u;
};

} // namespace epsilon
