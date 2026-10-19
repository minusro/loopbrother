"""Onset detection: spectral-flux peaks refined to sample-accurate attack starts."""
import numpy as np, librosa

HOP = 128

def odf(m, sr):
    env = librosa.onset.onset_strength(y=m.astype(np.float32), sr=sr, hop_length=HOP, n_fft=1024, lag=2, max_size=3)
    return env / (env.max() + 1e-12)

def envelope(x, sr, win_ms=0.5):
    n = max(1, int(sr * win_ms / 1000))
    return np.sqrt(np.convolve(x ** 2, np.ones(n) / n, mode='same'))

def refine_onset(t_frame, e, sr):
    """(attack sample, peak level) for an onset-function peak at t_frame: walk back from the
    envelope peak to 8 % of the rise"""
    c = int(t_frame * sr)
    a, b = max(0, c - int(0.030 * sr)), min(len(e), c + int(0.015 * sr))
    if b <= a: return c, 0.0
    ip = a + int(np.argmax(e[a:b])); emax = e[ip]
    pa = max(0, a - int(0.010 * sr))
    ebase = float(np.min(e[pa:ip + 1])) if ip >= pa else 0.0
    thr = ebase + 0.08 * (emax - ebase)
    i = ip
    while i > a - int(0.010 * sr) and i > 0 and e[i] > thr:
        i -= 1
    return i, float(emax)

def detect(y, sr):
    """returns attack samples S, onset strengths F (0..1), peak levels A, the onset function and the 0.5 ms envelope"""
    env = odf(y.mean(1), sr)
    w = int(0.03 * sr / HOP)
    pk = librosa.util.peak_pick(env, pre_max=w, post_max=w, pre_avg=int(0.1 * sr / HOP), post_avg=int(0.1 * sr / HOP), delta=0.04, wait=1)
    # merge peaks closer than 35 ms, keeping the stronger one
    kept = []
    for k in pk:
        if kept and (k - kept[-1]) * HOP / sr < 0.035:
            if env[k] > env[kept[-1]]: kept[-1] = k
            continue
        kept.append(k)
    pk = np.array(kept, dtype=int)
    pk = pk[env[pk] > 0.05]
    e = envelope(np.abs(y).max(1), sr)
    ons = sorted((*refine_onset(k * HOP / sr, e, sr), env[k]) for k in pk)
    # merge onsets that refine to within 15 ms of each other
    out = []
    for s, amp, f in ons:
        if out and s - out[-1][0] < int(0.015 * sr):
            if f > out[-1][1]: out[-1] = (out[-1][0], f, max(amp, out[-1][2]))
            continue
        out.append((s, f, amp))
    S = np.array([o[0] for o in out]); F = np.array([o[1] for o in out]); A = np.array([o[2] for o in out])
    return S, F, A, env, e
