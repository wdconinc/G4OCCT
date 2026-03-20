#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
"""Generate triple-box-v1/shape.step without requiring DRAWEXE.

Produces a STEP AP214 (AUTOMOTIVE_DESIGN) file containing three free-shape
PRODUCT records, each named 'Component', with 10x10x10 mm box geometry at
x = -12, 0, and +12 mm.  The BRep encoding (MANIFOLD_SOLID_BREP with
ADVANCED_BREP_SHAPE_REPRESENTATION) matches the format produced by OCCT's
STEPCAFControl_Writer, which is what generate.tcl (DRAWEXE WriteStep) emits.

G4OCCTAssemblyVolume::FromSTEP reads the PRODUCT names as material-map keys.
"""

import sys
from pathlib import Path


# ─── Formatting helpers ────────────────────────────────────────────────────────

def _fmt(v: float) -> str:
    if v == 0.0:
        return "0."
    s = f"{v:.10G}"
    if "." not in s and "E" not in s:
        s += "."
    elif "." in s:
        s = s.rstrip("0")
        if s.endswith("."):
            pass  # keep "10." etc.
    return s


def fmt3(x: float, y: float, z: float) -> str:
    return f"({_fmt(x)},{_fmt(y)},{_fmt(z)})"


def fmt2(u: float, v: float) -> str:
    return f"({_fmt(u)},{_fmt(v)})"


# ─── STEP writer ───────────────────────────────────────────────────────────────

class StepWriter:
    """Accumulates STEP DATA entities and writes the complete file."""

    def __init__(self) -> None:
        self._next_id: int = 0
        self._lines: list[str] = []

    def alloc(self) -> int:
        self._next_id += 1
        return self._next_id

    def add(self, entity_id: int, text: str) -> None:
        self._lines.append(f"#{entity_id} = {text};")

    def write_to(self, path: str) -> None:
        header = [
            "ISO-10303-21;",
            "HEADER;",
            "FILE_DESCRIPTION(('Open CASCADE Model'),'2;1');",
            "FILE_NAME('Open CASCADE Shape Model','2024-01-01T00:00:00',('Author'),(",
            "    'Open CASCADE'),'Open CASCADE STEP processor 7.9','Open CASCADE 7.9'",
            "  ,'Unknown');",
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));",
            "ENDSEC;",
            "DATA;",
        ]
        footer = ["ENDSEC;", "END-ISO-10303-21;"]
        Path(path).write_text(
            "\n".join(header + self._lines + footer) + "\n",
            encoding="utf-8",
        )


# ─── 2D geometry context (STEP inline compound entity) ────────────────────────

_GEOM2D = (
    "( GEOMETRIC_REPRESENTATION_CONTEXT(2)\n"
    "PARAMETRIC_REPRESENTATION_CONTEXT()"
    " REPRESENTATION_CONTEXT('2D SPACE',''\n"
    "  ) )"
)


# ─── Box BRep generator ────────────────────────────────────────────────────────

def _add_box(
    w: StepWriter,
    cx: float,
    cy: float,
    cz: float,
    side: float,
    geom3d_context_id: int,
    app_context_id: int,
    product_name: str,
) -> int:
    """Add one box centred at (cx,cy,cz) with the given side length.

    Returns the SHAPE_DEFINITION_REPRESENTATION entity id (XDE root for this part).
    The topology follows the face/edge ordering produced by OCCT's
    BRepPrimAPI_MakeBox, verified against the existing direct-primitives STEP
    fixtures.
    """
    S = side
    x0, x1 = cx - S / 2, cx + S / 2
    y0, y1 = cy - S / 2, cy + S / 2
    z0, z1 = cz - S / 2, cz + S / 2

    # ── 8 vertices (OCCT BRepPrimAPI_MakeBox ordering) ────────────────────
    vcoords = [
        (x0, y0, z0),  # v0
        (x1, y0, z0),  # v1
        (x1, y1, z0),  # v2
        (x0, y1, z0),  # v3
        (x0, y0, z1),  # v4
        (x1, y0, z1),  # v5
        (x1, y1, z1),  # v6
        (x0, y1, z1),  # v7
    ]
    vvp = []  # VERTEX_POINT ids
    for vx, vy, vz in vcoords:
        cp = w.alloc()
        vp = w.alloc()
        w.add(cp, f"CARTESIAN_POINT('',{fmt3(vx, vy, vz)})")
        w.add(vp, f"VERTEX_POINT('',#{cp})")
        vvp.append(vp)

    # ── 6 face planes (origin, axis=normal, ref_dir=U) ────────────────────
    # Face order: F0=LEFT(x=x0), F1=RIGHT(x=x1), F2=FRONT(y=y0),
    #             F3=BACK(y=y1),  F4=BOTTOM(z=z0), F5=TOP(z=z1)
    # same_sense: .F. for F0,F2,F4; .T. for F1,F3,F5
    # face_bound_sense mirrors same_sense
    face_plane_defs = [
        #  origin              axis (plane normal in STEP) ref_direction
        ((x0, y0, z0), (1, 0, 0), (0, 0, 1)),  # F0 LEFT
        ((x1, y0, z0), (1, 0, 0), (0, 0, 1)),  # F1 RIGHT
        ((x0, y0, z0), (0, 1, 0), (0, 0, 1)),  # F2 FRONT
        ((x0, y1, z0), (0, 1, 0), (0, 0, 1)),  # F3 BACK
        ((x0, y0, z0), (0, 0, 1), (1, 0, 0)),  # F4 BOTTOM
        ((x0, y0, z1), (0, 0, 1), (1, 0, 0)),  # F5 TOP
    ]
    plane_ids = []
    for (ox, oy, oz), (ax, ay, az), (rx, ry, rz) in face_plane_defs:
        o_id = w.alloc()
        a_id = w.alloc()
        r_id = w.alloc()
        p3_id = w.alloc()
        pl_id = w.alloc()
        w.add(o_id, f"CARTESIAN_POINT('',{fmt3(ox, oy, oz)})")
        w.add(a_id, f"DIRECTION('',{fmt3(ax, ay, az)})")
        w.add(r_id, f"DIRECTION('',{fmt3(rx, ry, rz)})")
        w.add(p3_id, f"AXIS2_PLACEMENT_3D('',#{o_id},#{a_id},#{r_id})")
        w.add(pl_id, f"PLANE('',#{p3_id})")
        plane_ids.append(pl_id)

    # ── Shared 2D geometry context for all PCURVEs in this box ────────────
    g2d = w.alloc()
    w.add(g2d, _GEOM2D)

    # ── UV parameterisation per face ──────────────────────────────────────
    # Derived from OCCT AXIS2_PLACEMENT_3D of each face plane:
    #   U_3D = ref_direction (Gram-Schmidt vs axis)
    #   V_3D = axis × U_3D
    # Face  |  U formula   |   V formula
    # F0,F1 |  z - z0      |  y0 - y   (V direction = -Y)
    # F2,F3 |  z - z0      |  x - x0   (V direction = +X)
    # F4,F5 |  x - x0      |  y - y0   (V direction = +Y)
    def uv(fi: int, vx: float, vy: float, vz: float) -> tuple[float, float]:
        if fi in (0, 1):
            return (vz - z0, y0 - vy)
        if fi in (2, 3):
            return (vz - z0, vx - x0)
        # fi in (4, 5)
        return (vx - x0, vy - y0)

    def make_pcurve(plane_id: int, su: float, sv: float, eu: float, ev: float) -> int:
        du, dv = eu - su, ev - sv
        length = (du * du + dv * dv) ** 0.5
        if length < 1e-12:
            raise ValueError(f"Zero-length edge in UV space: ({su},{sv})->({eu},{ev})")
        du /= length
        dv /= length
        o2d = w.alloc()
        d2d = w.alloc()
        v2d = w.alloc()
        l2d = w.alloc()
        dr = w.alloc()
        pc = w.alloc()
        w.add(o2d, f"CARTESIAN_POINT('',{fmt2(su, sv)})")
        w.add(d2d, f"DIRECTION('',{fmt2(du, dv)})")
        w.add(v2d, f"VECTOR('',#{d2d},1.)")
        w.add(l2d, f"LINE('',#{o2d},#{v2d})")
        w.add(dr, f"DEFINITIONAL_REPRESENTATION('',(#{l2d}),#{g2d})")
        w.add(pc, f"PCURVE('',#{plane_id},#{dr})")
        return pc

    # ── 12 edge curves ────────────────────────────────────────────────────
    # (start_vi, end_vi, 3D-line-dir, adjacent_face_1, adjacent_face_2)
    # Natural direction and face adjacency follow OCCT's face loop ordering.
    edge_defs = [
        (0, 4, (0, 0, 1), 0, 2),   # E0:  v0→v4  F0,F2
        (0, 3, (0, 1, 0), 0, 4),   # E1:  v0→v3  F0,F4
        (3, 7, (0, 0, 1), 0, 3),   # E2:  v3→v7  F0,F3
        (4, 7, (0, 1, 0), 0, 5),   # E3:  v4→v7  F0,F5
        (1, 5, (0, 0, 1), 1, 2),   # E4:  v1→v5  F1,F2
        (1, 2, (0, 1, 0), 1, 4),   # E5:  v1→v2  F1,F4
        (2, 6, (0, 0, 1), 1, 3),   # E6:  v2→v6  F1,F3
        (5, 6, (0, 1, 0), 1, 5),   # E7:  v5→v6  F1,F5
        (0, 1, (1, 0, 0), 2, 4),   # E8:  v0→v1  F2,F4
        (4, 5, (1, 0, 0), 2, 5),   # E9:  v4→v5  F2,F5
        (3, 2, (1, 0, 0), 3, 4),   # E10: v3→v2  F3,F4  (+X direction)
        (7, 6, (1, 0, 0), 3, 5),   # E11: v7→v6  F3,F5  (+X direction)
    ]
    ec_ids = []
    for sv_i, ev_i, (dx, dy, dz), fi1, fi2 in edge_defs:
        sx, sy, sz = vcoords[sv_i]
        ex, ey, ez = vcoords[ev_i]
        # 3D line
        o3d = w.alloc()
        d3d = w.alloc()
        v3d = w.alloc()
        l3d = w.alloc()
        w.add(o3d, f"CARTESIAN_POINT('',{fmt3(sx, sy, sz)})")
        w.add(d3d, f"DIRECTION('',{fmt3(dx, dy, dz)})")
        w.add(v3d, f"VECTOR('',#{d3d},1.)")
        w.add(l3d, f"LINE('',#{o3d},#{v3d})")
        # PCURVEs
        pc1 = make_pcurve(plane_ids[fi1], *uv(fi1, sx, sy, sz), *uv(fi1, ex, ey, ez))
        pc2 = make_pcurve(plane_ids[fi2], *uv(fi2, sx, sy, sz), *uv(fi2, ex, ey, ez))
        # SURFACE_CURVE
        sc = w.alloc()
        w.add(sc, f"SURFACE_CURVE('',#{l3d},(#{pc1},#{pc2}),.PCURVE_S1.)")
        # EDGE_CURVE
        ec = w.alloc()
        w.add(
            ec,
            f"EDGE_CURVE('',#{vvp[sv_i]},#{vvp[ev_i]},#{sc},.T.)",
        )
        ec_ids.append(ec)

    # ── 6 faces ───────────────────────────────────────────────────────────
    # Loop: list of (edge_idx, forward: bool)
    # same_sense and FACE_BOUND sense mirror each other (both .T./.F. together).
    face_loop_defs = [
        [(0, False), (1, True), (2, True), (3, False)],   # F0 LEFT
        [(4, False), (5, True), (6, True), (7, False)],   # F1 RIGHT
        [(8, False), (0, True), (9, True), (4, False)],   # F2 FRONT
        [(10, False), (2, True), (11, True), (6, False)], # F3 BACK
        [(1, False), (8, True), (5, True), (10, False)],  # F4 BOTTOM
        [(3, False), (9, True), (7, True), (11, False)],  # F5 TOP
    ]
    face_senses = [".F.", ".T.", ".F.", ".T.", ".F.", ".T."]

    face_ids = []
    for fi in range(6):
        oe_ids = []
        for ei, fwd in face_loop_defs[fi]:
            oe = w.alloc()
            sense = ".T." if fwd else ".F."
            w.add(oe, f"ORIENTED_EDGE('',*,*,#{ec_ids[ei]},{sense})")
            oe_ids.append(oe)
        el = w.alloc()
        w.add(el, f"EDGE_LOOP('',({','.join('#' + str(i) for i in oe_ids)}))")
        fb = w.alloc()
        w.add(fb, f"FACE_BOUND('',#{el},{face_senses[fi]})")
        af = w.alloc()
        w.add(af, f"ADVANCED_FACE('',(#{fb}),#{plane_ids[fi]},{face_senses[fi]})")
        face_ids.append(af)

    # ── CLOSED_SHELL + MANIFOLD_SOLID_BREP ───────────────────────────────
    cs = w.alloc()
    w.add(cs, f"CLOSED_SHELL('',({','.join('#' + str(i) for i in face_ids)}))")
    msb = w.alloc()
    w.add(msb, f"MANIFOLD_SOLID_BREP('',#{cs})")

    # ── ADVANCED_BREP_SHAPE_REPRESENTATION ───────────────────────────────
    g_o = w.alloc()
    g_z = w.alloc()
    g_x = w.alloc()
    g_ap = w.alloc()
    w.add(g_o, "CARTESIAN_POINT('',(0.,0.,0.))")
    w.add(g_z, "DIRECTION('',(0.,0.,1.))")
    w.add(g_x, "DIRECTION('',(1.,0.,-0.))")
    w.add(g_ap, f"AXIS2_PLACEMENT_3D('',#{g_o},#{g_z},#{g_x})")
    brep = w.alloc()
    w.add(
        brep,
        f"ADVANCED_BREP_SHAPE_REPRESENTATION('',(#{g_ap},#{msb}),#{geom3d_context_id})",
    )

    # ── XDE product structure ─────────────────────────────────────────────
    pc_e = w.alloc()  # PRODUCT_CONTEXT
    pdc = w.alloc()   # PRODUCT_DEFINITION_CONTEXT
    p_e = w.alloc()   # PRODUCT
    pdf = w.alloc()   # PRODUCT_DEFINITION_FORMATION
    pd = w.alloc()    # PRODUCT_DEFINITION
    pds = w.alloc()   # PRODUCT_DEFINITION_SHAPE
    sdr = w.alloc()   # SHAPE_DEFINITION_REPRESENTATION
    w.add(pc_e, f"PRODUCT_CONTEXT('',#{app_context_id},'mechanical')")
    w.add(
        pdc,
        f"PRODUCT_DEFINITION_CONTEXT('part definition',#{app_context_id},'design')",
    )
    name = product_name
    w.add(p_e, f"PRODUCT('{name}','{name}','',(#{pc_e}))")
    w.add(pdf, f"PRODUCT_DEFINITION_FORMATION('','',#{p_e})")
    w.add(pd, f"PRODUCT_DEFINITION('design','',#{pdf},#{pdc})")
    w.add(pds, f"PRODUCT_DEFINITION_SHAPE('','',#{pd})")
    w.add(sdr, f"SHAPE_DEFINITION_REPRESENTATION(#{pds},#{brep})")
    return sdr


# ─── Shared geometry context ───────────────────────────────────────────────────

def _add_geom3d_context(w: StepWriter) -> int:
    """Write the shared 3D geometry context (mm, radians, steradians).

    Returns the GEOMETRIC_REPRESENTATION_CONTEXT compound entity id.
    """
    lu = w.alloc()  # LENGTH_UNIT
    pau = w.alloc()  # PLANE_ANGLE_UNIT
    sau = w.alloc()  # SOLID_ANGLE_UNIT
    unc = w.alloc()  # UNCERTAINTY_MEASURE_WITH_UNIT
    ctx = w.alloc()  # GEOMETRIC_REPRESENTATION_CONTEXT (compound)
    w.add(lu, "( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
    w.add(pau, "( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
    w.add(sau, "( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
    w.add(
        unc,
        f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#{lu},"
        "\n  'distance_accuracy_value','confusion accuracy')",
    )
    w.add(
        ctx,
        f"( GEOMETRIC_REPRESENTATION_CONTEXT(3)\n"
        f"GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{unc}))"
        f" GLOBAL_UNIT_ASSIGNED_CONTEXT\n"
        f"((#{lu},#{pau},#{sau}))"
        f" REPRESENTATION_CONTEXT('Context #1',\n"
        f"  '3D Context with UNIT and UNCERTAINTY') )",
    )
    return ctx


# ─── Entry point ──────────────────────────────────────────────────────────────

def main() -> int:
    out = Path(__file__).parent / "shape.step"
    if len(sys.argv) > 1:
        out = Path(sys.argv[1])

    w = StepWriter()

    # Shared application protocol entities (written first, as in OCCT output).
    apd = w.alloc()  # APPLICATION_PROTOCOL_DEFINITION
    ac = w.alloc()   # APPLICATION_CONTEXT
    w.add(
        apd,
        f"APPLICATION_PROTOCOL_DEFINITION('international standard',\n"
        f"  'automotive_design',2000,#{ac})",
    )
    w.add(
        ac,
        "APPLICATION_CONTEXT(\n"
        "  'core data for automotive mechanical design processes')",
    )

    # Shared 3D geometry context (one per file, referenced by all boxes).
    geom3d = _add_geom3d_context(w)

    # Three boxes: component centres along X, 10×10×10 mm, 2 mm gaps.
    boxes = [(-12.0, 0.0, 0.0), (0.0, 0.0, 0.0), (12.0, 0.0, 0.0)]
    for cx, cy, cz in boxes:
        _add_box(w, cx, cy, cz, 10.0, geom3d, ac, "Component")

    w.write_to(str(out))
    print(f"Written: {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
