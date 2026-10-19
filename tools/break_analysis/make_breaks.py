"""Crop raw drum breaks to a whole number of bars starting on the first beat.

Usage (from the project root):
    python tools/break_analysis/make_breaks.py <raw-breaks-dir> <cropped-dir> [--json crops.json]

Writes 001.wav, 002.wav, ... (same sample rate / bit depth as the source, numbered in source order)
plus breaks.csv with tempo, bar count and where each crop came from. Pass <cropped-dir> to
tune_breaks.py to resample the loops into breaks/.
"""
import os, re, sys, glob, csv, json, argparse, numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analysis import Break, PRE_ROLL

FADE_IN = 0.0005     # s, only over the pre-roll before the first attack
FADE_OUT = 0.003     # s, anti-click at the loop point

# Left out of the output. Reasons are from inspecting each file's waveform and onset grid.
EXCLUDE = {
    '1 64.wav': 'irregular groove, no steady 4/4 grid',
    '1 71.wav': 'beat 1 falls ~60 ms before the file starts (would need silence padded in front)',
    '1 84.wav': 'distorted, tempo grid unreliable',
    '1 92.wav': 'loose and distorted, tempo grid unreliable',
    '1 85.wav': 'source ends ~43 ms short of the bar 2 line',
    '1 89.wav': 'source ends ~68 ms short of the bar 2 line',
    '1 50.wav': 'only 3 whole bars (4th bar incomplete in the source)',
    '1 97.wav': 'only 3 whole bars (4th bar incomplete in the source)',
    '1 98.wav': 'only 3 whole bars (4th bar incomplete in the source)',
}

def cut_exact(b):
    """sample right after the last loud sample (where the source was chopped)"""
    x = np.abs(b.y).max(1)
    return (np.where(x > 0.05 * x.max())[0][-1] + 1) / b.sr

# Manual corrections to the automatic result
OVERRIDES = {
    # hits fit 161.4 BPM better than the automatic fit (162.9); at 161.4 the bar line lands on the hard cut
    '1 82.wav': dict(end='hard-cut', note='bar end = hard cut (161.4 BPM grid agrees)'),
    # kick / snare / reverse-swell loops: the snare backbeat grid puts the bar line on the hard cut
    '1 65.wav': dict(end='hard-cut', note='bar end = hard cut (snare backbeat grid agrees)'),
    '1 70.wav': dict(end='hard-cut', note='bar end = hard cut (snare backbeat grid agrees)'),
    '1 77.wav': dict(end='hard-cut', note='bar end = hard cut (snare backbeat grid agrees)'),
    # strong hits fit ~162 BPM; the automatic fit was pulled off by quiet hats
    '1 60.wav': dict(end='hard-cut', note='bar end = hard cut (kick/snare grid agrees)'),
}

def resolve(path):
    b = Break(path)
    ov = OVERRIDES.get(b.name, {})
    c = dict(b.place(b.choose_family()[0]))
    if ov.get('end') == 'hard-cut':
        c['end'] = cut_exact(b); c['how'] = 'hard-cut (manual)'
        c['bpm'] = 240 * c['bars'] / (c['end'] - c['start'])
    return b, c, ov

def render(b, c):
    y, sr = b.y, b.sr
    s0 = int(round(c['start'] * sr)); s1 = int(round(c['end'] * sr))
    n = s1 - s0
    out = np.zeros((n, y.shape[1]))
    a, z = max(0, s0), min(len(y), s1)
    out[a - s0:z - s0] = y[a:z]
    pad_head = max(0, -s0); pad_tail = max(0, s1 - len(y))
    if s0 > 0:      # fade only across the short pre-roll, never into the hit itself
        k = min(int(FADE_IN * sr), n)
        out[:k] *= np.linspace(0, 1, k, endpoint=False)[:, None]
    last = n - pad_tail
    k = min(int(FADE_OUT * sr), last)
    out[last - k:last] *= (0.5 * (1 + np.cos(np.linspace(0, np.pi, k))))[:, None]
    return out, pad_head, pad_tail

def source_order(name):
    return [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', name)]

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--json', help='also write per-file crop details (for review.py)')
    args = ap.parse_args()
    os.makedirs(args.dst, exist_ok=True)
    names = sorted((os.path.basename(p) for p in glob.glob(os.path.join(args.src, '*.wav'))), key=source_order)
    rows, dump = [], {}
    for n in names:
        if n in EXCLUDE:
            print(f"{n:9s} excluded: {EXCLUDE[n]}"); continue
        b, c, ov = resolve(os.path.join(args.src, n))
        out, ph, pt = render(b, c)
        outname = f"{len(rows) + 1:03d}.wav"
        sf.write(os.path.join(args.dst, outname), out, b.sr, subtype=b.info.subtype, format='WAV')
        notes = []
        if ph: notes.append(f'padded {ph / b.sr * 1000:.0f} ms silence at start')
        if pt: notes.append(f'padded {pt / b.sr * 1000:.0f} ms silence at end')
        if c.get('skip'): notes.append('skipped pickup before beat 1')
        if ov: notes.append('manual: ' + ov['note'])
        rows.append(dict(file=outname, source_file=n, bpm=round(c['bpm'], 3), bars=c['bars'], length_samples=len(out),
                         length_s=round(len(out) / b.sr, 4), src_start_sample=int(round(c['start'] * b.sr)),
                         src_end_sample=int(round(c['end'] * b.sr)), end_method=c['how'],
                         sample_rate=b.sr, format=b.info.subtype, notes='; '.join(notes)))
        dump[n] = dict(out=outname, dur=b.dur, choice={k: (float(v) if isinstance(v, (float, np.floating)) else v)
                                                       for k, v in c.items() if k in ('bpm', 'bars', 'start', 'end', 'how', 'skip')})
        print(f"{n:9s} -> {outname}  {c['bpm']:7.2f} BPM x{c['bars']}  {c['start']:.3f}-{c['end']:.3f} s  ({c['how']})  {'; '.join(notes)}")
    with open(os.path.join(args.dst, 'breaks.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    if args.json:
        with open(args.json, 'w') as f: json.dump(dump, f, indent=1)
