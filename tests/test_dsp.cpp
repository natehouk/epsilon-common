// Oversampler chain (2x-32x), loudness meter (EBU Tech 3341 cases, gating, true peak, LRA), character stage.
#include "../common/Character.hpp"
#include "../common/Halfband.hpp"
#include "../common/Loudness.hpp"
#include <cstdio>
#include <vector>

using namespace epsilon;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const double PI = 3.14159265358979323846;

static void testOversampler() {
    // up to 4x it is the same as Oversampler
    for (int os : { 1, 2, 4 }) {
        Oversampler a; OversamplerN b; a.reset(); b.reset(); float ya[32], yb[32]; float worst = 0;
        for (int i = 0; i < 2000; ++i) {
            const float x = (float)std::sin(0.05 * i) * 0.7f + (i % 97 == 0 ? 0.3f : 0.f);
            a.up(x, os, ya); b.up(x, os, yb);
            for (int k = 0; k < os; ++k) worst = std::max(worst, std::fabs(ya[k] - yb[k]));
            worst = std::max(worst, std::fabs(a.down(ya, os) - b.down(yb, os)));
        }
        CHECK(worst < 1e-6f, "os%d matches Oversampler: %g", os, worst);
    }
    // 8x-32x: transparent round trip in the passband, with the stated latency
    for (int os : { 8, 16, 32 }) {
        OversamplerN o; o.reset(); float y[32];
        const int N = 16384, lat = OversamplerN::latency(os);
        std::vector<float> in(N), out(N);
        for (int i = 0; i < N; ++i) { in[i] = (float)std::sin(2 * PI * 0.1 * i); o.up(in[i], os, y); out[i] = o.down(y, os); }
        double num = 0, den = 0, err = 0;
        for (int i = 4096; i < N; ++i) { num += out[i] * out[i]; den += in[i - lat] * in[i - lat]; err += (out[i] - in[i - lat]) * (out[i] - in[i - lat]); }
        CHECK(std::fabs(10 * std::log10(num / den)) < 0.05 && 10 * std::log10(err / den) < -40, "os%d: gain %.3f dB, error %.1f dB", os, 10 * std::log10(num / den), 10 * std::log10(err / den));
        // the up-sampled signal has no image above the base Nyquist
        OversamplerN q; q.reset(); std::vector<float> hi; for (int i = 0; i < 4096; ++i) { q.up((float)std::sin(2 * PI * 0.2 * i), os, y); for (int k = 0; k < os; ++k) hi.push_back(y[k]); }
        double im = 0, pw = 0; const double fImg = (1.0 - 0.2) / os;     // first image, as a fraction of the high rate
        double re = 0, ii = 0, re2 = 0, ii2 = 0;
        for (size_t i = hi.size() / 2; i < hi.size(); ++i) { re += hi[i] * std::cos(2 * PI * fImg * i); ii += hi[i] * std::sin(2 * PI * fImg * i); re2 += hi[i] * std::cos(2 * PI * 0.2 / os * i); ii2 += hi[i] * std::sin(2 * PI * 0.2 / os * i); }
        im = re * re + ii * ii; pw = re2 * re2 + ii2 * ii2;
        CHECK(10 * std::log10(im / pw) < -60, "os%d image %.1f dB", os, 10 * std::log10(im / pw));
    }
    CHECK(OversamplerN::latency(32) == 62 && OversamplerN::latency(8) == 56, "latencies");
}

static void sineBoth(LoudnessMeter& m, double fs, double f, double dbfs, double secs) {
    const int n = (int)(fs * secs); std::vector<float> x(n); const double a = std::pow(10.0, dbfs / 20.0);
    static double ph = 0;
    for (int i = 0; i < n; ++i) { x[i] = (float)(a * std::sin(ph)); ph += 2 * PI * f / fs; }
    for (int i = 0; i < n; i += 512) { const int k = std::min(512, n - i); m.process(x.data() + i, x.data() + i, (uint32_t)k); }
}
static void testLoudness() {
    for (double fs : { 44100.0, 48000.0, 96000.0 }) {
        LoudnessMeter m; m.setSampleRate((float)fs);
        sineBoth(m, fs, 1000, -23, 20);
        CHECK(std::fabs(m.momentary() + 23) < 0.1f && std::fabs(m.shortTerm() + 23) < 0.1f && std::fabs(m.integrated() + 23) < 0.1f,
              "%g Hz: 1 kHz at -23 dBFS reads M %.2f S %.2f I %.2f LUFS (want -23.0)", fs, m.momentary(), m.shortTerm(), m.integrated());
        // EBU 3341 case 3: -36 / -23 / -36 dBFS for 10 / 60 / 10 s: integrated -23.0 (the quiet parts are gated out)
        m.reset(); sineBoth(m, fs, 1000, -36, 10); sineBoth(m, fs, 1000, -23, 60); sineBoth(m, fs, 1000, -36, 10);
        CHECK(std::fabs(m.integrated() + 23) < 0.1f, "%g Hz: gated integrated %.2f LUFS (want -23.0)", fs, m.integrated());
        // case 5: -26 / -20 / -26, 20 s + 20.1 s + 20 s: -23.0
        m.reset(); sineBoth(m, fs, 1000, -26, 20); sineBoth(m, fs, 1000, -20, 20.1); sineBoth(m, fs, 1000, -26, 20);
        CHECK(std::fabs(m.integrated() + 23) < 0.1f, "%g Hz: case 5 integrated %.2f LUFS", fs, m.integrated());
        // LRA (EBU 3342 case 1): -20 then -30 dBFS, 20 s each -> 10 LU
        m.reset(); sineBoth(m, fs, 1000, -20, 20); sineBoth(m, fs, 1000, -30, 20);
        CHECK(std::fabs(m.range() - 10) < 1.f, "%g Hz: LRA %.2f LU (want 10)", fs, m.range());
    }
    // true peak: fs/4 sine at 45 degrees phase has sample peaks of 0.707 but a true peak of 1.0
    LoudnessMeter t; t.setSampleRate(48000);
    std::vector<float> x(48000); for (int i = 0; i < 48000; ++i) x[i] = (float)std::sin(PI / 2 * i + PI / 4);
    t.process(x.data(), x.data(), 48000);
    CHECK(std::fabs(t.samplePeakDb() + 3.01f) < 0.05f && std::fabs(t.truePeakDb()) < 0.3f, "sample peak %.2f dB, true peak %.2f dBTP", t.samplePeakDb(), t.truePeakDb());
    // K-weighting relative to 1 kHz, from the BS.1770 48 kHz reference coefficients evaluated directly
    auto ref = [](double f) {
        const double b1[3] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 }, a1[3] = { 1, -1.69065929318241, 0.73248077421585 };
        const double b2[3] = { 1, -2, 1 }, a2[3] = { 1, -1.99004745483398, 0.99007225036621 };
        auto h = [&](const double* b, const double* a) { const double w = 2 * PI * f / 48000; double nr = 0, ni = 0, dr = 0, di = 0;
            for (int k = 0; k < 3; ++k) { nr += b[k] * std::cos(-w * k); ni += b[k] * std::sin(-w * k); dr += a[k] * std::cos(-w * k); di += a[k] * std::sin(-w * k); }
            return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di)); };
        return 20 * std::log10(h(b1, a1) * h(b2, a2));
    };
    auto lvl = [](double f) { LoudnessMeter q; q.setSampleRate(48000); sineBoth(q, 48000, f, -23, 5); return q.shortTerm() + 23; };
    float worst = 0;
    for (double f : { 30.0, 60.0, 200.0, 3000.0, 10000.0, 18000.0 }) worst = std::max(worst, (float)std::fabs(lvl(f) - (ref(f) - ref(1000))));
    CHECK(worst < 0.05f, "K-weighting differs from the BS.1770 reference by %.3f dB", worst);
}

static double thd(Character& ch, double amp) {
    const int N = 48000; double re = 0, im = 0, tot = 0;
    for (int i = 0; i < N; ++i) { ch.tick(); const float y = ch.process((float)(amp * std::sin(2 * PI * 1000.0 * i / 48000.0)), 0);
        if (i >= N / 2) { re += y * std::cos(2 * PI * 1000.0 * i / 48000.0); im += y * std::sin(2 * PI * 1000.0 * i / 48000.0); tot += (double)y * y; } }
    const int M = N / 2; const double a = 2 * std::sqrt(re * re + im * im) / M, fund = a * a / 2, all = tot / M;
    return 10 * std::log10(std::max(1e-20, all - fund) / fund);
}
static void testCharacter() {
    Character off; off.setup(48000, Character::kOff, 0.5f); off.reset();
    CHECK(off.process(0.3f, 0) == 0.3f, "off is transparent");
    for (int mode : { Character::kWarm, Character::kPunchy, Character::kVintage }) {
        Character c; c.setup(48000, mode, 0.f); c.reset();
        // small signals pass at unity
        double sum = 0; for (int i = 0; i < 48000; ++i) { c.tick(); const float y = c.process((float)(0.001 * std::sin(2 * PI * 1000.0 * i / 48000.0)), 0); if (i > 24000) sum += y * y; }
        const double g = 10 * std::log10(sum / 24000 / (0.001 * 0.001 / 2));
        CHECK(std::fabs(g) < (mode == Character::kVintage ? 0.5 : 0.05), "mode %d small-signal gain %.3f dB", mode, g);
        Character lo, hi; lo.setup(48000, mode, 0.f); hi.setup(48000, mode, 1.f); lo.reset(); hi.reset();
        const double tl = thd(lo, 0.3), th = thd(hi, 0.3);
        CHECK(th > tl + 6 && th > -40, "mode %d distortion: drive 0 %.1f dB, drive 1 %.1f dB", mode, tl, th);
    }
    // drift stays within [-1, 1] and moves
    Character v; v.setup(48000, Character::kVintage, 0.5f); v.reset(); float mn = 1, mx = -1;
    for (int i = 0; i < 48000 * 10; ++i) { v.tick(); mn = std::min(mn, v.drift(0)); mx = std::max(mx, v.drift(0)); }
    CHECK(mn >= -1 && mx <= 1 && mx - mn > 0.2f, "drift range %.2f .. %.2f", mn, mx);
}

int main() {
    testOversampler(); testLoudness(); testCharacter();
    std::printf(fails ? "%d FAILED\n" : "all dsp tests passed\n", fails);
    return fails ? 1 : 0;
}
