import numpy as np
def transfer(y, x, sr=44100, n=4096):
    """Averaged transfer function H = Sxy / Sxx (left channel)."""
    m = min(len(x), len(y)) - n
    w = np.hanning(n); Sxy = 0; Sxx = 0
    for i in range(0, m, n // 2):
        X = np.fft.rfft(w * x[i:i+n]); Y = np.fft.rfft(w * y[i:i+n])
        Sxy = Sxy + Y * np.conj(X); Sxx = Sxx + np.abs(X) ** 2
    return np.fft.rfftfreq(n, 1 / sr), Sxy / Sxx
