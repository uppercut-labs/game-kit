# uppercut-poly-meshes

Regular and semi-regular polyhedron meshes for console homebrew, and the C arrays to put them in a build. It has exact vertex sets for the cube, octahedron, dodecahedron, icosahedron, truncated icosahedron and rhombicosidodecahedron, an exact convex hull (no SciPy), fan triangulation with flat normals, a frequency-subdivided icosphere with smooth normals, a few simple solids (bipyramid, box, star prism, spiked solid), indexed meshes, and emitters for float vertex structs, mesh tables, fixed-point vertex rows, indexed faces with fixed-point normals and macro-initialised float rows. It uses the Python standard library only: no NumPy, SciPy or trimesh.

## Install

```sh
pip install uppercut-poly-meshes
```

## Example

```python
import uppercut_poly_meshes as pm

tris, polygon_faces = pm.flat_mesh(pm.SOLIDS["dodecahedron"](), 1.12)   # 36 triangles, 12 faces
lines = pm.emit_triangle_array("MESH_DODECAHEDRON", tris, "GameVertex", layout="nested")
sphere = pm.emit_triangle_array("MESH_SPHERE", pm.icosphere(3, 1.0), "GameVertex", layout="nested")
table = pm.emit_mesh_table("const GameMesh FORM_MESHES[FORM_MESH_COUNT]",
                           [("Dodecahedron", "MESH_DODECAHEDRON", len(tris) * 3, polygon_faces)])
open("meshes.c", "w").write("\n".join(lines + sphere + table) + "\n")

# Indexed, fixed point (a 3D unit with 1.0 = 32 and normals at 1.0 = 4096):
mesh = pm.solid_mesh("icosahedron")
faces = pm.orient_faces(mesh.vertices, mesh.faces)
rows = pm.emit_fixed_vertices("static const Vec16 ico_vertices[]", mesh.vertices, 32)
rows += pm.emit_indexed_faces("static const Face ico_faces[]", faces, pm.face_normals(mesh.vertices, faces))
```

The package writes arrays and tables. A project's own struct typedefs, header guards and `#define` lines stay in the project, because the type names are the project's.

## Functions

| Function | What it does |
| --- | --- |
| `SOLIDS[name](negative_zero=False)` | Vertex set of `cube`, `octahedron`, `dodecahedron`, `icosahedron`, `truncated_icosahedron` or `rhombicosidodecahedron`, also as the functions of the same name. |
| `hull_faces(points)` | Exact hull of a small point set: `(normal, member indices)` per polygon face, members counter-clockwise. Cost grows with the cube of the point count (60 points take about a second). |
| `flat_mesh(points, circumradius)` | Fan-triangulated, flat-normal triangles scaled to the circumradius, and the polygon face count. |
| `icosphere(freq, radius, negative_zero=False)` | `20 * freq**2` triangles with smooth normals. |
| `bipyramid(rx, ry, rz)`, `box(hx, hy, hz)`, `star_prism(...)`, `stellate(points, circumradius, height)`, `outward_tri(a, b, c, ref_center)` | Simple solids and the triangle builder they use. |
| `Mesh(vertices, faces)`, `solid_mesh(name, circumradius=1.0)`, `icosphere_mesh(freq, radius=1.0)`, `mesh_from_triangles(tris)` | Indexed meshes. |
| `orient_faces(vertices, faces)`, `face_normals(vertices, faces)` | Flip faces that point inward; unit normals. |
| `emit_triangle_array(symbol, tris, type_name, layout="nested" or "flat", digits=5)` | Float vertex array, three vertices per triangle, `f` suffixes. |
| `emit_mesh_table(declaration, entries)` | A table of `{ "label", symbol, vertex_count, face_count }`. |
| `emit_fixed_vertices(declaration, vertices, scale, width, extra)` | Integer vertex rows. |
| `emit_indexed_faces(declaration, faces, normals, ...)` | Index triples with fixed-point normals. |
| `emit_macro_rows(declaration, vertices, macro, scale, digits)` | `MACRO(x, y, z),` rows. |
| `fixed(value, scale)` | `int(round(value * scale))`. |
| `add`, `sub`, `scale`, `dot`, `cross`, `norm`, `PHI` | Vector helpers. |

Every emitter returns a list of lines. The vertex arrays are `static const` by default and end with a blank line.

## Where the four source copies differ

The code was written four times: three pure-Python generators that write float arrays (two with the same nested vertex layout, one with a flat layout) and a fourth that builds hulls with trimesh and writes fixed-point and macro-initialised arrays.

| Difference | What the package does |
| --- | --- |
| Two copies keep zero as `0`, one multiplies by float signs and prints `-0.00000` for some coordinates and normals. The solids are otherwise identical. | `negative_zero=False` (default) gives the first, `negative_zero=True` the second. |
| The icosphere is frequency 4 in one copy and 3 in the others. | `freq` is an argument. |
| Face counts: the float copies report polygon faces for solids and triangles for the sphere. | `flat_mesh` returns polygon faces, `len(icosphere(...))` is the triangle count; the caller writes whichever its table wants. |
| Only one copy has the two Archimedean solids; only the flat-layout copy has props. | All six vertex sets are in `SOLIDS`. The generic props are `bipyramid`, `star_prism`, `box`, `stellate`; shapes that are particular to a game (a horned enemy) are built from `outward_tri` in the game's own script. |
| The trimesh copy gets vertices and faces from the trimesh hull and icosphere; their order is trimesh's. | The package has its own hull and icosphere, so its vertex and face order is different. The emitters take any vertices and faces; give them trimesh's and the text is the same as the copy's. |
| Rounding: the copies scale hull points as `c * radius / r` or `c * (radius / r)`. | The first form. The text printed to five places is identical for every shape in the tests. |
| Header text: each copy writes its own struct typedefs and platform macros. | Not emitted. |

## Tests

`pytest` needs nothing installed. `tests/golden.json` holds the SHA-256 of the C text the original scripts wrote for the shapes the tests build (38 cases: the float arrays and a mesh table of two copies, the flat-layout arrays and props, and the fixed-point and macro-initialised arrays of the trimesh copy fed this package's meshes). `tests/make_goldens.py` rebuilds it from the four scripts' paths (it needs NumPy and trimesh, and only for this). Other tests check face and vertex counts, edge lengths, outward normals and the Euler characteristic.

## License

MIT
