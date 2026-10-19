"""Pattern self-similarity: how well the multi-band onset pattern repeats at a given lag (e.g. one bar)."""
import numpy as np, librosa
from onsets import HOP

def band_odf(m, sr):
    X = np.abs(librosa.stft(m.astype(np.float32), n_fft=1024, hop_length=HOP))
    f = librosa.fft_frequencies(sr=sr, n_fft=1024)
    out = []
    for lo, hi in [(20, 150), (150, 1500), (1500, 6000), (6000, 20000)]:
        b = np.log1p(1000 * X[(f >= lo) & (f < hi)].sum(0) / (X.sum(0).max() + 1e-9))
        d = np.maximum(0, np.diff(b, prepend=b[0]))
        out.append(d / (d.max() + 1e-9))
    return np.array(out)

def _cos(B, L):
    n = B.shape[1]
    a = B[:, :n - L].ravel(); b = B[:, L:].ravel()
    return (a * b).sum() / (np.sqrt((a * a).sum() * (b * b).sum()) + 1e-9)

def selfsim_at(B, sr, lag_s, tol=0.025):
    """best similarity within +-tol of lag_s, or None if the lag is too long for the file"""
    n = B.shape[1]
    vals = [_cos(B, L) for L in range(int(lag_s * (1 - tol) * sr / HOP), int(lag_s * (1 + tol) * sr / HOP) + 1)
            if 0 < L < 0.85 * n]
    return max(vals) if vals else None

def selfsim_baseline(B, sr):
    n = B.shape[1]
    return float(np.median([_cos(B, L) for L in np.linspace(int(0.12 * sr / HOP), int(0.8 * n), 60).astype(int)]))
