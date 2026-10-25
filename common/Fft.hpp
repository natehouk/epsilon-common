// epsilon-common — small iterative radix-2 complex FFT (power-of-two sizes), for analyzers and FFT convolution.
// License: MIT.
#pragma once
#include <cmath>
#include <complex>
#include <vector>

namespace epsilon {

typedef std::complex<float> cpx;
class Fft {
public:
    void init(int n) {
        if (n == size) return;
        size = n; twr.resize(n / 2); twi.resize(n / 2); rev.resize(n);
        for (int i = 0; i < n / 2; ++i) { const double a = -2.0 * 3.14159265358979323846 * i / n; twr[i] = (float)std::cos(a); twi[i] = (float)std::sin(a); }
        int bits = 0; while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[i] = r; }
    }
    int n() const { return size; }
    // inverse: conjugate twiddles, scaled by 1/n
    void run(cpx* xc, bool inverse) const {
        float* x = reinterpret_cast<float*>(xc);
        for (int i = 0; i < size; ++i) if (rev[i] > i) std::swap(xc[i], xc[rev[i]]);
        const float sg = inverse ? -1.f : 1.f;
        for (int len = 2; len <= size; len <<= 1) {
            const int half = len >> 1, step = size / len;
            for (int i = 0; i < size; i += len)
                for (int j = 0; j < half; ++j) {
                    const float wr = twr[j * step], wi = sg * twi[j * step];
                    float* u = x + 2 * (i + j); float* v = x + 2 * (i + j + half);
                    const float vr = v[0] * wr - v[1] * wi, vi = v[0] * wi + v[1] * wr;
                    v[0] = u[0] - vr; v[1] = u[1] - vi; u[0] += vr; u[1] += vi;
                }
        }
        if (inverse) { const float s = 1.f / size; for (int i = 0; i < 2 * size; ++i) x[i] *= s; }
    }
private:
    int size = 0;
    std::vector<float> twr, twi;
    std::vector<int> rev;
};

} // namespace epsilon
