// Modulation system v2: four XLFOs (wave or 16-step), two ADSR envelope generators (MIDI- or
// audio-triggered), two envelope followers (input or sidechain), two XY pads and five MIDI sources,
// feeding a matrix of slots (source, destination, bipolar amount, bypass). A slot can modulate the
// amount of another slot. Sources and the matrix run at a control rate chosen by the plugin.
//
// The plugin owns the parameters. It lays the source and slot parameters out in its own list using the
// field enums below and describes where they start with a Layout; the destinations other than slot
// amounts are the plugin's own (it maps them to parameters after mod::matrix).
//
// First used in Magma 2.0, whose output this reproduces exactly. Clean-room, generic techniques.
// License: MIT.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace epsilon {
namespace mod {

static constexpr int kNumXlfo = 4, kNumSteps = 16, kNumEg = 2, kNumEf = 2, kNumXy = 2;
enum XlfoField { kLMode, kLWave, kLRate, kLSync, kLDiv, kLBalance, kLGlide, kLSteps, kLStep1, kXlfoFields = kLStep1 + kNumSteps };
enum EgField { kEAttack, kEDecay, kESustain, kERelease, kETrigger, kEThreshold, kEgFields };
enum EfField { kFAttack, kFRelease, kFGain, kFSource, kEfFields };
enum SlotField { kSlotSrc, kSlotDst, kSlotAmt, kSlotBypass, kSlotFields };
enum Source { kSrcNone, kSrcXlfo1, kSrcXlfo2, kSrcXlfo3, kSrcXlfo4, kSrcEg1, kSrcEg2, kSrcEf1, kSrcEf2,
              kSrcXy1X, kSrcXy1Y, kSrcXy2X, kSrcXy2Y, kSrcVelocity, kSrcKey, kSrcWheel, kSrcBend, kSrcAftertouch, kNumSources };
static constexpr int kNumWaves = 7;     // sine, triangle, saw up, saw down, square, random (gliding), sample & hold

inline float mclamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline const char* sourceName(int s) {
    static const char* const n[kNumSources] = { "None", "XLFO 1", "XLFO 2", "XLFO 3", "XLFO 4", "EG 1", "EG 2", "EF 1", "EF 2",
        "XY 1 X", "XY 1 Y", "XY 2 X", "XY 2 Y", "Velocity", "Key", "Mod Wheel", "Pitch Bend", "Aftertouch" };
    return n[s < 0 || s >= kNumSources ? 0 : s];
}
inline const char* sourceShort(int s) {
    static const char* const n[kNumSources] = { "-", "XLFO1", "XLFO2", "XLFO3", "XLFO4", "EG1", "EG2", "EF1", "EF2",
        "XY1 X", "XY1 Y", "XY2 X", "XY2 Y", "VEL", "KEY", "WHEEL", "BEND", "AT" };
    return n[s < 0 || s >= kNumSources ? 0 : s];
}
// bipolar sources swing -1..1, the others 0..1
inline bool sourceBipolar(int s) { return (s >= kSrcXlfo1 && s <= kSrcXlfo4) || s == kSrcKey || s == kSrcBend; }

// Where the plugin keeps each block of parameters. Parameters inside a block follow the field enums.
struct Layout {
    uint32_t xlfoFirst, egFirst, efFirst, xyFirst, slotFirst;
    int numSlots;
    int dstSlotFirst, numDests;     // destination numbers: slot amounts are dstSlotFirst + slot; 0 is "none"
    uint32_t lfo(int l, int f) const { return xlfoFirst + (uint32_t)(l * kXlfoFields + f); }
    uint32_t eg(int e, int f) const { return egFirst + (uint32_t)(e * kEgFields + f); }
    uint32_t ef(int e, int f) const { return efFirst + (uint32_t)(e * kEfFields + f); }
    uint32_t xy(int x, int axis) const { return xyFirst + (uint32_t)(x * 2 + axis); }
    uint32_t slot(int s, int f) const { return slotFirst + (uint32_t)(s * kSlotFields + f); }
};

// value ranges of the source fields (min, max, default, log, integer)
struct FieldRange { float min, max, def; bool log, integer; };
inline FieldRange xlfoRange(int f) {
    static const FieldRange l[kLStep1] = { { 0, 1, 0, false, true }, { 0, 6, 0, false, true }, { 0.01f, 50, 1, true, false }, { 0, 3, 0, false, true },
        { 0, 10, 4, false, true }, { 0, 1, 0.5f, false, false }, { 0, 1, 0, false, false }, { 1, kNumSteps, 16, false, true } };
    if (f < kLStep1) return l[f];
    const FieldRange st = { -1, 1, 0, false, false }; return st;
}
inline FieldRange egRange(int f) {
    static const FieldRange e[kEgFields] = { { 0.001f, 5, 0.01f, true, false }, { 0.001f, 5, 0.3f, true, false }, { 0, 1, 0.5f, false, false },
        { 0.001f, 10, 0.4f, true, false }, { 0, 1, 1, false, true }, { -60, 0, -30, false, false } };
    return e[f];
}
inline FieldRange efRange(int f) {
    static const FieldRange r[kEfFields] = { { 0.0005f, 1, 0.005f, true, false }, { 0.005f, 5, 0.15f, true, false }, { -24, 24, 0, false, false }, { 0, 1, 0, false, true } };
    return r[f];
}
inline FieldRange slotRange(int f, int numDests) {
    const FieldRange s[kSlotFields] = { { 0, (float)(kNumSources - 1), 0, false, true }, { 0, (float)(numDests - 1), 0, false, true }, { -1, 1, 0, false, false }, { 0, 1, 0, false, true } };
    return s[f];
}

// LFO divisions, in bars; sync modes 0 free, 1 straight, 2 dotted, 3 triplet
static constexpr int kNumDivs = 11;
inline float divBars(int i) { static const float t[kNumDivs] = { 16, 8, 4, 2, 1, 1.f/2, 1.f/4, 1.f/8, 1.f/16, 1.f/32, 1.f/64 }; return t[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)]; }
inline float syncFactor(int s) { return s == 2 ? 1.5f : (s == 3 ? 2.f / 3.f : 1.f); }
inline const char* divName(int i) { static const char* const d[kNumDivs] = { "16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" }; return d[i < 0 ? 0 : (i >= kNumDivs ? kNumDivs - 1 : i)]; }

struct ADSR {
    enum Stage { Idle, Attack, Decay, Sustain, Release } stage = Idle;
    float value = 0, s = 0.5f, cA = 0, cD = 0, cR = 0;
    static float coef(float t, float sr) { return 1.f - std::exp(-1.f / std::max(t * sr, 1.f)); }
    void set(float sr, float A, float D, float S, float R) { s = mclamp(S, 0.f, 1.f); cA = coef(A / 1.5f, sr); cD = coef(D / 4.f, sr); cR = coef(R / 4.f, sr); }
    void gate(bool on) { stage = on ? Attack : (stage == Idle ? Idle : Release); }
    bool gated() const { return stage == Attack || stage == Decay || stage == Sustain; }
    float process() {
        switch (stage) {
        case Attack:  value += (1.2f - value) * cA; if (value >= 1.f) { value = 1.f; stage = Decay; } break;
        case Decay:   value += (s - value) * cD; if (std::fabs(value - s) < 1e-4f) { value = s; stage = Sustain; } break;
        case Sustain: value = s; break;
        case Release: value -= value * cR; if (value < 1e-5f) { value = 0; stage = Idle; } break;
        case Idle:    value = 0; break;
        }
        return value;
    }
};

struct Xlfo {
    double phase = 0; float rndA = 0, rndB = 0, held = 0, stepOut = 0; uint32_t rng = 1; int lastStep = -1;
    float rnd() { rng = rng * 1664525u + 1013904223u; return (int32_t)rng * (1.f / 2147483648.f); }
    static float warp(float t, float bal) { bal = mclamp(bal, 0.02f, 0.98f); return t < bal ? 0.5f * t / bal : 0.5f + 0.5f * (t - bal) / (1.f - bal); }
    float wave(int w, float bal) const {
        const float t = (float)phase;
        switch (w) {
        case 0: return -std::cos(2.f * 3.14159265358979f * warp(t, bal));
        case 2: return 2.f * warp(t, bal) - 1.f;
        case 3: return 1.f - 2.f * warp(t, bal);
        case 4: return t < mclamp(bal, 0.02f, 0.98f) ? 1.f : -1.f;
        case 5: return rndA + (rndB - rndA) * t;
        case 6: return held;
        default: { const float u = warp(t, bal); return u < 0.5f ? -1.f + 4.f * u : 3.f - 4.f * u; }
        }
    }
    void advance(double inc) { phase += inc; if (phase >= 1.0) { phase -= std::floor(phase); rndA = rndB; rndB = rnd(); held = rnd(); } }
};

// All sources. Call follow() every sample with the input (and sidechain) level, tick() at the control rate.
struct Sources {
    Xlfo xlfo[kNumXlfo]; ADSR eg[kNumEg];
    float efEnv[kNumEf] = {}, efA[kNumEf] = {}, efR[kNumEf] = {}; bool efSrc[kNumEf] = {};
    float inLevel = 0;
    int stack[16] = {}, held = 0, lastNote = -1; float vel = 0, wheel = 0, bend = 0, at = 0;
    float val[kNumSources] = {};

    void resetFollowers() { for (int e = 0; e < kNumEf; ++e) efEnv[e] = 0; }

    // MIDI (p: the plugin's parameter values)
    void noteOn(const float* p, const Layout& L, int note, int v) {
        if (held < 16) stack[held++] = note;
        lastNote = note; vel = v / 127.f;
        for (int e = 0; e < kNumEg; ++e) if (p[L.eg(e, kETrigger)] < 0.5f) eg[e].gate(true);
    }
    void noteOff(const float* p, const Layout& L, int note) {
        int j = 0; for (int i = 0; i < held; ++i) if (stack[i] != note) stack[j++] = stack[i];
        held = j;
        if (held > 0) lastNote = stack[held - 1];
        else for (int e = 0; e < kNumEg; ++e) if (p[L.eg(e, kETrigger)] < 0.5f) eg[e].gate(false);
    }
    void allNotesOff(const float* p, const Layout& L) { held = 0; for (int e = 0; e < kNumEg; ++e) if (p[L.eg(e, kETrigger)] < 0.5f) eg[e].gate(false); }
    void pitchBend(int v14) { bend = (v14 - 8192) / 8192.f; }
    void modWheel(int v) { wheel = v / 127.f; }
    void channelPressure(int v) { at = v / 127.f; }
    void polyPressure(int note, int v) { if (note == lastNote) at = v / 127.f; }

    // every sample: aIn = max |input|, aSc = max |sidechain| (0 if none)
    void follow(float aIn, float aSc) {
        for (int e = 0; e < kNumEf; ++e) { const float a = efSrc[e] ? aSc : aIn; efEnv[e] += (a > efEnv[e] ? efA[e] : efR[e]) * (a - efEnv[e]); }
        inLevel += (aIn > inLevel ? 0.01f : 0.0005f) * (aIn - inLevel);
    }

    // control rate. p: parameter values; effv: the modulated values (XLFO rate and balance and the EG
    // stages can themselves be modulated); fs: sample rate; cr: control rate.
    void tick(const float* p, const float* effv, const Layout& L, float fs, float cr, bool tValid, bool tPlaying, double tBarPos, double bpm) {
        for (int l = 0; l < kNumXlfo; ++l) {
            const int sync = (int)std::round(p[L.lfo(l, kLSync)]);
            double rate = effv[L.lfo(l, kLRate)];
            if (sync > 0) {
                const double bars = divBars((int)std::round(p[L.lfo(l, kLDiv)])) * syncFactor(sync);
                rate = bpm / 60.0 / 4.0 / bars;
                if (tValid && tPlaying) { xlfo[l].phase = std::fmod(tBarPos / bars, 1.0); if (xlfo[l].phase < 0) xlfo[l].phase += 1; }
            }
            xlfo[l].advance(rate / cr);
            if (p[L.lfo(l, kLMode)] > 0.5f) {         // step sequencer
                const int steps = (int)mclamp(std::round(p[L.lfo(l, kLSteps)]), 1, kNumSteps);
                const int st = std::min(steps - 1, (int)(xlfo[l].phase * steps));
                const float target = mclamp(p[L.lfo(l, kLStep1 + st)], -1.f, 1.f), gl = mclamp(p[L.lfo(l, kLGlide)], 0.f, 1.f);
                const float stepSecs = (float)(1.0 / std::max(rate, 1e-4) / steps);
                const float c = gl < 0.01f ? 1.f : 1.f - std::exp(-1.f / std::max(gl * stepSecs * 0.5f * cr, 1.f));
                xlfo[l].stepOut += (target - xlfo[l].stepOut) * c;
                val[kSrcXlfo1 + l] = xlfo[l].stepOut;
            } else val[kSrcXlfo1 + l] = xlfo[l].wave((int)std::round(p[L.lfo(l, kLWave)]), effv[L.lfo(l, kLBalance)]);
        }
        for (int e = 0; e < kNumEg; ++e) {
            if (p[L.eg(e, kETrigger)] > 0.5f) {               // audio-triggered: the input crossing the threshold
                const float thr = std::pow(10.f, p[L.eg(e, kEThreshold)] / 20.f);
                if (!eg[e].gated() && inLevel > thr) eg[e].gate(true);
                else if (eg[e].gated() && inLevel < thr * 0.7f) eg[e].gate(false);
            }
            eg[e].set(cr, effv[L.eg(e, kEAttack)], effv[L.eg(e, kEDecay)], effv[L.eg(e, kESustain)], effv[L.eg(e, kERelease)]);
            val[kSrcEg1 + e] = eg[e].process();
        }
        for (int e = 0; e < kNumEf; ++e) {
            efA[e] = 1.f - std::exp(-1.f / (std::max(p[L.ef(e, kFAttack)], 0.0001f) * fs));
            efR[e] = 1.f - std::exp(-1.f / (std::max(p[L.ef(e, kFRelease)], 0.001f) * fs));
            efSrc[e] = p[L.ef(e, kFSource)] > 0.5f;
            val[kSrcEf1 + e] = mclamp((20.f * std::log10(std::max(efEnv[e], 1e-9f)) + p[L.ef(e, kFGain)] + 54.f) / 54.f, 0.f, 1.f);
        }
        for (int x = 0; x < kNumXy; ++x) { val[kSrcXy1X + 2 * x] = p[L.xy(x, 0)]; val[kSrcXy1Y + 2 * x] = p[L.xy(x, 1)]; }
        val[kSrcVelocity] = vel; val[kSrcKey] = lastNote < 0 ? 0.f : mclamp((lastNote - 60) / 60.f, -1.f, 1.f);
        val[kSrcWheel] = wheel; val[kSrcBend] = bend; val[kSrcAftertouch] = at;
        val[kSrcNone] = 0;
    }
};

// The matrix: fills off[0..numDests) with the normalised offset for every destination. Slot amounts
// are applied first, so a slot can modulate another slot's amount. Up to 256 slots.
inline void matrix(const float* p, const Layout& L, const float* src, float* off) {
    std::fill(off, off + L.numDests, 0.f);
    int act[256], na = 0;
    const int ns = std::min(L.numSlots, 256);
    for (int s = 0; s < ns; ++s) {
        const int sr = (int)std::round(p[L.slot(s, kSlotSrc)]), dst = (int)std::round(p[L.slot(s, kSlotDst)]);
        if (sr <= kSrcNone || sr >= kNumSources || dst <= 0 || dst >= L.numDests || p[L.slot(s, kSlotBypass)] > 0.5f) continue;
        act[na++] = s;
    }
    float amt[256];
    for (int s = 0; s < ns; ++s) amt[s] = p[L.slot(s, kSlotAmt)];
    for (int k = 0; k < na; ++k) { const int s = act[k], dst = (int)std::round(p[L.slot(s, kSlotDst)]);
        if (dst >= L.dstSlotFirst) { const int t = dst - L.dstSlotFirst; amt[t] = mclamp(amt[t] + p[L.slot(s, kSlotAmt)] * src[(int)std::round(p[L.slot(s, kSlotSrc)])], -1.f, 1.f); off[dst] += p[L.slot(s, kSlotAmt)] * src[(int)std::round(p[L.slot(s, kSlotSrc)])]; } }
    for (int k = 0; k < na; ++k) { const int s = act[k], dst = (int)std::round(p[L.slot(s, kSlotDst)]);
        if (dst < L.dstSlotFirst) off[dst] += amt[s] * src[(int)std::round(p[L.slot(s, kSlotSrc)])]; }
}

} // namespace mod
} // namespace epsilon
