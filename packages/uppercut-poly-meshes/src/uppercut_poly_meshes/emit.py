"""C source emitters. Each returns a list of lines (join with "\\n"); none writes a file.

The layouts are the ones the console copies used: float vertex structs (nested or flat),
a table of meshes, fixed-point vertex rows, indexed faces with fixed-point normals, and
macro-initialised float rows. Type names, macro names and header files are arguments or
left to the caller; the package never writes a project's own header.
"""

from __future__ import annotations

from typing import Sequence


def fixed(value: float, scale: float) -> int:
    """`value` in fixed point: `int(round(value * scale))`, rounding halves to even."""
    return int(round(value * scale))


def emit_triangle_array(
    symbol: str,
    tris: Sequence,
    type_name: str,
    layout: str = "nested",
    digits: int = 5,
    storage: str = "static const",
) -> list[str]:
    """A C array of float vertices, three per triangle, each with a position and a normal.

    `layout="nested"` writes `{ { px, py, pz }, { nx, ny, nz } },` for a struct holding two
    float[3]; `layout="flat"` writes `{ px, py, pz, nx, ny, nz },`. Floats carry an `f` suffix.
    The last line is blank, as both copies left it.
    """
    if layout not in ("nested", "flat"):
        raise ValueError("layout must be 'nested' or 'flat'")
    lines = [f"{storage} {type_name} {symbol}[{len(tris) * 3}] = {{"]
    f = f"%.{digits}ff"
    for tri in tris:
        for pos, nrm in tri:
            if layout == "nested":
                lines.append("    { { %s, %s, %s }, { %s, %s, %s } }," % tuple(f % c for c in (*pos, *nrm)))
            else:
                lines.append("    { %s, %s, %s, %s, %s, %s }," % tuple(f % c for c in (*pos, *nrm)))
    lines.append("};")
    lines.append("")
    return lines


def emit_mesh_table(declaration: str, entries: Sequence[tuple]) -> list[str]:
    """`declaration = {` then one `{ "label", symbol, 36u, 12u },` row per `(label, symbol, vertex_count,
    face_count)` entry, then `};`. `declaration` is everything before ` = {`, such as
    `const GameMesh FORM_MESHES[FORM_MESH_COUNT]`."""
    lines = [f"{declaration} = {{"]
    for label, symbol, vertex_count, face_count in entries:
        lines.append(f'    {{ "{label}", {symbol}, {vertex_count}u, {face_count}u }},')
    lines.append("};")
    return lines


def emit_fixed_vertices(
    declaration: str,
    vertices: Sequence,
    scale: float = 32,
    width: int = 4,
    extra: Sequence[object] = (0,),
) -> list[str]:
    """Integer vertex rows, `{ x, y, z, 0 },`, each coordinate `fixed(c, scale)` right-aligned to
    `width`. `extra` values (default one zero, a padding word) follow the three coordinates."""
    lines = [f"{declaration} = {{"]
    for x, y, z in vertices:
        tail = "".join(f", {e}" for e in extra)
        lines.append(f"    {{ {fixed(x, scale):{width}d}, {fixed(y, scale):{width}d}, {fixed(z, scale):{width}d}{tail} }},")
    lines.append("};")
    return lines


def emit_indexed_faces(
    declaration: str,
    faces: Sequence,
    normals: Sequence,
    index_width: int = 3,
    normal_scale: float = 4096,
    normal_width: int = 5,
) -> list[str]:
    """Rows `{ i0, i1, i2, nx, ny, nz },` with the normal in fixed point (4096 is 1.0 by default)."""
    lines = [f"{declaration} = {{"]
    for face, normal in zip(faces, normals):
        nx, ny, nz = (fixed(v, normal_scale) for v in normal)
        lines.append(
            f"    {{ {face[0]:{index_width}d}, {face[1]:{index_width}d}, {face[2]:{index_width}d}, "
            f"{nx:{normal_width}d}, {ny:{normal_width}d}, {nz:{normal_width}d} }},"
        )
    lines.append("};")
    return lines


def emit_macro_rows(
    declaration: str,
    vertices: Sequence,
    macro: str,
    scale: float = 1.0,
    digits: int = 7,
) -> list[str]:
    """Rows `MACRO(x, y, z),` with each coordinate multiplied by `scale` and written to `digits` places,
    for a fixed-point type built by a macro or constructor."""
    lines = [f"{declaration} = {{"]
    for x, y, z in vertices:
        lines.append(f"    {macro}({x * scale:.{digits}f}, {y * scale:.{digits}f}, {z * scale:.{digits}f}),")
    lines.append("};")
    return lines
