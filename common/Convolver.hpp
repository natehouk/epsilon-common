// epsilon-common — uniformly partitioned FFT convolution (overlap-save), one channel.
// process() works on whole blocks, so a caller feeding it sample by sample adds one block of latency. Kernel changes cross-fade over one block, so they are click-free.
// Not thread-safe: call setKernel() and process() from the same thread (e.g. at block boundaries).
// License: MIT.
#pragma once
#include "Fft.hpp"
#include <algorithm>
#include <vector>

namespace epsilon {

class Convolver {
public:
    // block: samples per process() call (power of two); maxLen: longest kernel that will be set
    void init(int block, int maxLen) {
        B = block; N = 2 * B; P = std::max(1, (maxLen + B - 1) / B);
        fft.init(N);
        for (int s = 0; s < 2; ++s) part[s].assign((size_t)P * N, cpx(0, 0));
        fdl.assign((size_t)P * N, cpx(0, 0));
        hist.assign(N, 0.f); tmp.assign(N, cpx(0, 0)); acc.assign(N, cpx(0, 0)); acc2.assign(N, cpx(0, 0));
        cur = 0; pending = false; head = 0; used[0] = used[1] = 0;
    }
    int block() const { return B; }
    void reset() { std::fill(fdl.begin(), fdl.end(), cpx(0, 0)); std::fill(hist.begin(), hist.end(), 0.f); }

    // takes effect at the next process() call, cross-fading from the previous kernel
    void setKernel(const float* h, int len) {
        const int s = 1 - cur;
        len = std::min(len, P * B);
        used[s] = (len + B - 1) / B;
        for (int p = 0; p < used[s]; ++p) {
            for (int i = 0; i < N; ++i) tmp[i] = cpx(0, 0);
            for (int i = 0; i < B && p * B + i < len; ++i) tmp[i] = cpx(h[p * B + i], 0.f);
            fft.run(tmp.data(), false);
            std::copy(tmp.begin(), tmp.end(), part[s].begin() + (size_t)p * N);
        }
        if (!primed) { cur = s; primed = true; pending = false; } else pending = true;
    }

    // in and out may be the same buffer; exactly block() samples
    void process(const float* in, float* out) {
        // slide the input history: [previous block | this block]
        std::copy(hist.begin() + B, hist.end(), hist.begin());
        std::copy(in, in + B, hist.begin() + B);
        for (int i = 0; i < N; ++i) tmp[i] = cpx(hist[i], 0.f);
        fft.run(tmp.data(), false);
        head = (head + P - 1) % P;
        std::copy(tmp.begin(), tmp.end(), fdl.begin() + (size_t)head * N);
        if (!primed) { std::fill(out, out + B, 0.f); return; }
        mac(cur, acc);
        if (pending) {
            const int nx = 1 - cur;
            mac(nx, acc2);
            for (int i = 0; i < B; ++i) { const float t = (i + 0.5f) / B; out[i] = acc[B + i].real() * (1.f - t) + acc2[B + i].real() * t; }
            cur = nx; pending = false;
        } else {
            for (int i = 0; i < B; ++i) out[i] = acc[B + i].real();
        }
    }

private:
    void mac(int s, std::vector<cpx>& a) {
        std::fill(a.begin(), a.end(), cpx(0, 0));
        for (int p = 0; p < used[s]; ++p) {
            const cpx* x = &fdl[(size_t)((head + p) % P) * N];
            const cpx* k = &part[s][(size_t)p * N];
            for (int i = 0; i < N; ++i) a[i] += x[i] * k[i];
        }
        fft.run(a.data(), true);
    }
    int B = 0, N = 0, P = 0, cur = 0, head = 0, used[2] = { 0, 0 };
    bool pending = false, primed = false;
    Fft fft;
    std::vector<cpx> part[2], fdl, tmp, acc, acc2;
    std::vector<float> hist;
};

} // namespace epsilon
