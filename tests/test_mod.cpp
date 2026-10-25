// Modulation system v2: XLFO rate, sync and steps; envelope generator triggering; envelope follower;
// XY and MIDI sources; the matrix (bypass, slot-on-slot).
#include "../common/ModSystem.hpp"
#include <cstdio>
#include <vector>

using namespace epsilon::mod;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// a parameter list holding just the modulation blocks, laid out as a plugin would
static const int kSlots = 8;
static const Layout L = { 0, (uint32_t)(kNumXlfo * kXlfoFields), (uint32_t)(kNumXlfo * kXlfoFields + kNumEg * kEgFields),
    (uint32_t)(kNumXlfo * kXlfoFields + kNumEg * kEgFields + kNumEf * kEfFields),
    (uint32_t)(kNumXlfo * kXlfoFields + kNumEg * kEgFields + kNumEf * kEfFields + kNumXy * 2), kSlots, 10, 10 + kSlots };
static const int kN = (int)L.slotFirst + kSlots * kSlotFields;
static void defaults(std::vector<float>& p) {
    p.assign(kN, 0.f);
    for (int l = 0; l < kNumXlfo; ++l) for (int f = 0; f < kXlfoFields; ++f) p[L.lfo(l, f)] = xlfoRange(f).def;
    for (int e = 0; e < kNumEg; ++e) for (int f = 0; f < kEgFields; ++f) p[L.eg(e, f)] = egRange(f).def;
    for (int e = 0; e < kNumEf; ++e) for (int f = 0; f < kEfFields; ++f) p[L.ef(e, f)] = efRange(f).def;
}

int main() {
    const float fs = 48000, cr = fs / 16;
    {   // free XLFO: a 2 Hz sine crosses zero upwards twice a second
        std::vector<float> p; defaults(p); p[L.lfo(0, kLRate)] = 2; Sources s;
        int ups = 0; float prev = 0;
        for (int i = 0; i < (int)cr; ++i) { s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120); const float v = s.val[kSrcXlfo1]; if (prev < 0 && v >= 0) ++ups; prev = v; }
        CHECK(ups == 2, "2 Hz sine: %d upward zero crossings in 1 s", ups);
    }
    {   // synced XLFO follows the bar position while playing
        std::vector<float> p; defaults(p); p[L.lfo(0, kLSync)] = 1; p[L.lfo(0, kLDiv)] = 4; Sources s;   // one bar
        s.tick(p.data(), p.data(), L, fs, cr, true, true, 7.25, 120);
        CHECK(std::fabs(s.xlfo[0].phase - (0.25 + 120.0 / 60 / 4 / cr)) < 1e-6, "sync phase %f", s.xlfo[0].phase);
    }
    {   // steps: with no glide the output is the step value
        std::vector<float> p; defaults(p); p[L.lfo(1, kLMode)] = 1; p[L.lfo(1, kLSteps)] = 4; p[L.lfo(1, kLRate)] = 1;
        for (int k = 0; k < 4; ++k) p[L.lfo(1, kLStep1 + k)] = -0.75f + 0.5f * k;
        Sources s; std::vector<float> seen;
        for (int i = 0; i < (int)cr; ++i) { s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120); if (i % ((int)cr / 4) == (int)cr / 8) seen.push_back(s.val[kSrcXlfo2]); }
        bool ok = seen.size() == 4; for (int k = 0; k < 4 && ok; ++k) ok = std::fabs(seen[k] - (-0.75f + 0.5f * k)) < 1e-6f;
        CHECK(ok, "step values");
    }
    {   // EG, audio-triggered: rises with a loud input, falls after it stops
        std::vector<float> p; defaults(p); p[L.eg(0, kETrigger)] = 1; p[L.eg(0, kEThreshold)] = -20; Sources s;
        for (int i = 0; i < 4800; ++i) { s.follow(0.5f, 0); if (i % 16 == 0) s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120); }
        const float on = s.val[kSrcEg1];
        for (int i = 0; i < 96000; ++i) { s.follow(0, 0); if (i % 16 == 0) s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120); }
        CHECK(on > 0.45f && s.val[kSrcEg1] < 0.01f, "EG on %.3f, after release %.3f", on, s.val[kSrcEg1]);
    }
    {   // EG, MIDI-triggered
        std::vector<float> p; defaults(p); p[L.eg(1, kETrigger)] = 0; Sources s;
        s.noteOn(p.data(), L, 60, 100);
        for (int i = 0; i < 300; ++i) s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120);
        CHECK(s.eg[1].gated() && s.val[kSrcEg2] > 0.4f, "MIDI EG %.3f", s.val[kSrcEg2]);
        s.noteOff(p.data(), L, 60); CHECK(!s.eg[1].gated(), "note off releases");
        CHECK(std::fabs(s.vel - 100 / 127.f) < 1e-6f, "velocity");
    }
    {   // EF: -6 dBFS input reads about (−6 + 54) / 54; sidechain source listens to the sidechain only
        std::vector<float> p; defaults(p); p[L.ef(1, kFSource)] = 1; Sources s;
        s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120);
        for (int i = 0; i < 48000; ++i) { s.follow(0.5f, 0); if (i % 16 == 0) s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120); }
        CHECK(std::fabs(s.val[kSrcEf1] - (48.f / 54.f)) < 0.01f && s.val[kSrcEf2] < 0.01f, "EF %.3f, sidechain EF %.3f", s.val[kSrcEf1], s.val[kSrcEf2]);
    }
    {   // XY and MIDI sources
        std::vector<float> p; defaults(p); p[L.xy(1, 1)] = 0.3f; Sources s; s.modWheel(127); s.pitchBend(0); s.noteOn(p.data(), L, 120, 1);
        s.tick(p.data(), p.data(), L, fs, cr, false, false, 0, 120);
        CHECK(s.val[kSrcXy2Y] == 0.3f && s.val[kSrcWheel] == 1.f && s.val[kSrcBend] == -1.f && s.val[kSrcKey] == 1.f, "XY / MIDI sources");
    }
    {   // matrix: plain slot, bypassed slot, slot-on-slot
        std::vector<float> p; defaults(p); float src[kNumSources] = {}; src[kSrcXlfo1] = 0.5f; src[kSrcWheel] = 1.f;
        float off[10 + kSlots];
        auto slot = [&](int s, int sr, int d, float a, bool byp) { p[L.slot(s, kSlotSrc)] = (float)sr; p[L.slot(s, kSlotDst)] = (float)d; p[L.slot(s, kSlotAmt)] = a; p[L.slot(s, kSlotBypass)] = byp ? 1.f : 0.f; };
        slot(0, kSrcXlfo1, 3, 0.4f, false);
        slot(1, kSrcXlfo1, 4, 0.4f, true);
        matrix(p.data(), L, src, off);
        CHECK(std::fabs(off[3] - 0.2f) < 1e-6f && off[4] == 0.f, "plain %.3f, bypassed %.3f", off[3], off[4]);
        slot(2, kSrcWheel, L.dstSlotFirst + 0, 0.3f, false);     // the wheel adds 0.3 to slot 1's amount
        matrix(p.data(), L, src, off);
        CHECK(std::fabs(off[3] - 0.35f) < 1e-6f, "slot-on-slot %.3f", off[3]);
    }
    std::printf(fails ? "%d FAILED\n" : "mod: all passed\n", fails);
    return fails ? 1 : 0;
}
