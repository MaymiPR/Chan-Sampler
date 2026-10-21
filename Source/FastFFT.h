#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <complex>
#include <memory>
#include <mutex>
#include <vector>

// A complex FFT for powers of two (radix-4 Stockham autosort on split real /
// imaginary arrays, SIMD butterflies), a drop-in for juce::dsp::FFT::perform:
// forward e^-i, inverse scaled by 1 / N, same results to about -130 dB.
// JUCE's fallback FFT (Windows, Linux) is 3-3.6x slower at 256..8192 points,
// and the stretch modes spend most of their time in FFTs.
//
// The twiddle tables are shared by every instance of an order (built once,
// read-only); the working arrays come from a Scratch the caller owns, which
// can serve several FFTs used one after another.
class FastFFT
{
public:
    struct Buffer
    {
        void resize (size_t size)
        {
            block.allocate (size + 16, true);
            data_ = reinterpret_cast<float*> ((reinterpret_cast<uintptr_t> (block.get()) + 63) & ~(uintptr_t) 63);
        }
        float* data() const noexcept { return data_; }
        float& operator[] (size_t i) const noexcept { return data_[i]; }
        juce::HeapBlock<float> block;
        float* data_ = nullptr;
    };

    struct Scratch
    {
        explicit Scratch (int maxOrder) { for (auto& b : buf) b.resize ((size_t) 1 << maxOrder); }
        Buffer buf[4];
    };

    FastFFT (int order, Scratch& s) : n (1 << order), t (tables (order)), scratch (s) {}

    int getSize() const noexcept { return n; }

    void perform (const std::complex<float>* in, std::complex<float>* out, bool inverse) const noexcept
    {
        const float sign = inverse ? -1.0f : 1.0f;   // inverse: conj in, conj out
        float* xr = scratch.buf[0].data(); float* xi = scratch.buf[1].data();
        float* yr = scratch.buf[2].data(); float* yi = scratch.buf[3].data();
        for (int i = 0; i < n; ++i)
        {
            xr[i] = in[i].real();
            xi[i] = sign * in[i].imag();
        }
        int s = 1;
        for (const auto& st : t.stages)
        {
            radix4 (st, s, xr, xi, yr, yi);
            std::swap (xr, yr);
            std::swap (xi, yi);
            s *= 4;
        }
        if (s < n)   // a final radix-2 stage (odd orders)
        {
            for (int q = 0; q < s; ++q)
            {
                const float ar = xr[q], ai = xi[q], br = xr[q + s], bi = xi[q + s];
                yr[q] = ar + br; yi[q] = ai + bi;
                yr[q + s] = ar - br; yi[q + s] = ai - bi;
            }
            std::swap (xr, yr);
            std::swap (xi, yi);
        }
        const float scale = inverse ? 1.0f / (float) n : 1.0f;
        for (int i = 0; i < n; ++i)
            out[i] = { xr[i] * scale, sign * xi[i] * scale };
    }

private:
    using V = juce::dsp::SIMDRegister<float>;
    struct Stage { int len, twOffset; };

    struct Tables
    {
        explicit Tables (int order)
        {
            const int size = 1 << order;
            // Per radix-4 stage of length L, for p < L / 4: w^p, w^2p, w^3p
            // (w = e^(-2 pi i / L)), interleaved.
            for (int len = size; len >= 4; len /= 4)
            {
                stages.push_back ({ len, (int) tw.size() });
                for (int p = 0; p < len / 4; ++p)
                    for (int k = 1; k <= 3; ++k)
                    {
                        const double a = -2.0 * juce::MathConstants<double>::pi * (double) (k * p) / (double) len;
                        tw.push_back ((float) std::cos (a));
                        tw.push_back ((float) std::sin (a));
                    }
            }
            // The first stage's twiddles by kind, for loads across p.
            const int m = size / 4;
            for (int k = 0; k < 6; ++k)
            {
                first[k].resize ((size_t) juce::jmax (1, m));
                for (int p = 0; p < m; ++p)
                    first[k][(size_t) p] = tw[(size_t) (6 * p + k)];
            }
        }
        std::vector<Stage> stages;
        std::vector<float> tw;
        Buffer first[6];
    };

    static const Tables& tables (int order)
    {
        static std::mutex lock;
        static std::array<std::unique_ptr<Tables>, 16> all;
        const std::lock_guard<std::mutex> guard (lock);
        auto& t = all[(size_t) order];
        if (t == nullptr)
            t = std::make_unique<Tables> (order);
        return *t;
    }

    // One Stockham radix-4 step: x holds s interleaved transforms of length
    // len (element q + s j), y receives 4 s transforms of length len / 4.
    void radix4 (const Stage& st, int s, const float* __restrict xr, const float* __restrict xi,
                 float* __restrict yr, float* __restrict yi) const noexcept
    {
        const int m = st.len / 4;
        constexpr int L = (int) V::size();
        if (s == 1 && m >= L)
        {
            // The first stage: L butterflies at once across p, the results
            // transposed into place (y[4 p + k]).
            alignas (64) float o[8][L];
            for (int p = 0; p < m; p += L)
            {
                const auto Ar = V::fromRawArray (xr + p), Ai = V::fromRawArray (xi + p);
                const auto Br = V::fromRawArray (xr + p + m), Bi = V::fromRawArray (xi + p + m);
                const auto Cr = V::fromRawArray (xr + p + 2 * m), Ci = V::fromRawArray (xi + p + 2 * m);
                const auto Dr = V::fromRawArray (xr + p + 3 * m), Di = V::fromRawArray (xi + p + 3 * m);
                const auto W1r = V::fromRawArray (t.first[0].data() + p), W1i = V::fromRawArray (t.first[1].data() + p);
                const auto W2r = V::fromRawArray (t.first[2].data() + p), W2i = V::fromRawArray (t.first[3].data() + p);
                const auto W3r = V::fromRawArray (t.first[4].data() + p), W3i = V::fromRawArray (t.first[5].data() + p);
                const auto apcR = Ar + Cr, apcI = Ai + Ci, amcR = Ar - Cr, amcI = Ai - Ci;
                const auto bpdR = Br + Dr, bpdI = Bi + Di, jR = Bi - Di, jI = Dr - Br;   // -i (b - d)
                const auto t1r = amcR + jR, t1i = amcI + jI, t2r = apcR - bpdR, t2i = apcI - bpdI, t3r = amcR - jR, t3i = amcI - jI;
                (apcR + bpdR).copyToRawArray (o[0]);            (apcI + bpdI).copyToRawArray (o[1]);
                (t1r * W1r - t1i * W1i).copyToRawArray (o[2]);  (t1r * W1i + t1i * W1r).copyToRawArray (o[3]);
                (t2r * W2r - t2i * W2i).copyToRawArray (o[4]);  (t2r * W2i + t2i * W2r).copyToRawArray (o[5]);
                (t3r * W3r - t3i * W3i).copyToRawArray (o[6]);  (t3r * W3i + t3i * W3r).copyToRawArray (o[7]);
                for (int j = 0; j < L; ++j)
                    for (int k = 0; k < 4; ++k)
                    {
                        yr[4 * (p + j) + k] = o[2 * k][j];
                        yi[4 * (p + j) + k] = o[2 * k + 1][j];
                    }
            }
            return;
        }

        const float* w = t.tw.data() + st.twOffset;
        for (int p = 0; p < m; ++p, w += 6)
        {
            const float w1r = w[0], w1i = w[1], w2r = w[2], w2i = w[3], w3r = w[4], w3i = w[5];
            const float* ar = xr + s * p;            const float* ai = xi + s * p;
            const float* br = xr + s * (p + m);      const float* bi = xi + s * (p + m);
            const float* cr = xr + s * (p + 2 * m);  const float* ci = xi + s * (p + 2 * m);
            const float* dr = xr + s * (p + 3 * m);  const float* di = xi + s * (p + 3 * m);
            float* y0r = yr + s * (4 * p);      float* y0i = yi + s * (4 * p);
            float* y1r = y0r + s;               float* y1i = y0i + s;
            float* y2r = y1r + s;               float* y2i = y1i + s;
            float* y3r = y2r + s;               float* y3i = y2i + s;
            if (s >= L)
            {
                const V W1r (w1r), W1i (w1i), W2r (w2r), W2i (w2i), W3r (w3r), W3i (w3i);
                for (int q = 0; q < s; q += L)
                {
                    const auto Ar = V::fromRawArray (ar + q), Ai = V::fromRawArray (ai + q);
                    const auto Br = V::fromRawArray (br + q), Bi = V::fromRawArray (bi + q);
                    const auto Cr = V::fromRawArray (cr + q), Ci = V::fromRawArray (ci + q);
                    const auto Dr = V::fromRawArray (dr + q), Di = V::fromRawArray (di + q);
                    const auto apcR = Ar + Cr, apcI = Ai + Ci, amcR = Ar - Cr, amcI = Ai - Ci;
                    const auto bpdR = Br + Dr, bpdI = Bi + Di, jR = Bi - Di, jI = Dr - Br;
                    (apcR + bpdR).copyToRawArray (y0r + q);
                    (apcI + bpdI).copyToRawArray (y0i + q);
                    const auto t1r = amcR + jR, t1i = amcI + jI, t2r = apcR - bpdR, t2i = apcI - bpdI, t3r = amcR - jR, t3i = amcI - jI;
                    (t1r * W1r - t1i * W1i).copyToRawArray (y1r + q);
                    (t1r * W1i + t1i * W1r).copyToRawArray (y1i + q);
                    (t2r * W2r - t2i * W2i).copyToRawArray (y2r + q);
                    (t2r * W2i + t2i * W2r).copyToRawArray (y2i + q);
                    (t3r * W3r - t3i * W3i).copyToRawArray (y3r + q);
                    (t3r * W3i + t3i * W3r).copyToRawArray (y3i + q);
                }
                continue;
            }
            for (int q = 0; q < s; ++q)
            {
                const float apcR = ar[q] + cr[q], apcI = ai[q] + ci[q];
                const float amcR = ar[q] - cr[q], amcI = ai[q] - ci[q];
                const float bpdR = br[q] + dr[q], bpdI = bi[q] + di[q];
                const float jR = bi[q] - di[q], jI = dr[q] - br[q];
                y0r[q] = apcR + bpdR;                y0i[q] = apcI + bpdI;
                const float t1r = amcR + jR, t1i = amcI + jI;
                const float t2r = apcR - bpdR, t2i = apcI - bpdI;
                const float t3r = amcR - jR, t3i = amcI - jI;
                y1r[q] = t1r * w1r - t1i * w1i;      y1i[q] = t1r * w1i + t1i * w1r;
                y2r[q] = t2r * w2r - t2i * w2i;      y2i[q] = t2r * w2i + t2i * w2r;
                y3r[q] = t3r * w3r - t3i * w3i;      y3i[q] = t3r * w3i + t3i * w3r;
            }
        }
    }

    int n;
    const Tables& t;
    Scratch& scratch;
};
