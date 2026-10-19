"""Resample (varispeed: tempo and pitch move together) cropped breaks to one target tempo.

Usage (from the project root):
    python tools/break_analysis/tune_breaks.py <cropped-dir> breaks [--bpm 162] [--double-below 93]

<cropped-dir> is the output of make_breaks.py (WAVs plus breaks.csv).
Reads breaks.csv from the source folder for each loop's bar count, works out the loop's exact
tempo from its length, and writes each file under the same name, same sample rate and bit depth,
at exactly the target number of bars (length rounded to the nearest sample). Writes breaks.csv
alongside with the speed ratio and pitch shift.

Loops slower than --double-below (default 93 BPM) are read at double tempo with twice the bars
(e.g. 1 bar at 89 BPM -> 2 bars at 178 BPM), so they are slowed slightly instead of pitched up
about 10 semitones. Pass --double-below 0 to take every loop at its listed tempo.
"""
import os, csv, zlib, argparse, numpy as np, soundfile as sf, soxr

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src'); ap.add_argument('dst')
    ap.add_argument('--bpm', type=float, default=162.0)
    ap.add_argument('--double-below', type=float, default=93.0)
    args = ap.parse_args()
    os.makedirs(args.dst, exist_ok=True)
    rows = []
    for r in csv.DictReader(open(os.path.join(args.src, 'breaks.csv'))):
        path = os.path.join(args.src, r['file'])
        info = sf.info(path)
        x, sr = sf.read(path, always_2d=True)
        bars = int(r['bars'])
        src_bpm = 240 * bars * sr / len(x)                  # exact tempo of the loop as cropped
        note = ''
        if src_bpm < args.double_below:
            note = f'read at double tempo (listed as {bars} bar(s) at {src_bpm:.3f} BPM)'
            bars, src_bpm = bars * 2, src_bpm * 2
        n_out = round(240 * bars * sr / args.bpm)
        y = soxr.resample(x, len(x), n_out, quality='VHQ')   # time-aligned, exact output length
        lsb = 2.0 ** -15 if info.subtype == 'PCM_16' else 2.0 ** -23
        if info.subtype == 'PCM_16':                        # TPDF dither before requantising to 16 bit
            rng = np.random.default_rng(zlib.crc32(r['file'].encode()))
            y = y + (rng.random(y.shape) - rng.random(y.shape)) * lsb
        y = np.clip(y, -1.0, 1.0 - lsb)                     # resampling can overshoot full scale slightly
        sf.write(os.path.join(args.dst, r['file']), y, sr, subtype=info.subtype, format='WAV')
        ratio = args.bpm / src_bpm
        rows.append(dict(file=r['file'], source_file=r['source_file'], bpm=round(240 * bars * sr / n_out, 3), bars=bars,
                         length_samples=n_out, length_s=round(n_out / sr, 4), source_bpm=round(src_bpm, 3),
                         speed_ratio=round(ratio, 4), pitch_semitones=round(12 * np.log2(ratio), 2),
                         sample_rate=sr, format=info.subtype, notes=note))
        print(f"{r['file']}  {src_bpm:7.2f} -> {args.bpm:g} BPM  x{ratio:.3f}  {12 * np.log2(ratio):+6.2f} st  {bars} bar(s)  {note}")
    with open(os.path.join(args.dst, 'breaks.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
