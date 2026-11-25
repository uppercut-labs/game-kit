import pytest

import uppercut_poly_meshes as pm

TRI = [(((0.0, 0.5, 1.0), (0.0, 0.0, 1.0)), ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0)), ((0.0, 1.0, 0.0), (0.0, 0.0, 1.0)))]


def test_nested_layout():
    lines = pm.emit_triangle_array("MESH_A", TRI, "Vertex", "nested")
    assert lines[0] == "static const Vertex MESH_A[3] = {"
    assert lines[1] == "    { { 0.00000f, 0.50000f, 1.00000f }, { 0.00000f, 0.00000f, 1.00000f } },"
    assert lines[-2:] == ["};", ""]


def test_flat_layout_digits_and_storage():
    lines = pm.emit_triangle_array("m", TRI, "V", "flat", digits=3, storage="const")
    assert lines[0] == "const V m[3] = {"
    assert lines[1] == "    { 0.000f, 0.500f, 1.000f, 0.000f, 0.000f, 1.000f },"
    with pytest.raises(ValueError):
        pm.emit_triangle_array("m", TRI, "V", "other")


def test_table():
    lines = pm.emit_mesh_table("const T TABLE[2]", [("Cube", "MESH_CUBE", 36, 6), ("Sphere", "MESH_SPHERE", 540, 180)])
    assert lines == [
        "const T TABLE[2] = {",
        '    { "Cube", MESH_CUBE, 36u, 6u },',
        '    { "Sphere", MESH_SPHERE, 540u, 180u },',
        "};",
    ]


def test_fixed_point():
    assert pm.fixed(0.5, 32) == 16 and pm.fixed(-0.5, 32) == -16
    assert pm.fixed(0.5 / 32, 32) == 0 and pm.fixed(1.5 / 32, 32) == 2  # halves go to even
    rows = pm.emit_fixed_vertices("static const SVECTOR v[]", [(1.0, -1.0, 0.0)], 32)
    assert rows == ["static const SVECTOR v[] = {", "    {   32,  -32,    0, 0 },", "};"]
    rows = pm.emit_fixed_vertices("v", [(1.0, 0.0, 0.0)], 16, width=2, extra=())
    assert rows[1] == "    { 16,  0,  0 },"


def test_indexed_faces_and_macro_rows():
    rows = pm.emit_indexed_faces("static const F f[]", [(0, 1, 2)], [(0.0, 0.0, 1.0)])
    assert rows[1] == "    {   0,   1,   2,     0,     0,  4096 },"
    rows = pm.emit_macro_rows("p", [(0.5, -0.25, 1.0)], "VEC3", 2.0, 3)
    assert rows[1] == "    VEC3(1.000, -0.500, 2.000),"
