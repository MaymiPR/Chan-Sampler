"""Schroeder comb and allpass sections, and reverb type A built from them as
fitted to FL's impulse response (the plugin's version: Source/SampleData.cpp)."""
import numpy as np
def ap(x, D, g):
    """Schroeder allpass: y[n] = -g x[n] + x[n-D] + g y[n-D] (block-wise, exact)."""
    y = np.zeros(len(x))
    for s in range(0, len(x), D):
        e = min(len(x), s + D)
        xd = x[s - D:e - D] if s >= D else np.zeros(e - s)
        yd = y[s - D:e - D] if s >= D else np.zeros(e - s)
        y[s:e] = -g * x[s:e] + xd + g * yd
    return y

def comb(x, D, g):
    y = np.zeros(len(x))
    for s in range(0, len(x), D):
        e = min(len(x), s + D)
        y[s:e] = x[s:e] + (g * y[s - D:e - D] if s >= D else 0)
    return y

def reverbA(x, mix, fs=44100.0):
    t60 = mix / 256.0 * fs
    acc = np.zeros(len(x))
    for D in (1777, 1847, 1993, 2137):
        acc += 0.15 * comb(x, D, 10 ** (-3 * D / t60))
    for D in (43, 127, 389):
        acc = ap(acc, D, 0.7)
    return 0.7 * x + np.convolve(acc, [.5, 1, .5])[:len(x)]
