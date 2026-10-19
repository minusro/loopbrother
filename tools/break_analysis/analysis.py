"""Per-break analysis: tempo, first beat and a whole-bar crop.

Tempo comes from fitting straight / triplet beat grids to the detected hits, scored by grid fit,
bar-level pattern repetition and whether an existing cut in the file lands on a bar line.
"""
import os, numpy as np, soundfile as sf, librosa
from scipy.signal import find_peaks
from onsets import detect, HOP
from selfsim import band_odf, selfsim_at, selfsim_baseline

PRE_ROLL = 0.0005          # crops start this long before the attack
LO, HI = 85.0, 180.0       # preferred BPM range when the half/double tempo reading is ambiguous
GRIDS = {  # name -> (levels [(divisions per beat, weight)], subdivisions per beat for fitting, score penalty)
    'straight': ([(1, 1.0), (2, 0.85), (4, 0.6)], 4, 0.0),
    'triplet':  ([(1, 1.0), (3, 0.75), (6, 0.5)], 6, 0.08),
}

def level_score(rr, P, levels, sigma):
    bb = np.zeros(np.broadcast(rr, P).shape)
    for div, lw in levels:
        g = P / div
        d = rr - np.round(rr / g) * g
        bb = np.maximum(bb, lw * np.exp(-0.5 * (d / sigma) ** 2))
    return bb

def grid_curve(tau, w, bpms, levels, sigma=0.010):
    P = 60.0 / bpms
    best = np.zeros(len(P))
    divs = max(d for d, _ in levels)
    for ph in range(divs):
        for dj in np.arange(-0.008, 0.0081, 0.002):
            rr = (tau - tau[0])[None, :] + (ph / divs * P - dj)[:, None]
            best = np.maximum(best, (level_score(rr, P[:, None], levels, sigma) * w[None, :]).sum(1))
    return best / w.sum()

def refine(tau, w, P, anchor, div, iters=5):
    """weighted least-squares fit of beat period and grid anchor; off-8th hits count less (swing)"""
    for _ in range(iters):
        g = P / div
        q = np.round((tau - anchor) / g)
        r = tau - (anchor + q * g)
        inl = np.abs(r) < 0.3 * g
        ww = w * np.where(q % (div // 2) == 0, 1.0, 0.3) * inl
        if ww.sum() <= 0 or len(np.unique(q[inl])) < 2: break
        M = np.vstack([np.ones_like(q), q]).T; W = np.sqrt(ww)
        sol, *_ = np.linalg.lstsq(M * W[:, None], tau * W, rcond=None)
        anchor, P = sol[0], sol[1] * div
    return P, anchor

def fit_G(tau, w, P, anchor, levels, sigma=0.010):
    return float((level_score(tau - anchor, P, levels, sigma) * w).sum() / w.sum())

def rms_env(x, sr, ms):
    n = max(1, int(sr * ms / 1000))
    return np.sqrt(np.convolve(x ** 2, np.ones(n) / n, mode='same'))

def audio_extent(y, sr):
    """end of the meaningful audio (ignoring trailing clicks) and whether it ends in a hard cut"""
    env = rms_env(np.abs(y).max(1), sr, 5); pk = env.max()
    idx = np.where(env > pk * 10 ** (-45 / 20))[0]
    if len(idx) == 0: return len(y) / sr, False
    gaps = np.where(np.diff(idx) > int(0.1 * sr))[0]
    starts = np.concatenate([[idx[0]], idx[gaps + 1]]); ends = np.concatenate([idx[gaps], [idx[-1]]])
    k = len(starts) - 1
    while k > 0 and env[starts[k]:ends[k] + 1].max() < pk * 10 ** (-20 / 20): k -= 1
    end = ends[k]
    pre = env[max(0, end - int(0.012 * sr)):max(1, end - int(0.006 * sr))].max()
    return end / sr, bool(pre > pk * 10 ** (-28 / 20))

def onset_bands(y, sr, S):
    """per-onset kick / snare / hat salience from band flux right after the attack"""
    X = np.abs(librosa.stft(y.mean(1).astype(np.float32), n_fft=1024, hop_length=HOP)); f = librosa.fft_frequencies(sr=sr, n_fft=1024)
    out = []
    for lo, hi in [(25, 130), (1500, 5000), (7000, 18000)]:
        b = np.log1p(200 * X[(f >= lo) & (f < hi)].sum(0) / (X.sum(0).max() + 1e-9))
        d = np.maximum(0, b - np.concatenate([[b[0]] * 3, b[:-3]]))
        v = np.array([d[max(0, int(s / HOP) - 1):int(s / HOP) + 6].max() for s in S])
        out.append(v / (v.max() + 1e-9))
    return np.array(out).T   # (n, 3): kick, snare, hat

class Break:
    def __init__(self, path):
        self.path = path; self.name = os.path.basename(path)
        y, sr = sf.read(path, always_2d=True); self.y, self.sr = y, sr
        self.info = sf.info(path)
        self.dur = len(y) / sr
        S, F, A, env, e = detect(y, sr)
        A = np.array([e[s:s + int(0.06 * sr)].max() if s < len(e) else 0 for s in S])
        A90 = np.percentile(A, 90)
        # a file that starts right on a hit has no rise for the onset function to see
        head = e[:int(0.03 * sr)]
        if (len(S) == 0 or S[0] > int(0.015 * sr)) and head.max() >= 0.3 * A90 and np.argmax(head) > int(0.001 * sr) \
                and head[:int(0.002 * sr)].max() > 0.05 * A90:
            S = np.concatenate([[0], S]); F = np.concatenate([[0.5], F]); A = np.concatenate([[head.max()], A])
        valid = A >= 0.12 * A90
        self.S, self.F, self.A, self.A90 = S[valid], F[valid], A[valid], A90
        self.e = e
        self.e_peak = float(e.max()); self.e_floor = float(np.percentile(e, 2))
        self.foot = np.array([self.attack_foot(s) for s in self.S])
        self.tau = self.S / sr
        self.w = self.F * np.sqrt(np.clip(self.A / A90, 0, 1.5))
        self.bands = onset_bands(y, sr, self.S)
        self.a_end, self.hard = audio_extent(y, sr)
        self.B = band_odf(y.mean(1), sr); self.base = selfsim_baseline(self.B, sr)
        self.first_strong = int(np.argmax(self.A >= 0.3 * A90))

    def attack_foot(self, s):
        """start of the attack at sample s: the envelope minimum in the 8 ms before it"""
        a = max(0, s - int(0.008 * self.sr))
        return (a + int(np.argmin(self.e[a:s + 1]))) / self.sr

    def start_foot(self, i):
        """for the first beat: walk back through any grace note / flam to the preceding silence (<= 40 ms)"""
        e, sr = self.e, self.sr
        s = int(self.S[i])
        f = int(self.foot[i] * sr)
        emax = e[s:s + int(0.03 * sr)].max()
        silence = max(self.e_peak * 10 ** (-50 / 20), 0.03 * emax, min(3 * self.e_floor, 0.06 * emax))
        a = max(0, s - int(0.040 * sr))
        q = np.concatenate([[False], e[a:f + 1] <= silence, [False]]).astype(int)
        d = np.diff(q); runs_s = np.where(d == 1)[0]; runs_e = np.where(d == -1)[0]   # [start, end) in segment coords
        for rs, re_ in zip(runs_s[::-1], runs_e[::-1]):
            if re_ - rs >= int(0.003 * sr) or (a == 0 and rs == 0):
                return (a + re_ - 1) / sr
        return f / sr

    def refine_strong(self, P, anc, div):
        """refit period/phase on strong hits sitting on 8th-note positions (kicks/snares carry the most reliable timing)"""
        tau, w, A, A90 = self.tau, self.w, self.A, self.A90
        g = P / div
        q = np.round((tau - anc) / g)
        r = tau - anc - q * g
        sel = (A >= 0.4 * A90) & (np.abs(r) < 0.25 * g) & (q % (div // 2) == 0)
        if sel.sum() < 5 or np.ptp(q[sel]) < 2 * div: return P, anc
        M = np.vstack([np.ones(sel.sum()), q[sel]]).T; W = np.sqrt(w[sel])
        (a, b), *_ = np.linalg.lstsq(M * W[:, None], tau[sel] * W, rcond=None)
        if not (0.985 < b * div / P < 1.015): return P, anc
        return b * div, a

    def candidates(self):
        tau, w = self.tau, self.w
        bpms = np.arange(60, 200.01, 0.05)
        found = []
        for gname, (levels, div, pen) in GRIDS.items():
            c = grid_curve(tau, w, bpms, levels)
            pk, _ = find_peaks(c, distance=int(1.5 / 0.05))
            for k in pk[np.argsort(c[pk])[::-1][:4]]:
                for mul in (1, 2, 0.5):
                    b = bpms[k] * mul
                    if 60 <= b <= 200: found.append((b, gname))
        rows = []
        for b, gname in found:
            levels, div, pen = GRIDS[gname]
            P = 60 / b; best = None
            for frac in np.arange(div) / div:
                P2, a2 = refine(tau, w, P, tau[self.first_strong] - frac * P, div)
                if not (0.96 < P2 / P < 1.04): continue
                G = fit_G(tau, w, P2, a2, levels) - pen
                if best is None or G > best[0]: best = (G, P2, a2)
            if best is None: continue
            G, P2, a2 = best
            if any(abs(60 / P2 / r['bpm'] - 1) < 0.01 and r['grid'] == gname for r in rows): continue
            rows.append(dict(bpm=60 / P2, P=P2, anchor=a2, G=G, grid=gname,
                             R1=selfsim_at(self.B, self.sr, 4 * P2), R2=selfsim_at(self.B, self.sr, 8 * P2)))
        return rows

    def loop_fit(self, P):
        """does a hard cut / the file end land on a whole number of bars from the first strong hit?"""
        s0 = self.tau[self.first_strong]; L = 4 * P
        for c in (([self.dur] if self.dur - self.a_end < 0.02 else []) + ([self.a_end] if self.hard else [])):
            n = round((c - s0) / L)
            if n >= 1 and abs(c - s0 - n * L) <= max(0.02, 0.012 * n * L): return 1
        return 0

    def choose_family(self):
        """group candidate tempi into half/double families and rank them"""
        rows = self.candidates()
        gmax = max(r['G'] for r in rows)
        fams = []
        for r in sorted(rows, key=lambda r: -r['G']):
            for f in fams:
                x = np.log2(r['bpm'] / f[0]['bpm'])
                if abs(x - round(x)) < 0.015: f.append(r); break
            else: fams.append([r])
        scored = []
        for f in fams:
            G = max(r['G'] for r in f)
            if G < gmax - 0.15: continue
            Rs = [x for r in f for x in (r['R1'], r['R2']) if x is not None]
            R = max(Rs) - self.base if Rs else 0.0
            LF = max([self.loop_fit(r['P'] * k) for r in f for k in (0.5, 1, 2) if 60 <= 60 / (r['P'] * k) <= 200] or [0])
            scored.append(dict(score=G + 0.5 * max(0.0, R - 0.15) + 0.1 * LF, G=G, R=R, LF=LF, members=f))
        scored.sort(key=lambda s: -s['score'])
        return scored

    def place(self, fam, force_bpm=None):
        """octave choice, first beat, bar count and crop points for one tempo family"""
        rep = max(fam['members'], key=lambda r: r['G'])
        tau, w, A, A90 = self.tau, self.w, self.A, self.A90
        opts = []
        for mul in (0.25, 0.5, 1, 2, 4):
            bpm = rep['bpm'] * mul
            if force_bpm: bpm = force_bpm * mul if mul == 1 else None
            if bpm is None or not (55 <= bpm <= 210): continue
            P = 60 / bpm
            div = 4 if (rep['grid'] == 'straight' or mul < 1) else 6
            P2, anc = refine(tau, w, P, rep['anchor'], div)
            if not (0.98 < P2 / P < 1.02): P2, anc = P, rep['anchor']
            if force_bpm: P2 = P
            P2, anc = self.refine_strong(P2, anc, div)
            g = P2 / div
            q = np.round((tau - anc) / g).astype(int)
            on = np.abs(tau - (anc + q * g)) < 0.35 * g
            # beat phase: the subdivision carrying the most salience; keep the first strong hit unless clearly beaten
            sal = w * np.sqrt(np.clip(A / A90, 0, 1.5)) * on
            ph_sal = np.array([sal[(q % div) == j].sum() for j in range(div)])
            fs = self.first_strong
            phase = int(np.argmax(ph_sal)); ph_first = q[fs] % div
            if ph_sal[ph_first] >= 0.6 * ph_sal[phase]: phase = ph_first
            cand = np.where((A >= 0.3 * A90) & on & ((q % div) == phase))[0]
            i0 = cand[0] if len(cand) else fs
            if tau[i0] - tau[fs] > 0.95 * P2: i0 = fs      # a pickup is never more than a beat long
            start = max(0.0, self.start_foot(i0) - PRE_ROLL)
            L = 4 * P2
            snap_tol = 0.015 * L
            res = None
            k = int(np.floor((min(self.dur + L / 16, self.a_end + 0.25 * L) - start) / L + 1e-9))
            while k >= 1:
                pred = start + k * L
                if pred > self.dur + L / 16 or self.a_end < pred - 0.25 * L: k -= 1; continue
                ft = self.foot - PRE_ROLL
                near = np.where((np.abs(ft - pred) < max(0.02, 0.06 * P2)) & (A >= 0.3 * A90) & (self.F >= 0.2))[0]
                if len(near):                                   # the next bar's first hit is in the file
                    E, how = ft[near[np.argmin(np.abs(ft[near] - pred))]], 'downbeat'
                elif abs(self.dur - pred) <= snap_tol:          # file was already cut on the bar line
                    E, how = min(self.dur, pred), ('file-end' if pred > self.dur else 'grid')
                elif self.hard and abs(self.a_end - pred) <= snap_tol:
                    E, how = self.a_end, 'hard-cut'
                elif pred > self.dur:                           # source ends < 1/16 short of the bar line
                    E, how = pred, 'padded'
                else:
                    E, how = pred, 'grid'
                res = (k, E, how); break
            if res is None:
                opts.append(dict(bpm=60 / P2, bars=0, start=start, end=start, how='none', i0=int(i0), skip=int(i0 != fs), P=P2))
                continue
            k, E, how = res
            opts.append(dict(bpm=240 * k / (E - start), bars=k, start=start, end=E, how=how, i0=int(i0), skip=int(i0 != fs), P=P2))
        # octave: most retained audio (3 % tie), then within [LO, HI], then snare backbeat test, then faster
        mx = max(o['end'] - o['start'] for o in opts)
        good = [o for o in opts if o['end'] - o['start'] >= 0.97 * mx and o['bars'] > 0]
        pool = [o for o in good if LO <= o['bpm'] <= HI] or good
        if len(pool) > 1:
            pool.sort(key=lambda o: o['bpm'])
            slow, fast = pool[0], pool[-1]
            return slow if self.backbeat(slow) > 0.15 else fast
        return pool[0] if pool else opts[0]

    def backbeat(self, o):
        """+ if snares sit on beats 2 & 4 of this (slower) tempo, - if they sit on the 8th offbeats (= 2 & 4 at double tempo)"""
        g = o['P'] / 4
        q = np.round((self.tau - self.tau[o['i0']]) / g).astype(int)
        on = np.abs(self.tau - self.tau[o['i0']] - q * g) < 0.35 * g
        sn = self.bands[:, 1] * (self.bands[:, 1] > self.bands[:, 0]) * on * (q >= 0)
        pos = q % 16
        slow24 = sn[(pos == 4) | (pos == 12)].sum()
        fast24 = sn[(pos == 2) | (pos == 6) | (pos == 10) | (pos == 14)].sum() / 2
        return float((slow24 - fast24) / (slow24 + fast24 + 1e-9))
