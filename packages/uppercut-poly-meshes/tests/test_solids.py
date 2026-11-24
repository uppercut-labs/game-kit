import math

import pytest

import uppercut_poly_meshes as pm

# name: (vertices, polygon faces, triangles after fan triangulation)
COUNTS = {
    "cube": (8, 6, 12),
    "octahedron": (6, 8, 8),
    "dodecahedron": (20, 12, 36),
    "icosahedron": (12, 20, 20),
    "truncated_icosahedron": (60, 32, 116),
    "rhombicosidodecahedron": (60, 62, 116),
}


@pytest.mark.parametrize("name", sorted(COUNTS))
def test_counts(name):
    verts, faces, tris = COUNTS[name]
    points = pm.SOLIDS[name]()
    assert len(points) == verts
    mesh, polygon_count = pm.flat_mesh(points, 1.0)
    assert polygon_count == faces and len(mesh) == tris
    solid = pm.solid_mesh(name)
    assert len(solid.vertices) == verts and len(solid.faces) == tris


@pytest.mark.parametrize("name", sorted(COUNTS))
def test_vertices_sit_on_the_circumsphere_and_normals_point_out(name):
    mesh, _ = pm.flat_mesh(pm.SOLIDS[name](), 1.5)
    for tri in mesh:
        for pos, nrm in tri:
            assert math.sqrt(sum(c * c for c in pos)) == pytest.approx(1.5)
            assert math.sqrt(sum(c * c for c in nrm)) == pytest.approx(1.0)
        a, b, c = (p for p, _ in tri)
        normal = pm.cross(pm.sub(b, a), pm.sub(c, a))
        assert pm.dot(normal, tri[0][1]) > 0  # winding agrees with the stored normal
        assert pm.dot(tri[0][1], a) > 0  # and it points away from the centre


def test_platonic_polygons_have_equal_edges():
    for name in ("cube", "octahedron", "dodecahedron", "icosahedron", "truncated_icosahedron", "rhombicosidodecahedron"):
        points = pm.SOLIDS[name]()
        lengths = set()
        for _normal, members in pm.hull_faces(points):
            for i, m in enumerate(members):
                lengths.add(round(math.dist(points[m], points[members[(i + 1) % len(members)]]), 6))
        assert len(lengths) == 1, (name, lengths)


def test_euler_characteristic_of_indexed_meshes():
    for name in COUNTS:
        mesh = pm.solid_mesh(name)
        edges = {frozenset((f[i], f[(i + 1) % 3])) for f in mesh.faces for i in range(3)}
        assert len(mesh.vertices) - len(edges) + len(mesh.faces) == 2


@pytest.mark.parametrize("freq", [1, 2, 3, 4])
def test_icosphere_counts(freq):
    tris = pm.icosphere(freq, 2.0)
    assert len(tris) == 20 * freq * freq
    for tri in tris:
        for pos, nrm in tri:
            assert math.sqrt(sum(c * c for c in pos)) == pytest.approx(2.0)
            assert math.sqrt(sum(c * c for c in nrm)) == pytest.approx(1.0)
    mesh = pm.icosphere_mesh(freq)
    assert len(mesh.vertices) == 10 * freq * freq + 2 and len(mesh.faces) == 20 * freq * freq


def test_negative_zero_flag_changes_only_the_sign_of_zero():
    plain, flagged = pm.octahedron(), pm.octahedron(negative_zero=True)
    assert sorted(plain) == sorted(flagged)
    assert not any(math.copysign(1.0, c) < 0 and c == 0 for p in plain for c in p)
    assert any(math.copysign(1.0, c) < 0 and c == 0 for p in flagged for c in p)


def test_simple_solids_have_the_right_triangle_counts_and_face_outward():
    for tris, count in ((pm.bipyramid(1, 2, 1), 8), (pm.box(1, 1, 1), 12), (pm.star_prism(), 20), (pm.stellate(pm.cube(), 1.0, 1.5), 36)):
        assert len(tris) == count
        for tri in tris:
            centroid = pm.scale(pm.add(pm.add(tri[0][0], tri[1][0]), tri[2][0]), 1 / 3)
            assert pm.dot(tri[0][1], centroid) > 0


def test_outward_tri_flips_to_face_away_from_the_reference():
    flipped = pm.outward_tri((0, 0, 1), (1, 0, 1), (0, 1, 1), ref_center=(0, 0, 5))
    assert flipped[0][1] == (0.0, 0.0, -1.0)
    up = pm.outward_tri((0, 0, 1), (1, 0, 1), (0, 1, 1))
    assert up[0][1] == (0.0, 0.0, 1.0)


def test_mesh_from_triangles_merges_positions_and_flat_triangles_round_trip():
    mesh = pm.solid_mesh("octahedron", 2.0)
    again = pm.mesh_from_triangles(mesh.flat_triangles())
    assert again.vertices == mesh.vertices and again.faces == mesh.faces


def test_orient_faces_and_normals():
    vertices = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)]
    assert pm.orient_faces(vertices, [(0, 1, 2)]) == [(0, 1, 2)]
    assert pm.orient_faces(vertices, [(0, 2, 1)]) == [(0, 1, 2)]
    assert pm.face_normals(vertices, [(0, 1, 2)])[0] == pytest.approx((3**-0.5,) * 3)
    assert pm.face_normals(vertices, [(0, 0, 1)])[0] == (0.0, 1.0, 0.0)


def test_unknown_solid():
    with pytest.raises(KeyError):
        pm.solid_mesh("tetrahedron")
