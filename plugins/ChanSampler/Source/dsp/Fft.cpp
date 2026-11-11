#include "Fft.h"
#include "Dsp.h"

#include <cmath>

namespace cs::dsp
{
Fft::Fft (int order) : n (1 << order), twiddle ((size_t) 1 << order)
{
    // e^(-2 pi i k / n): the first quarter computed, the second quarter rotated from it, the upper half mirrored
    const double step = -2.0 * MathConstants<double>::pi / (double) n;
    if (n <= 4)
    {
        for (int k = 0; k < n; ++k)
            twiddle[(size_t) k] = { (float) std::cos (k * step), (float) std::sin (k * step) };
    }
    else
    {
        const int quarter = n / 4, half = n / 2;
        for (int k = 0; k < quarter; ++k)
            twiddle[(size_t) k] = { (float) std::cos (k * step), (float) std::sin (k * step) };
        for (int k = quarter; k < half; ++k)
        {
            const Complex w = twiddle[(size_t) (k - quarter)];
            twiddle[(size_t) k] = { w.imag(), -w.real() };
        }
        twiddle[(size_t) half] = { -1.0f, 0.0f };
        for (int k = half; k < n; ++k)
            twiddle[(size_t) k] = std::conj (twiddle[(size_t) (n - k)]);
    }

    // radix-4 stages while the size divides by four, then one radix-2 stage
    for (int left = n; left > 1;)
    {
        const int radix = left % 4 == 0 ? 4 : 2;
        left /= radix;
        stages.push_back ({ radix, left });
    }
}

void Fft::run (const Complex* in, Complex* out) const
{
    if (n == 1) { out[0] = in[0]; return; }
    stage (in, out, 1, 0);
}

void Fft::stage (const Complex* in, Complex* out, int stride, size_t level) const
{
    // the sub-transforms of every radix-th input, laid one after another in out, then joined by one butterfly pass
    const Stage s = stages[level];
    if (s.length == 1)
    {
        for (int k = 0; k < s.radix; ++k) out[k] = in[k * stride];
    }
    else
    {
        for (int k = 0; k < s.radix; ++k) stage (in + k * stride, out + k * s.length, stride * s.radix, level + 1);
    }
    if (s.radix == 4) radix4 (out, stride, s.length);
    else radix2 (out, stride, s.length);
}

void Fft::radix2 (Complex* data, int stride, int length) const
{
    for (int i = 0; i < length; ++i)
    {
        Complex t = data[i + length];
        t *= twiddle[(size_t) (i * stride)];
        data[i + length] = data[i] - t;
        data[i] += t;
    }
}

void Fft::radix4 (Complex* data, int stride, int length) const
{
    Complex* d0 = data;
    Complex* d1 = data + length;
    Complex* d2 = data + 2 * length;
    Complex* d3 = data + 3 * length;
    for (int i = 0; i < length; ++i)
    {
        const Complex a = d1[i] * twiddle[(size_t) (i * stride)];
        const Complex b = d2[i] * twiddle[(size_t) (2 * i * stride)];
        const Complex c = d3[i] * twiddle[(size_t) (3 * i * stride)];
        Complex sum = a;   sum += c;
        Complex diff = a;  diff -= c;
        Complex low = d0[i]; low -= b;
        d0[i] += b;
        d2[i] = d0[i];
        d2[i] -= sum;
        d0[i] += sum;
        d1[i] = { low.real() + diff.imag(), low.imag() - diff.real() };
        d3[i] = { low.real() - diff.imag(), low.imag() + diff.real() };
    }
}

void Fft::forwardMagnitudes (float* data) const
{
    if (n == 1)
        return;
    std::vector<Complex> in ((size_t) n);
    for (int i = 0; i < n; ++i) in[(size_t) i] = { data[i], 0.0f };
    auto* bins = reinterpret_cast<Complex*> (data);
    run (in.data(), bins);
    const int limit = n / 2 + 1;
    for (int i = 0; i < limit; ++i)
        data[i] = std::abs (bins[i]);
    for (int i = limit; i < 2 * n; ++i)
        data[i] = 0.0f;
}
}
