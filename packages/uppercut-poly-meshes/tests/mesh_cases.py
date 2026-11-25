"""The package's C text for each synthetic shape, and (for make_goldens.py) the same text taken from the
original scripts. Shapes are plain regular and semi-regular solids; nothing here is game data."""

from __future__ import annotations

import hashlib
import re

import uppercut_poly_meshes as pm

# (key, label, circumradius) in the order the scripts listed them.
N64_FORMS = [
    ("cube", "Cube", 1.18),
    ("octahedron", "Octahedron", 1.22),
    ("dodecahedron", "Dodecahedron", 1.12),
    ("icosahedron", "Icosahedron", 1.10),
    ("truncated_icosahedron", "Truncated Icosahedron", 1.06),
    ("rhombicosidodecahedron", "Rhombicosidodecahedron", 1.04),
]
CORE_FORMS = N64_FORMS[:4]
PSX_SHAPES = [("cube", 4), ("octahedron", 8), ("dodecahedron", 12), ("icosahedron", 20), ("sphere", 360)]


def sha(text: str) -> str:
    """SHA-256 of the text with trailing blank lines dropped (the copies end arrays differently)."""
    return hashlib.sha256((text.rstrip() + "\n").encode()).hexdigest()


def _text(lines) -> str:
    return "\n".join(lines) + "\n"


def float_arrays() -> dict[str, str]:
    out: dict[str, str] = {}
    for key, _label, radius in N64_FORMS:
        tris, _ = pm.flat_mesh(pm.SOLIDS[key](), radius)
        out[f"n64.{key}"] = _text(pm.emit_triangle_array(f"MESH_{key.upper()}", tris, "UBN64Vertex", "nested"))
    out["n64.sphere"] = _text(pm.emit_triangle_array("MESH_SPHERE", pm.icosphere(4, 1.0), "UBN64Vertex", "nested"))
    for key, _label, radius in CORE_FORMS:
        tris, _ = pm.flat_mesh(pm.SOLIDS[key](), radius)
        out[f"naomi.{key}"] = _text(pm.emit_triangle_array(f"MESH_{key.upper()}", tris, "UBNAOMIVertex", "nested"))
    out["naomi.sphere"] = _text(pm.emit_triangle_array("MESH_SPHERE", pm.icosphere(3, 1.0), "UBNAOMIVertex", "nested"))
    for key, _label, radius in CORE_FORMS:
        tris, _ = pm.flat_mesh(pm.SOLIDS[key](negative_zero=True), radius)
        out[f"dreamcast.{key}"] = _text(
            pm.emit_triangle_array(f"MESH_FORM_{key.upper()}", tris, "UBDCVert", "flat")
        )
    out["dreamcast.sphere"] = _text(
        pm.emit_triangle_array("MESH_FORM_SPHERE", pm.icosphere(3, 1.0, negative_zero=True), "UBDCVert", "flat")
    )
    solids = {
        "diamond": pm.bipyramid(0.75, 1.08, 0.75),
        "star": pm.star_prism(),
        "health": pm.bipyramid(0.85, 0.85, 0.85),
        "spiky": pm.stellate(pm.octahedron(negative_zero=True), 0.72, 1.28),
        "box": pm.box(1.0, 1.0, 1.0),
    }
    for key, tris in solids.items():
        out[f"dreamcast.prop.{key}"] = _text(pm.emit_triangle_array(f"MESH_PROP_{key.upper()}", tris, "UBDCVert", "flat"))
    return out


def mesh_tables() -> dict[str, str]:
    entries = []
    for key, label, radius in N64_FORMS:
        tris, faces = pm.flat_mesh(pm.SOLIDS[key](), radius)
        entries.append((label, f"MESH_{key.upper()}", len(tris) * 3, faces))
    sphere = pm.icosphere(4, 1.0)
    entries.append(("Sphere", "MESH_SPHERE", len(sphere) * 3, len(sphere)))
    return {"n64.table": _text(pm.emit_mesh_table("const UBN64Mesh UB_N64_FORM_MESHES[UB_N64_FORM_MESH_COUNT]", entries))}


def shape_meshes() -> list[tuple[str, "pm.Mesh", int]]:
    """The five shapes of the fixed-point scripts as indexed meshes, built by this package."""
    meshes = [(name, pm.solid_mesh(name), logical) for name, logical in PSX_SHAPES[:4]]
    meshes.append(("sphere", pm.icosphere_mesh(1), PSX_SHAPES[4][1]))
    return meshes


def fixed_point_text() -> dict[str, str]:
    out: dict[str, str] = {}
    for name, mesh, _ in shape_meshes():
        faces = pm.orient_faces(mesh.vertices, mesh.faces)
        normals = pm.face_normals(mesh.vertices, faces)
        out[f"psx.{name}.vertices"] = _text(pm.emit_fixed_vertices(f"static const SVECTOR {name}_vertices[]", mesh.vertices, 32))
        out[f"psx.{name}.faces"] = _text(pm.emit_indexed_faces(f"static const UBPSXFace {name}_faces[]", faces, normals))
        out[f"saturn.{name}.points"] = _text(
            pm.emit_macro_rows(f"static const fix16_vec3_t {name}_points[]", mesh.vertices, "FIX16_VEC3_INITIALIZER", 2.0)
        )
    return out


def package_cases() -> dict[str, str]:
    out = {}
    for group in (float_arrays(), mesh_tables(), fixed_point_text()):
        out.update({k: sha(v) for k, v in group.items()})
    return out


# ---- the copies' side (make_goldens.py only) ----------------------------------------------

def _chunks(c_text: str) -> dict[str, str]:
    """Each `static const T NAME[...] = { ... };` block of a generated C file, keyed by NAME."""
    return {
        m.group(1): m.group(0)
        for m in re.finditer(r"static const [\w ]+? (\w+)\[\d*\] = \{\n.*?\n\};\n", c_text, re.S)
    }


def _emit(module, name, *args):
    import os
    import sys
    import tempfile

    folder = tempfile.mkdtemp()
    paths = [os.path.join(folder, f) for f in ("out.c", "out.h")]
    old = sys.argv
    sys.argv = [name, *paths]
    try:
        module.emit()
    finally:
        sys.argv = old
    return open(paths[0]).read()


def copy_cases(n64, naomi, dc, shapes, np) -> dict[str, str]:
    """{label: sha256} from the original scripts (`shapes` is the generate_shapes module)."""
    import os
    import tempfile
    import types

    res: dict[str, str] = {}
    chunks = _chunks(_emit(n64, "n64"))
    for key, _label, _r in N64_FORMS:
        res[f"n64.{key}"] = sha(chunks[f"MESH_{key.upper()}"])
    res["n64.sphere"] = sha(chunks["MESH_SPHERE"])
    n64_c = _emit(n64, "n64")
    table = re.search(r"const UBN64Mesh UB_N64_FORM_MESHES.*?\n\};\n", n64_c, re.S).group(0)
    res["n64.table"] = sha(table)
    for key, _label, _r in CORE_FORMS:
        res[f"naomi.{key}"] = sha(_chunks(_emit(naomi, "naomi"))[f"MESH_{key.upper()}"])
    res["naomi.sphere"] = sha(_chunks(_emit(naomi, "naomi"))["MESH_SPHERE"])
    chunks = _chunks(_emit(dc, "dc"))
    for key, _label, _r in CORE_FORMS:
        res[f"dreamcast.{key}"] = sha(chunks[f"MESH_FORM_{key.upper()}"])
    res["dreamcast.sphere"] = sha(chunks["MESH_FORM_SPHERE"])
    for key in ("diamond", "star", "health", "spiky", "box"):
        res[f"dreamcast.prop.{key}"] = sha(chunks[f"MESH_PROP_{key.upper()}"])
    # The trimesh script: hand it duck-typed meshes built from this package's own shapes.
    root = tempfile.mkdtemp()
    for sub in ("psx/include", "psx/src", "saturn"):
        os.makedirs(os.path.join(root, sub))
    shapes.PSX_DIR = __import__("pathlib").Path(root, "psx")
    shapes.SATURN_DIR = __import__("pathlib").Path(root, "saturn")
    fakes = []
    for name, mesh, logical in shape_meshes():
        fake = types.SimpleNamespace(vertices=np.array(mesh.vertices, dtype=float), faces=np.array(mesh.faces, dtype=np.int64))
        fakes.append((name, fake, logical))
    shapes.write_psx(fakes)
    shapes.write_saturn(fakes)
    psx = _chunks(open(os.path.join(root, "psx", "src", "ub_psx_generated_shapes.c")).read())
    sat = _chunks(open(os.path.join(root, "saturn", "generated_shapes.c")).read())
    for name, _mesh, _logical in shape_meshes():
        res[f"psx.{name}.vertices"] = sha(psx[f"{name}_vertices"])
        res[f"psx.{name}.faces"] = sha(psx[f"{name}_faces"])
        res[f"saturn.{name}.points"] = sha(sat[f"{name}_points"])
    return res
