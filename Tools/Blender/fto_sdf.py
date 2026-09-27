"""
Signed distance fields in numpy, meshed with surface nets: how FTO's characters get one smooth, watertight skin.

A character is a stack of layers (skin, shirt, trousers, hair, cap...), each a signed distance function of points
in centimetres (negative inside). The mesh is the surface of their union, so clothes are part of the body surface
(no separate shells to clip through when it bends) and a hem is a small step where one layer ends. Each vertex takes
the colour of the layer it lies on (see label_vertices).

Primitives follow Inigo Quilez's distance functions (https://iquilezles.org/articles/distfunctions/). Only numpy is
needed, which ships with Blender, so the scripts run in a stock `blender -b`.
"""
import numpy as np


# --------------------------------------------------------------------------------------
# Primitives: each returns f(P) with P an (N, 3) float array in cm
# --------------------------------------------------------------------------------------
def _v(x):
    return np.asarray(x, dtype=np.float64)


def _norm(a):
    return np.sqrt(np.einsum('ij,ij->i', a, a))


def ellipsoid(center, radii):
    c, r = _v(center), _v(radii)

    def f(P):
        q = (P - c) / r
        k0 = _norm(q)
        k1 = _norm((P - c) / (r * r))
        return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)
    return f


def sphere(center, radius):
    c = _v(center)
    return lambda P: _norm(P - c) - radius


def round_cone(a, b, ra, rb):
    """A capsule from a (radius ra) to b (radius rb)."""
    a, b = _v(a), _v(b)
    ba = b - a
    l2 = float(ba @ ba)
    rr = ra - rb
    a2 = l2 - rr * rr
    il2 = 1.0 / l2

    def f(P):
        pa = P - a
        y = pa @ ba
        z = y - l2
        w = pa * l2 - np.outer(y, ba)
        x2 = np.einsum('ij,ij->i', w, w)
        y2 = y * y * l2
        z2 = z * z * l2
        k = np.sign(rr) * rr * rr * x2
        d_b = np.sqrt(x2 + z2) * il2 - rb
        d_a = np.sqrt(x2 + y2) * il2 - ra
        d_s = (np.sqrt(np.maximum(x2 * a2 * il2, 0.0)) + y * rr) * il2 - ra
        return np.where(np.sign(z) * a2 * z2 > k, d_b, np.where(np.sign(y) * a2 * y2 < k, d_a, d_s))
    return f


def capsule(a, b, r):
    return round_cone(a, b, r, r)


def box(center, half, rounding=0.0, axes=None):
    """A rounded box; axes (3x3, columns = the box's local x, y, z in world space) orients it."""
    c, h = _v(center), _v(half)
    R = np.eye(3) if axes is None else _v(axes)

    def f(P):
        q = np.abs((P - c) @ R) - h + rounding
        outside = _norm(np.maximum(q, 0.0))
        inside = np.minimum(q.max(axis=1), 0.0)
        return outside + inside - rounding
    return f


def cylinder(center, axis, radius, half_height, rounding=0.0):
    c, ax = _v(center), _v(axis) / np.linalg.norm(axis)

    def f(P):
        d = P - c
        h = d @ ax
        rad = _norm(d - np.outer(h, ax))
        q = np.stack([rad - radius + rounding, np.abs(h) - half_height + rounding], axis=1)
        return np.minimum(q.max(axis=1), 0.0) + _norm(np.maximum(q, 0.0)) - rounding
    return f


def halfspace(point, normal):
    """Everything behind the plane (the side the normal points away from) is inside."""
    p0, n = _v(point), _v(normal) / np.linalg.norm(normal)
    return lambda P: (P - p0) @ n


# --------------------------------------------------------------------------------------
# Combinators
# --------------------------------------------------------------------------------------
def union(*fs):
    def f(P):
        out = fs[0](P)
        for g in fs[1:]:
            out = np.minimum(out, g(P))
        return out
    return f


def smin(a, b, k):
    if k <= 0.0:
        return np.minimum(a, b)
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def smooth_union(k, *fs):
    def f(P):
        out = fs[0](P)
        for g in fs[1:]:
            out = smin(out, g(P), k)
        return out
    return f


def intersect(*fs):
    def f(P):
        out = fs[0](P)
        for g in fs[1:]:
            out = np.maximum(out, g(P))
        return out
    return f


def smooth_intersect(k, a, b):
    return lambda P: -smin(-a(P), -b(P), k)


def subtract(f, g, k=0.0):
    return lambda P: -smin(-f(P), g(P), k) if k > 0 else np.maximum(f(P), -g(P))


def offset(f, t):
    """Grow (t > 0) or shrink a shape: how a clothing layer sits t cm over the skin."""
    return lambda P: f(P) - t


def cached(f):
    """Remembers the last evaluation, so a shape shared by several layers (the torso) is computed once per batch."""
    memo = {"key": None, "val": None}

    def g(P):
        key = (id(P), P.shape)
        if memo["key"] != key:
            memo["key"], memo["val"] = key, f(P)
        return memo["val"]
    return g


# --------------------------------------------------------------------------------------
# Meshing
# --------------------------------------------------------------------------------------
def evaluate_grid(f, lo, hi, voxel, chunk=400000):
    lo, hi = _v(lo), _v(hi)
    dims = np.ceil((hi - lo) / voxel).astype(int) + 1
    xs, ys, zs = (lo[i] + voxel * np.arange(dims[i]) for i in range(3))
    F = np.empty(dims, dtype=np.float32)
    plane = dims[0] * dims[1]
    gx, gy = np.meshgrid(xs, ys, indexing='ij')
    gx, gy = gx.ravel(), gy.ravel()
    slab = max(1, chunk // plane)
    for k0 in range(0, dims[2], slab):
        k1 = min(dims[2], k0 + slab)
        n = k1 - k0
        P = np.empty((plane * n, 3))
        P[:, 0] = np.tile(gx, n)
        P[:, 1] = np.tile(gy, n)
        P[:, 2] = np.repeat(zs[k0:k1], plane)
        F[:, :, k0:k1] = f(P).reshape(n, dims[0], dims[1]).transpose(1, 2, 0)
    return F, lo, dims


def surface_nets(f, lo, hi, voxel, project=2):
    """
    Meshes f's zero surface: one vertex per grid cell the surface passes through (the mean of its edge crossings,
    then pulled onto the surface along the gradient), one quad per crossing grid edge. Returns (verts, quads) with
    outward-facing quads.
    """
    F, lo, dims = evaluate_grid(f, lo, hi, voxel)
    inside = F < 0.0
    cdims = dims - 1
    ncell = int(np.prod(cdims))
    acc = np.zeros((ncell, 3))
    cnt = np.zeros(ncell)

    def cell_id(i, j, k):
        return (i * cdims[1] + j) * cdims[2] + k

    edges = []
    for axis in range(3):
        sl0 = [slice(None)] * 3
        sl1 = [slice(None)] * 3
        sl0[axis] = slice(0, -1)
        sl1[axis] = slice(1, None)
        f0, f1 = F[tuple(sl0)], F[tuple(sl1)]
        cross = inside[tuple(sl0)] != inside[tuple(sl1)]
        idx = np.nonzero(cross)
        a, b = f0[idx].astype(np.float64), f1[idx].astype(np.float64)
        t = a / (a - b)
        pos = np.stack(idx, axis=1).astype(np.float64)
        pos[:, axis] += t
        edges.append((idx, pos, inside[tuple(sl0)][idx]))
        # Each edge touches the four cells around it.
        others = [ax for ax in range(3) if ax != axis]
        for da in (0, -1):
            for db in (0, -1):
                c = [idx[0].copy(), idx[1].copy(), idx[2].copy()]
                c[others[0]] = c[others[0]] + da
                c[others[1]] = c[others[1]] + db
                ok = np.ones(len(a), dtype=bool)
                for ax in range(3):
                    ok &= (c[ax] >= 0) & (c[ax] < cdims[ax])
                cid = cell_id(c[0][ok], c[1][ok], c[2][ok])
                np.add.at(acc, cid, pos[ok])
                np.add.at(cnt, cid, 1.0)

    used = np.nonzero(cnt > 0)[0]
    vindex = -np.ones(ncell, dtype=np.int64)
    vindex[used] = np.arange(len(used))
    verts = lo + voxel * (acc[used] / cnt[used][:, None])

    quads = []
    for axis, (idx, _pos, lower_inside) in enumerate(edges):
        a, b = [(1, 2), (2, 0), (0, 1)][axis]  # quad spans (a, b) with a x b = +axis
        i = [idx[0], idx[1], idx[2]]
        ok = np.ones(len(i[0]), dtype=bool)
        for ax in (a, b):
            ok &= (i[ax] >= 1) & (i[ax] < cdims[ax])
        i = [x[ok] for x in i]
        flip = ~lower_inside[ok]

        def shifted(da, db):
            c = [i[0].copy(), i[1].copy(), i[2].copy()]
            c[a] = c[a] - da
            c[b] = c[b] - db
            return vindex[cell_id(*c)]
        q = np.stack([shifted(1, 1), shifted(0, 1), shifted(0, 0), shifted(1, 0)], axis=1)
        q[flip] = q[flip][:, ::-1]
        quads.append(q)
    quads = np.concatenate(quads)
    quads = quads[(quads >= 0).all(axis=1)]

    for _ in range(project):
        verts = project_to_surface(f, verts, max_step=0.5 * voxel)
    return verts, quads


def gradient(f, P, eps=0.15):
    g = np.empty_like(P)
    for ax in range(3):
        d = np.zeros(3)
        d[ax] = eps
        g[:, ax] = (f(P + d) - f(P - d)) / (2.0 * eps)
    return g


def project_to_surface(f, P, max_step=None):
    d = f(P)
    g = gradient(f, P)
    gg = np.maximum(np.einsum('ij,ij->i', g, g), 1e-9)
    step = (d / gg)[:, None] * g
    if max_step is not None:
        ln = _norm(step)
        step *= np.minimum(1.0, max_step / np.maximum(ln, 1e-9))[:, None]
    return P - step


def label_vertices(layers, P):
    """Index of the layer each point lies on: the one with the smallest distance (after its bias)."""
    vals = np.stack([f(P) - bias for f, bias in layers], axis=1)
    return np.argmin(vals, axis=1)
