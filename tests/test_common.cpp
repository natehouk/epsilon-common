// epsilon-common tests: preset round trip, undo/redo and A/B, halfband filters, FFT, typed characters.
#include "../common/Convolver.hpp"
#include "../common/Fft.hpp"
#include "../common/Halfband.hpp"
#include "../common/Platform.hpp"
#include "../common/UndoHistory.hpp"
#include "../common/UserPresets.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

using namespace epsilon;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void testPresets() {
    char tmpl[] = "/tmp/epsilon-common-XXXXXX";
    const char* d = mkdtemp(tmpl);
    CHECK(d, "mkdtemp");
    setenv("HOME", d, 1); setenv("XDG_CONFIG_HOME", "", 1); setenv("APPDATA", d, 1);
    const PresetParam params[] = { { "gain", -24, 24, true }, { "mode", 0, 3, true }, { "meter", 0, 1, false }, { "freq", 20, 20000, true } };
    PresetStore store("Test", ".test");
    CHECK(store.list().empty(), "empty list");
    const float v[] = { 3.25f, 2.f, 0.7f, 1234.5f };
    CHECK(store.save("My Preset/1", params, 4, v), "save");
    CHECK(store.save("alpha", params, 4, v), "save 2");
    const auto names = store.list();
    CHECK(names.size() == 2 && names[0] == "alpha" && names[1] == "My Preset1", "list order/sanitize: %zu", names.size());
    float out[] = { 0, 0, 0.1f, 20 };
    CHECK(store.load("My Preset1", params, 4, out), "load");
    CHECK(out[0] == 3.25f && out[1] == 2.f && out[2] == 0.1f && out[3] == 1234.5f, "values %g %g %g %g", out[0], out[1], out[2], out[3]);
    // clamping and unknown keys
    { FILE* f = std::fopen((store.dir() + "/odd.test").c_str(), "wb"); std::fprintf(f, "# x\nformat=1\ngain=99\nunknown=4\n"); std::fclose(f); }
    float o2[] = { 0, 1, 0, 100 };
    CHECK(store.load("odd", params, 4, o2) && o2[0] == 24.f && o2[1] == 1.f && o2[3] == 100.f, "clamp/missing keys");
    CHECK(store.remove("alpha") && store.list().size() == 2, "remove");
    CHECK(PresetStore::sanitize("  ..x:y*z.  ") == "..xyz", "sanitize '%s'", PresetStore::sanitize("  ..x:y*z.  ").c_str());
    CHECK(!store.save("   ", params, 4, v), "empty name refused");
}

static void testUndo() {
    UndoHistory h;
    h.init(3, [](uint32_t p) { return p != 2; });   // param 2 is a meter
    float cur[3] = { 0, 0, 0 };
    h.reset(cur);
    CHECK(!h.canUndo(), "fresh");
    h.begin(cur); cur[0] = 1; CHECK(h.end(cur), "step 1");
    h.begin(cur); cur[2] = 5; CHECK(!h.end(cur), "meter change is not a step");
    h.begin(cur); cur[1] = 2; h.end(cur);
    CHECK(h.undoSteps() == 2, "2 steps");
    UndoHistory::Snap s;
    CHECK(h.undo(cur, s) && s[0] == 1 && s[1] == 0, "undo 1");
    cur[0] = s[0]; cur[1] = s[1];
    CHECK(h.undo(cur, s) && s[0] == 0, "undo 2");
    cur[0] = s[0]; cur[1] = s[1];
    CHECK(!h.undo(cur, s), "nothing left");
    CHECK(h.redo(cur, s) && s[0] == 1 && h.canRedo(), "redo");
    cur[0] = s[0]; cur[1] = s[1];
    // host change between gestures updates "before"
    h.hostChanged(0, 7); cur[0] = 7;
    h.begin(cur); cur[1] = 3; h.end(cur);
    CHECK(!h.canRedo(), "new edit clears redo");
    CHECK(h.undo(cur, s) && s[0] == 7 && s[1] == 0, "before kept host value: %g %g", s[0], s[1]);
    cur[0] = s[0]; cur[1] = s[1];
    // A/B
    CHECK(h.switchTo(1, cur, s) && h.slot() == 1 && s[0] == 7 && s[3] == 1, "B starts as copy of A");
    h.begin(cur); cur[0] = 9; h.end(cur);
    CHECK(h.switchTo(0, cur, s) && s[0] == 7 && h.slot() == 0, "back to A");
    cur[0] = s[0];
    CHECK(h.switchTo(1, cur, s) && s[0] == 9, "B remembered");
    cur[0] = s[0];
    h.copyToOther(cur);
    CHECK(h.switchTo(0, cur, s) && s[0] == 9, "copy B to A");
    cur[0] = s[0];
    // undo across a switch returns to the slot
    CHECK(h.undo(cur, s) && h.slot() == 1, "undo switch -> slot %d", h.slot());
    for (int i = 0; i < 300; ++i) { h.begin(cur); cur[1] += 1; h.end(cur); }
    CHECK(h.undoSteps() == UndoHistory::kMaxSteps, "capped at %zu", h.undoSteps());
}

static float toneGain(int os, float f) {   // round trip up+down gain of a sine at f (fraction of base rate)
    Oversampler o; o.reset();
    double num = 0, den = 0; float y[4];
    const int N = 8192, lat = Oversampler::latency(os);   // in base-rate samples
    std::vector<float> in(N), out(N);
    for (int i = 0; i < N; ++i) { in[i] = std::sin(2 * M_PI * f * i); o.up(in[i], os, y); out[i] = o.down(y, os); }
    for (int i = 2048; i < N; ++i) { num += out[i] * out[i]; den += in[i - lat] * in[i - lat]; }
    return (float)(10 * std::log10(num / den));
}

static void testHalfband() {
    for (int os : { 2, 4 }) {
        const float pass = toneGain(os, 0.1f), hi = toneGain(os, 0.4f);
        CHECK(std::fabs(pass) < 0.05f, "os%d passband %.3f dB", os, pass);
        CHECK(std::fabs(hi) < 0.5f, "os%d 0.4 fs %.3f dB", os, hi);
    }
    // a 1x path is transparent with no latency
    Oversampler o; o.reset(); float y[4]; o.up(0.5f, 1, y); CHECK(o.down(y, 1) == 0.5f && Oversampler::latency(1) == 0, "1x");
    // latency (base-rate samples): an impulse comes out that much later
    for (int os : { 2, 4 }) {
        Oversampler q; q.reset(); int peak = -1; float best = 0;
        for (int i = 0; i < 96; ++i) { q.up(i == 0 ? 1.f : 0.f, os, y); const float v = std::fabs(q.down(y, os)); if (v > best) { best = v; peak = i; } }
        CHECK(peak == Oversampler::latency(os), "os%d latency peak %d vs %d", os, peak, Oversampler::latency(os));
    }
    // stopband: the filter taps reject above 0.75 of the half rate
    const float* t = halfbandTaps();
    double worst = 0;
    for (double f = 0.30; f <= 0.5; f += 0.002) {   // normalised to the oversampled rate; centre tap 0.5, odd taps either side
        double hf = 0.5;
        for (int j = 0; j < kHbHalf; ++j) hf += 2 * t[j] * std::cos(2 * M_PI * f * (2 * j + 1));
        worst = std::max(worst, std::fabs(hf));
    }
    CHECK(20 * std::log10(worst) < -60, "stopband %.1f dB", 20 * std::log10(worst));
}

static void testFft() {
    Fft f; f.init(64);
    std::vector<cpx> x(64);
    for (int i = 0; i < 64; ++i) x[i] = cpx((float)std::cos(2 * M_PI * 5 * i / 64.0), 0.f);
    f.run(x.data(), false);
    CHECK(std::fabs(std::abs(x[5]) - 32.f) < 1e-3f && std::abs(x[6]) < 1e-3f, "bin5 %g bin6 %g", std::abs(x[5]), std::abs(x[6]));
    f.run(x.data(), true);
    CHECK(std::fabs(x[3].real() - (float)std::cos(2 * M_PI * 15 / 64.0)) < 1e-4f, "inverse");
}

static void testConvolver() {
    // partitioned convolution equals direct convolution sample for sample; kernel swaps cross-fade
    const int B = 64, L = 300;
    std::vector<float> h(L), x(4096), ref(4096, 0.f), out(4096, 0.f);
    unsigned r = 1; auto rnd = [&]() { r = r * 1664525u + 1013904223u; return ((r >> 9) & 0xffff) / 32768.f - 1.f; };
    for (float& v : h) v = rnd() * 0.3f;
    for (float& v : x) v = rnd();
    for (int n = 0; n < 4096; ++n) for (int k = 0; k < L && k <= n; ++k) ref[n] += h[k] * x[n - k];
    Convolver c; c.init(B, 512); c.setKernel(h.data(), L);
    for (int b = 0; b < 4096 / B; ++b) c.process(x.data() + b * B, out.data() + b * B);
    float worst = 0; for (int n = 0; n < 4096; ++n) worst = std::max(worst, std::fabs(out[n] - ref[n]));
    CHECK(worst < 1e-3f, "convolver error %g", worst);
    // swap to a pure delay of 5 samples: after the cross-fade block, out = x delayed by 5
    std::vector<float> d(8, 0.f); d[5] = 1.f; c.setKernel(d.data(), 8);
    std::vector<float> o2(4 * B); for (int b = 0; b < 4; ++b) c.process(x.data() + b * B, o2.data() + b * B);
    float w2 = 0; for (int n = 2 * B; n < 4 * B; ++n) w2 = std::max(w2, std::fabs(o2[n] - x[n - 5]));
    CHECK(w2 < 1e-4f, "kernel swap %g", w2);
}

static void testTyped() {
    CHECK(typedChar('a', true) == 'A' && typedChar('a', false) == 'a' && typedChar('1', true) == '!' && typedChar(13, false) == 0, "typedChar");
}

int main() {
    testPresets(); testUndo(); testHalfband(); testFft(); testConvolver(); testTyped();
    std::printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails ? 1 : 0;
}
