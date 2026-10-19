"""Lay out loops in the unit square by similarity, with evenly spaced positions.

Usage (from the project root):
    python tools/break_analysis/layout_breaks.py breaks breaks/layout.json [--bpm 162] [--plot layout.png]

1. Describe each loop by timbre (MFCC / spectral shape), dynamics, and its groove: onset strength
   of the low / mid / high bands on each 16th step, averaged over bars (loops must share one
   tempo and start on beat 1, as the files in breaks/ do).
2. Embed the loops in 2-D with t-SNE so similar loops land close together.
3. Make N evenly spaced sites in [0, 1] x [0, 1] (a centroidal Voronoi tessellation: every
   site owns the same area), assign loops to sites optimally, then swap pairs while that brings
   each loop's nearest neighbours in sound closer together on the square.

Writes JSON: [{"file": "001.wav", "x": 0.0512, "y": 0.4870}, ...] with y measured upwards.
"""
import os, glob, json, argparse, numpy as np, soundfile as sf, librosa
from scipy.spatial import cKDTree
from scipy.optimize import linear_sum_assignment
from sklearn.manifold import TSNE

WEIGHTS = dict(timbre=0.45, groove=0.45, dynamics=0.10)   # share of the similarity each aspect gets
BANDS = [(30, 150), (150, 2000), (4000, 16000)]           # kick / snare body / hats & cymbals

def features(path, bpm):
    y, sr = sf.read(path, always_2d=True)
    m = y.mean(1).astype(np.float32)
    # timbre
    S = np.abs(librosa.stft(m, n_fft=2048, hop_length=512))
    logmel = librosa.power_to_db(librosa.feature.melspectrogram(S=S ** 2, sr=sr, n_mels=64))
    mfcc = librosa.feature.mfcc(S=logmel, n_mfcc=20)[1:]
    shape = [librosa.feature.spectral_centroid(S=S, sr=sr), librosa.feature.spectral_bandwidth(S=S, sr=sr),
             librosa.feature.spectral_rolloff(S=S, sr=sr), librosa.feature.spectral_flatness(S=S)]
    shape = [np.log(s[0] + 1e-9) for s in shape]
    contrast = librosa.feature.spectral_contrast(S=S, sr=sr)
    timbre = np.concatenate([mfcc.mean(1), mfcc.std(1), [s.mean() for s in shape], [s.std() for s in shape], contrast.mean(1)])
    # dynamics
    rms = librosa.feature.rms(S=S)[0]
    rms_db = 20 * np.log10(rms + 1e-6)
    dynamics = np.array([rms_db.mean(), rms_db.std(), 20 * np.log10(np.abs(m).max() / (np.sqrt(np.mean(m ** 2)) + 1e-9))])
    # groove: band onset strength on each 16th step, averaged over bars
    hop = 128
    X = np.abs(librosa.stft(m, n_fft=1024, hop_length=hop)); f = librosa.fft_frequencies(sr=sr, n_fft=1024)
    step = 60 / bpm / 4
    n_steps = int(round(len(m) / sr / step)) // 16 * 16
    pattern = []
    for lo, hi in BANDS:
        b = np.log1p(100 * X[(f >= lo) & (f < hi)].sum(0) / (X.sum(0).max() + 1e-9))
        flux = np.maximum(0, b - np.concatenate([[b[0]] * 2, b[:-2]]))
        hits = []
        for k in range(n_steps):
            c = k * step * sr / hop
            hits.append(flux[max(0, int(c - 0.015 * sr / hop)):int(c + 0.030 * sr / hop) + 1].max())
        pattern.append(np.array(hits).reshape(-1, 16).mean(0))
    groove = np.concatenate(pattern)
    groove = groove / (np.linalg.norm(groove) + 1e-9)
    return dict(timbre=timbre, groove=groove, dynamics=dynamics)

def combine(feats):
    """stack aspects so plain Euclidean distance mixes them by WEIGHTS"""
    parts = []
    for name, w in WEIGHTS.items():
        F = np.array([f[name] for f in feats])
        if name != 'groove':                       # groove vectors are already unit-norm shapes
            F = (F - F.mean(0)) / (F.std(0) + 1e-9)
        d = np.sqrt(((F[:, None] - F[None]) ** 2).sum(-1))
        parts.append(F * np.sqrt(w) / d[np.triu_indices(len(F), 1)].mean())
    return np.hstack(parts)

def even_sites(n, iters=300, res=300, seed=0):
    """n generators of a centroidal Voronoi tessellation of the unit square (Lloyd's algorithm)"""
    rng = np.random.default_rng(seed)
    g = (np.arange(res) + 0.5) / res
    samples = np.stack(np.meshgrid(g, g), -1).reshape(-1, 2)
    sites = rng.random((n, 2))
    for _ in range(iters):
        owner = cKDTree(sites).query(samples)[1]
        cnt = np.bincount(owner, minlength=n)
        sites = np.stack([np.bincount(owner, samples[:, i], minlength=n) for i in (0, 1)], 1) / np.maximum(cnt, 1)[:, None]
    return sites

def affinity(Z, k=8):
    """symmetric kNN Gaussian affinity in feature space"""
    D = np.sqrt(((Z[:, None] - Z[None]) ** 2).sum(-1))
    idx = np.argsort(D, 1)[:, 1:k + 1]
    sigma = D[np.arange(len(Z)), idx[:, -1]]
    P = np.zeros_like(D)
    for i in range(len(Z)):
        P[i, idx[i]] = np.exp(-(D[i, idx[i]] / sigma[i]) ** 2)
    return (P + P.T) / 2, D

def refine(assign, sites, P, h, max_passes=100):
    """pairwise swaps that raise sum_ij P_ij exp(-|pos_i - pos_j|^2 / 2h^2): loops that sound alike
    end up next to each other (positions stay on the even sites)"""
    assign = assign.copy()
    pos = sites[assign].copy()
    n = len(pos)
    for _ in range(max_passes):
        improved = False
        for i in range(n):
            K = np.exp(-((pos[:, None] - pos[None]) ** 2).sum(-1) / (2 * h * h))
            # change in cost if loop i and loop j exchange positions, for every j at once
            delta = -2 * (K @ P[i] - P[i] @ K[i] - (P * K).sum(1) + P @ K[i] - 2 * P[i] * (1 - K[i]))
            delta[i] = 0
            j = int(np.argmin(delta))
            if delta[j] < -1e-12:
                pos[[i, j]] = pos[[j, i]]; assign[[i, j]] = assign[[j, i]]; improved = True
        if not improved: break
    return assign

def knn_overlap(D, pos, k=5):
    L = np.sqrt(((pos[:, None] - pos[None]) ** 2).sum(-1))
    a = np.argsort(D, 1)[:, 1:k + 1]; b = np.argsort(L, 1)[:, 1:k + 1]
    return np.mean([len(set(x) & set(y)) / k for x, y in zip(a, b)])

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--bpm', type=float, default=162.0)
    ap.add_argument('--plot')
    args = ap.parse_args()
    files = sorted(glob.glob(os.path.join(args.src, '*.wav')))
    feats = [features(p, args.bpm) for p in files]
    Z = combine(feats)
    n = len(files)
    emb = TSNE(n_components=2, perplexity=min(15, n // 4), init='pca', random_state=0, max_iter=3000).fit_transform(Z)
    spread = (np.argsort(np.argsort(emb, 0), 0) + 0.5) / n          # rank-normalise: evens out dense clusters
    sites = even_sites(n)
    assign = linear_sum_assignment(((spread[:, None] - sites[None]) ** 2).sum(-1))[1]
    P, D = affinity(Z)
    before = knn_overlap(D, sites[assign])
    assign = refine(assign, sites, P, h=1.2 / np.sqrt(n))           # kernel width ~ one site spacing
    pos = sites[assign]
    nn = cKDTree(pos).query(pos, 2)[0][:, 1]
    print(f"{n} loops | 5-NN kept: t-SNE {knn_overlap(D, emb):.2f}, on even sites {before:.2f} -> after swaps {knn_overlap(D, pos):.2f} "
          f"(random ~{5 / (n - 1):.2f}) | nearest-neighbour spacing {nn.mean():.4f} +- {nn.std() / nn.mean() * 100:.1f}%")
    with open(args.out, 'w') as f:
        json.dump([dict(file=os.path.basename(p), x=round(float(x), 5), y=round(float(y), 5)) for p, (x, y) in zip(files, pos)], f, indent=1)
    if args.plot:
        import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(9, 9))
        ax.scatter(pos[:, 0], pos[:, 1], s=30)
        for p, (x, y) in zip(files, pos): ax.annotate(os.path.basename(p)[:3], (x, y), fontsize=7, xytext=(3, 3), textcoords='offset points')
        ax.set_xlim(0, 1); ax.set_ylim(0, 1); ax.set_aspect('equal'); plt.tight_layout(); plt.savefig(args.plot, dpi=90)
