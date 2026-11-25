"""Exact vertex sets, the exact convex hull, flat-shaded and icosphere triangle meshes, and a few
simple solids built from triangles. Pure Python; no NumPy, SciPy or trimesh.

A triangle mesh here is a list of triangles; a triangle is three `(position, normal)` pairs, wound
counter-clockwise seen from outside. An indexed mesh is a `Mesh` of vertices and index triples.
"""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass
from typing import Callable

from .geometry import PHI, add, all_perms, cross, dedupe, dot, even_perms, norm, scale, signs, sub

Triangle = tuple
TriMesh = list


# ---- exact vertex sets ------------------------------------------------------------------

def cube(negative_zero=False):
    return dedupe(signs((1, 1, 1), negative_zero))


def octahedron(negative_zero=False):
    pts = []
    for p in all_perms((1, 0, 0)):
        pts += signs(p, negative_zero)
    return dedupe(pts)


def icosahedron(negative_zero=False):
    pts = []
    for p in even_perms((0, 1, PHI)):
        pts += signs(p, negative_zero)
    return dedupe(pts)


def dodecahedron(negative_zero=False):
    pts = signs((1, 1, 1), negative_zero)
    for p in even_perms((0, 1 / PHI, PHI)):
        pts += signs(p, negative_zero)
    return dedupe(pts)


def truncated_icosahedron(negative_zero=False):
    pts = []
    for base in [(0, 1, 3 * PHI), (1, 2 + PHI, 2 * PHI), (PHI, 2, PHI**3)]:
        for p in even_perms(base):
            pts += signs(p, negative_zero)
    return dedupe(pts)


def rhombicosidodecahedron(negative_zero=False):
    pts = []
    for base in [(1, 1, PHI**3), (PHI**2, PHI, 2 * PHI), (2 + PHI, 0, PHI**2)]:
        for p in even_perms(base):
            pts += signs(p, negative_zero)
    return dedupe(pts)


SOLIDS: dict[str, Callable] = {
    "cube": cube,
    "octahedron": octahedron,
    "dodecahedron": dodecahedron,
    "icosahedron": icosahedron,
    "truncated_icosahedron": truncated_icosahedron,
    "rhombicosidodecahedron": rhombicosidodecahedron,
}
"""Name to vertex-set builder. Each takes `negative_zero=False`."""


# ---- hull and triangle meshes -----------------------------------------------------------

def hull_faces(points):
    """Exact convex hull of a small point set: every supporting plane through three points that
    has all points on one side is a face plane. Returns `(normal, member indices)` per face, the
    members in counter-clockwise order. Coplanar points make one polygon, not triangles."""
    faces = []
    seen = []
    n = len(points)
    for i, j, k in itertools.combinations(range(n), 3):
        nrm = cross(sub(points[j], points[i]), sub(points[k], points[i]))
        if dot(nrm, nrm) < 1e-9:
            continue
        nrm = norm(nrm)
        d = dot(nrm, points[i])
        side = [dot(nrm, p) - d for p in points]
        if all(s <= 1e-6 for s in side):
            pass
        elif all(s >= -1e-6 for s in side):
            nrm = tuple(-c for c in nrm)
            d = -d
        else:
            continue
        key = tuple(round(c, 5) for c in nrm)
        if key in seen:
            continue
        seen.append(key)
        members = [m for m in range(n) if abs(dot(nrm, points[m]) - d) < 1e-6]
        center = tuple(sum(points[m][a] for m in members) / len(members) for a in range(3))
        ref = norm(sub(points[members[0]], center))
        ortho = cross(nrm, ref)
        members.sort(
            key=lambda m: math.atan2(dot(sub(points[m], center), ortho), dot(sub(points[m], center), ref))
        )
        faces.append((nrm, members))
    return faces


def flat_mesh(points, circumradius):
    """Fan-triangulated, flat-shaded mesh of the hull of `points`, scaled so the farthest point is
    `circumradius` from the origin. Returns `(triangles, polygon face count)`."""
    r = max(math.sqrt(dot(p, p)) for p in points)
    pts = [tuple(c * circumradius / r for c in p) for p in points]
    faces = hull_faces(pts)
    tris = []
    for nrm, members in faces:
        for t in range(1, len(members) - 1):
            a, b, c = pts[members[0]], pts[members[t]], pts[members[t + 1]]
            if dot(cross(sub(b, a), sub(c, a)), nrm) < 0:
                b, c = c, b
            tris.append(((a, nrm), (b, nrm), (c, nrm)))
    return tris, len(faces)


def icosphere(freq, radius, negative_zero=False):
    """Frequency-`freq` subdivision of the icosahedron projected onto a sphere, smooth normals
    (the normal of a vertex is its unit position). `freq` 1 is the icosahedron's 20 triangles; the
    count is 20 * freq**2."""
    base = [norm(p) for p in icosahedron(negative_zero)]
    faces = [m for _, m in hull_faces(base)]
    tris = []
    for f in faces:
        a, b, c = (base[f[0]], base[f[1]], base[f[2]])
        grid = {}
        for i in range(freq + 1):
            for j in range(freq + 1 - i):
                k = freq - i - j
                p = tuple((a[x] * i + b[x] * j + c[x] * k) / freq for x in range(3))
                grid[(i, j)] = norm(p)
        for i in range(freq):
            for j in range(freq - i):
                tris.append((grid[(i, j)], grid[(i + 1, j)], grid[(i, j + 1)]))
                if i + j + 1 < freq:
                    tris.append((grid[(i + 1, j)], grid[(i + 1, j + 1)], grid[(i, j + 1)]))
    out = []
    for a, b, c in tris:
        n = cross(sub(b, a), sub(c, a))
        if dot(n, a) < 0:
            b, c = c, b
        out.append(tuple((tuple(v * radius for v in p), p) for p in (a, b, c)))
    return out


# ---- simple solids from triangles -------------------------------------------------------

def outward_tri(a, b, c, ref_center=(0.0, 0.0, 0.0)):
    """A flat triangle `a, b, c` wound so its normal points away from `ref_center`."""
    n = norm(cross(sub(b, a), sub(c, a)))
    mid = scale(add(add(a, b), c), 1.0 / 3.0)
    if dot(n, sub(mid, ref_center)) < 0.0:
        b, c = c, b
        n = (-n[0], -n[1], -n[2])
    return ((a, n), (b, n), (c, n))


def bipyramid(rx, ry, rz):
    """Octahedron with unequal half-axes: 8 triangles. `(0.75, 1.08, 0.75)` is a gem shape."""
    pts = [
        (0.0, ry, 0.0), (0.0, -ry, 0.0),
        (rx, 0.0, 0.0), (-rx, 0.0, 0.0),
        (0.0, 0.0, rz), (0.0, 0.0, -rz),
    ]
    ring = [pts[2], pts[4], pts[3], pts[5]]
    tris = []
    for i in range(4):
        p0, p1 = ring[i], ring[(i + 1) % 4]
        tris.append(outward_tri(pts[0], p1, p0))
        tris.append(outward_tri(pts[1], p0, p1))
    return tris


def star_prism(r_outer=1.05, r_inner=0.48, z_depth=0.38, points=5):
    """A star of `points` points in the XY plane, pushed out front and back to a point at
    +/- `z_depth`: 4 * points triangles."""
    count = 2 * points
    ring = []
    for i in range(count):
        ang = math.pi * 0.5 + i * (math.pi / points)
        rad = r_outer if i % 2 == 0 else r_inner
        ring.append((math.cos(ang) * rad, math.sin(ang) * rad, 0.0))
    front, back = (0.0, 0.0, z_depth), (0.0, 0.0, -z_depth)
    tris = []
    for i in range(count):
        a, b = ring[i], ring[(i + 1) % count]
        tris.append(outward_tri(front, a, b))
        tris.append(outward_tri(back, b, a))
    return tris


def box(hx, hy, hz):
    """An axis-aligned box of half-extents `hx, hy, hz`: 12 triangles."""
    pts = [
        (-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
        (-hx, -hy, hz), (hx, -hy, hz), (hx, hy, hz), (-hx, hy, hz),
    ]
    quads = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (0, 4, 7, 3), (1, 2, 6, 5)]
    tris = []
    for i0, i1, i2, i3 in quads:
        tris.append(outward_tri(pts[i0], pts[i1], pts[i2]))
        tris.append(outward_tri(pts[i0], pts[i2], pts[i3]))
    return tris


def stellate(points, circumradius, height):
    """Each face of the hull of `points` becomes three triangles meeting at a tip pushed out to
    `height` times the circumradius: a spiked version of the solid."""
    base, _ = flat_mesh(points, circumradius)
    tris = []
    for (a, _), (b, _), (c, _) in base:
        mid = scale(add(add(a, b), c), 1.0 / 3.0)
        tip = scale(norm(mid), height)
        tris.append(outward_tri(a, b, tip))
        tris.append(outward_tri(b, c, tip))
        tris.append(outward_tri(c, a, tip))
    return tris


# ---- indexed meshes ---------------------------------------------------------------------

@dataclass(frozen=True)
class Mesh:
    """Shared vertices and index triples, counter-clockwise seen from outside."""

    vertices: list
    faces: list

    def flat_triangles(self):
        """The same mesh as flat-shaded `(position, normal)` triangles."""
        normals = face_normals(self.vertices, self.faces)
        return [tuple((self.vertices[i], n) for i in f) for f, n in zip(self.faces, normals)]


def mesh_from_triangles(tris, tol=1e-9) -> Mesh:
    """Merge the positions of a triangle mesh into an indexed mesh (first occurrence first)."""
    vertices: list = []
    index: dict = {}
    faces = []
    for tri in tris:
        ids = []
        for pos, _ in tri:
            key = tuple(round(c / tol) for c in pos) if tol else pos
            if key not in index:
                index[key] = len(vertices)
                vertices.append(tuple(pos))
            ids.append(index[key])
        faces.append(tuple(ids))
    return Mesh(vertices, faces)


def solid_mesh(name, circumradius=1.0, negative_zero=False) -> Mesh:
    """An indexed, fan-triangulated mesh of a named solid in `SOLIDS`."""
    if name not in SOLIDS:
        raise KeyError(f"unknown solid {name!r}; use one of {sorted(SOLIDS)}")
    tris, _ = flat_mesh(SOLIDS[name](negative_zero), circumradius)
    return mesh_from_triangles(tris)


def icosphere_mesh(freq, radius=1.0) -> Mesh:
    """An indexed icosphere: 10 * freq**2 + 2 vertices, 20 * freq**2 faces."""
    return mesh_from_triangles(icosphere(freq, radius))


def orient_faces(vertices, faces):
    """Faces with any whose normal points toward the origin flipped, for meshes whose centre is the origin."""
    out = []
    for face in faces:
        a, b, c = (vertices[i] for i in face)
        normal = cross(sub(b, a), sub(c, a))
        center = tuple((a[i] + b[i] + c[i]) / 3.0 for i in range(3))
        out.append((face[0], face[2], face[1]) if dot(normal, center) < 0 else tuple(face))
    return out


def face_normals(vertices, faces):
    """Unit face normals; a degenerate face gets `(0, 1, 0)`."""
    out = []
    for face in faces:
        a, b, c = (vertices[i] for i in face)
        n = cross(sub(b, a), sub(c, a))
        length = math.sqrt(dot(n, n))
        out.append(tuple(x / length for x in n) if length else (0.0, 1.0, 0.0))
    return out
