"""Colour the loops by similarity cluster, in rainbow order.

Usage (from the project root):
    python tools/break_analysis/colour_breaks.py breaks breaks/colours.json [--bpm 162] [--colours 7]

1. Describe each loop exactly as layout_breaks.py does (timbre, groove, dynamics, mixed by
   its WEIGHTS), so the colours and the pad positions come from the same idea of similarity.
2. Cluster into as many groups as the plugin has colours (k-means; the Froot Loops palette
   has 7: red, orange, yellow, green, blue, purple, pink).
3. Order the clusters along the rainbow so neighbouring colours are neighbouring sounds: the
   shortest path through the cluster centres, starting from either end.

The breaks are more of a continuum than a set of tight groups: the silhouette score is about
0.14, and 7 clusters scored best in the 3 to 8 range. Read the colours as regions of a
spectrum. On the pad, 55% of neighbouring cells share a colour (19% for shuffled colours).

Writes JSON: [{"file": "001.wav", "colour": 2}, ...], colour = rainbow index 0 .. colours-1.
"""
import os, glob, json, argparse, itertools, numpy as np
from sklearn.cluster import KMeans
from sklearn.metrics import silhouette_score
from layout_breaks import features, combine

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('out')
    ap.add_argument('--bpm', type=float, default=162.0)
    ap.add_argument('--colours', type=int, default=7)
    a = ap.parse_args()

    files = sorted(glob.glob(os.path.join(a.folder, '*.wav')))
    X = combine([features(f, a.bpm) for f in files])
    km = KMeans(n_clusters=a.colours, n_init=100, random_state=0).fit(X)
    C = km.cluster_centers_
    D = np.sqrt(((C[:, None] - C[None]) ** 2).sum(-1))
    k = a.colours
    order = min(itertools.permutations(range(k)), key=lambda p: sum(D[p[i], p[i + 1]] for i in range(k - 1)))
    rank = {c: i for i, c in enumerate(order)}
    out = [{'file': os.path.basename(f), 'colour': rank[int(c)]} for f, c in zip(files, km.labels_)]
    with open(a.out, 'w') as fh:
        json.dump(out, fh, indent=1)
    sizes = np.bincount([o['colour'] for o in out], minlength=k)
    print(f'{len(files)} loops, {k} colours, sizes {sizes.tolist()}, silhouette {silhouette_score(X, km.labels_):.3f}')

if __name__ == '__main__':
    main()
