#pragma once
#include <complex>
#include <vector>

// Detect tempo's FFT: a power-of-two Cooley-Tukey transform, decimation in time, in radix-4 stages with one radix-2
// stage when the size is not a power of four. The twiddles come from a quarter-wave table by symmetry, and every
// butterfly keeps a fixed order of operations, so a tempo estimate is the same on every build and platform.
namespace cs::dsp
{
class Fft
{
public:
    explicit Fft (int order);                    // size 2^order
    int size() const noexcept { return n; }

    // data holds 2 * size() floats: size() real samples in; the magnitudes of the bins 0 .. size() / 2 out, the rest
    // of data cleared.
    void forwardMagnitudes (float* data) const;

private:
    using Complex = std::complex<float>;
    struct Stage { int radix, length; };

    void run (const Complex* in, Complex* out) const;
    void stage (const Complex* in, Complex* out, int stride, size_t level) const;
    void radix2 (Complex* data, int stride, int length) const;
    void radix4 (Complex* data, int stride, int length) const;

    int n;
    std::vector<Complex> twiddle;
    std::vector<Stage> stages;
};
}
