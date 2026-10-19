"""Plot each source break with its crop, beat grid and zooms on the start / end points.

Usage (from the project root):
    python tools/break_analysis/review.py <raw-breaks-dir> crops.json <plots-dir> [source names...]

crops.json comes from make_breaks.py --json; source names are raw file names such as "1 12.wav".
"""
import os, sys, json, numpy as np, soundfile as sf, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from onsets import detect

if __name__ == '__main__':
    d, resf, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(outdir, exist_ok=True)
    res = json.load(open(resf))
    names = sys.argv[4:] or list(res.keys())
    per = 6
    for page in range(0, len(names), per):
        grp = names[page:page + per]
        fig, axs = plt.subplots(len(grp), 3, figsize=(26, 2.7 * len(grp)), gridspec_kw=dict(width_ratios=[6, 1, 1]))
        axs = np.atleast_2d(axs)
        for row, n in zip(axs, grp):
            r = res[n]; c = r['choice']
            y, sr = sf.read(os.path.join(d, n), always_2d=True); m = y.mean(1); t = np.arange(len(m)) / sr
            S = detect(y, sr)[0]
            st, en, bpm, bars = c['start'], c['end'], c['bpm'], c['bars']
            P = 60 / bpm
            ax = row[0]
            ax.plot(t, m, lw=0.3, color='0.35')
            ax.axvspan(st, en, color='C2', alpha=0.12)
            for k in range(0, 16 * bars + 1):       # bar lines red, beats blue, 16ths grey
                ax.axvline(st + k * P / 4, color='C3' if k % 16 == 0 else ('C0' if k % 4 == 0 else '0.7'),
                           lw=2 if k % 16 == 0 else (0.9 if k % 4 == 0 else 0.4))
            ax.plot(S / sr, np.full(len(S), 0.97 * np.abs(m).max()), 'v', color='k', ms=4)
            ax.set_xlim(0, max(t[-1], en)); ax.set_yticks([])
            ax.set_title(f"{n} -> {r.get('out', '')}: {bpm:.2f} BPM x{bars}  {st:.3f}-{en:.3f} s ({c['how']}{', pickup skipped' if c['skip'] else ''})",
                         loc='left', fontsize=11)
            for axz, centre, lab in ((row[1], st, 'start'), (row[2], en, 'end')):
                sel = (t >= centre - 0.04) & (t <= centre + 0.04)
                axz.plot(t[sel], y[sel, 0], lw=0.6); axz.plot(t[sel], y[sel, 1], lw=0.6, alpha=0.6)
                axz.axvline(centre, color='r'); axz.set_xlim(centre - 0.04, centre + 0.04); axz.set_title(lab, fontsize=9); axz.set_yticks([])
        plt.tight_layout(); plt.savefig(os.path.join(outdir, f"review_{page // per:02d}.png"), dpi=50); plt.close()
