import numpy as np, kernel_extract as K
STEP = 512
def build():
    d, k = K.points()
    a = np.abs(d)
    grid = np.arange(0, 15 * STEP + 1) / STEP
    tab = np.zeros(len(grid))
    idx = np.round(a * STEP).astype(int)
    for g in range(len(grid)):
        pass
    # local linear regression in a +-1/STEP window
    o = np.argsort(a); a, k = a[o], k[o]
    lo = np.searchsorted(a, grid - 1.0 / STEP); hi = np.searchsorted(a, grid + 1.0 / STEP)
    for g in range(len(grid)):
        x = a[lo[g]:hi[g]] - grid[g]; y = k[lo[g]:hi[g]]
        if len(x) < 3: tab[g] = np.interp(grid[g], a, k); continue
        A = np.stack([np.ones_like(x), x], 1)
        tab[g] = np.linalg.lstsq(A, y, rcond=None)[0][0]
    tab[-1] = 0.0
    return grid, tab
def kern(tab, d):
    a = np.abs(d) * STEP
    i = np.floor(a).astype(int); t = a - i
    ok = i < len(tab) - 1
    out = np.zeros_like(a)
    out[ok] = tab[i[ok]] * (1 - t[ok]) + tab[i[ok] + 1] * t[ok]
    return out
if __name__ == '__main__':
    grid, tab = build()
    np.save('fl_kernel.npy', tab)
    d, k = K.points()
    print('fit residual (LSB) rms/max', np.sqrt(((kern(tab, d) - k) ** 2).mean()) * 13103, np.abs(kern(tab, d) - k).max() * 13103)
    print('k(0)', tab[0], 'sum over integers', sum(tab[i * STEP] for i in range(1, 15)) * 2 + tab[0])
