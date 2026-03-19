#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors
"""Generate a STEP file for the hexagonal frustum (G4Polyhedra fixture).

Hexagonal frustum: bottom at z=-12.5 (circumradius 10), top at z=+12.5 (circumradius 6).
All faces are PLANE surfaces for robust OCCT BRepClass3d classification.
"""
import math, sys
from pathlib import Path

def fmt(v):
    """Format a float for STEP (avoid trailing zeros, STEP decimal convention)."""
    s = f"{v:.15g}"
    if 'e' in s: s = s.replace('e', 'E')
    if '.' not in s and 'E' not in s: s += '.'
    if '.' in s and 'E' not in s:
        s = s.rstrip('0')
        if s.endswith('.'): s += '0'
    return s

def v3(*args):
    if len(args)==1: a=args[0]
    else: a=args
    return tuple(float(x) for x in a)

def sub3(a,b): return (a[0]-b[0], a[1]-b[1], a[2]-b[2])
def add3(a,b): return (a[0]+b[0], a[1]+b[1], a[2]+b[2])
def scale3(a,s): return (a[0]*s, a[1]*s, a[2]*s)
def dot3(a,b): return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]
def cross3(a,b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def norm3(a): m=math.sqrt(dot3(a,a)); return (a[0]/m,a[1]/m,a[2]/m)
def norm2(a): m=math.sqrt(a[0]**2+a[1]**2); return (a[0]/m,a[1]/m)
def sub2(a,b): return (a[0]-b[0],a[1]-b[1])

class STEP:
    def __init__(self):
        self._data = {}  # id -> definition string
        self._n = 1
    def add(self, s):
        i = self._n; self._n += 1
        self._data[i] = s; return i
    def ref(self, i): return f"#{i}"
    def write(self, path):
        lines = [
            "ISO-10303-21;", "HEADER;",
            "FILE_DESCRIPTION(('Open CASCADE Model'),'2;1');",
            "FILE_NAME('Open CASCADE Shape Model','2024-01-01T00:00:00',('Author'),(",
            "    'Open CASCADE'),'Open CASCADE STEP processor 7.9','Open CASCADE 7.9'",
            "  ,'Unknown');",
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }'));",
            "ENDSEC;", "DATA;"
        ]
        for i in sorted(self._data): lines.append(f"#{i} = {self._data[i]};")
        lines += ["ENDSEC;", "END-ISO-10303-21;"]
        Path(path).write_text('\n'.join(lines)+'\n', encoding='utf-8')

S = STEP()

# ─── Compute vertices ──────────────────────────────────────────────────────────
def hex_ring(r, z):
    return [(r*math.cos(k*math.pi/3), r*math.sin(k*math.pi/3), z) for k in range(6)]

BV = hex_ring(10.0, -12.5)  # bottom V0..V5
TV = hex_ring(6.0,  12.5)   # top    V6..V11
VERTS = BV + TV

# ─── Standard STEP header entities (boilerplate) ──────────────────────────────
e_apd = S.add("APPLICATION_PROTOCOL_DEFINITION('international standard',\n  'automotive_design',2000,#2)")
e_ac  = S.add("APPLICATION_CONTEXT(\n  'core data for automotive mechanical design processes')")

# Reserve IDs for product entities (we'll fill them later)
# The SHAPE_DEFINITION_REPRESENTATION must reference PDS (added after ABREP),
# so we just ensure the references are correct by tracking IDs.

# ─── Geometry context ──────────────────────────────────────────────────────────
e_len = S.add("( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) )")
e_ang = S.add("( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) )")
e_sol = S.add("( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() )")
e_unc = S.add(f"UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#{e_len},\n  'distance_accuracy_value','confusion accuracy')")
e_ctx = S.add(f"( GEOMETRIC_REPRESENTATION_CONTEXT(3)\nGLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#{e_unc})) GLOBAL_UNIT_ASSIGNED_CONTEXT\n((#{e_len},#{e_ang},#{e_sol})) REPRESENTATION_CONTEXT('Context #1',\n  '3D Context with UNIT and UNCERTAINTY') )")

# 2D context (shared by all PCURVEs)
e_ctx2 = S.add("( GEOMETRIC_REPRESENTATION_CONTEXT(2)\nPARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('2D SPACE',''\n  ) )")

# ─── Helper: create a PLANE surface ───────────────────────────────────────────
def make_plane(origin, n, u):
    """Return (plane_eid, n_normed, u_normed, v_normed)."""
    n = norm3(n); u = norm3(u)
    # orthogonalize u w.r.t. n
    u = norm3(sub3(u, scale3(n, dot3(u,n))))
    v = cross3(n, u)
    e_o = S.add(f"CARTESIAN_POINT('',({fmt(origin[0])},{fmt(origin[1])},{fmt(origin[2])}))")
    e_n = S.add(f"DIRECTION('',({fmt(n[0])},{fmt(n[1])},{fmt(n[2])}))")
    e_u = S.add(f"DIRECTION('',({fmt(u[0])},{fmt(u[1])},{fmt(u[2])}))")
    e_a = S.add(f"AXIS2_PLACEMENT_3D('',#{e_o},#{e_n},#{e_u})")
    e_p = S.add(f"PLANE('',#{e_a})")
    return e_p, n, u, v

def uv(pt, origin, u, v):
    d = sub3(pt, origin)
    return (dot3(d,u), dot3(d,v))

# ─── Helper: create 2D line PCURVE on a plane ─────────────────────────────────
def make_pcurve(plane_eid, p_start_uv, p_end_uv):
    du, dv = p_end_uv[0]-p_start_uv[0], p_end_uv[1]-p_start_uv[1]
    ln = math.sqrt(du*du+dv*dv)
    e_sp = S.add(f"CARTESIAN_POINT('',({fmt(p_start_uv[0])},{fmt(p_start_uv[1])}))")
    e_dd = S.add(f"DIRECTION('',({fmt(du/ln)},{fmt(dv/ln)}))")
    e_vv = S.add(f"VECTOR('',#{e_dd},1.)")
    e_ln = S.add(f"LINE('',#{e_sp},#{e_vv})")
    e_dr = S.add(f"DEFINITIONAL_REPRESENTATION('',( #{e_ln}),#{e_ctx2})")
    return S.add(f"PCURVE('',#{plane_eid},#{e_dr})")

# ─── Helper: create 3D LINE ────────────────────────────────────────────────────
def make_line3(p1, p2):
    d = norm3(sub3(p2,p1))
    e_p = S.add(f"CARTESIAN_POINT('',({fmt(p1[0])},{fmt(p1[1])},{fmt(p1[2])}))")
    e_d = S.add(f"DIRECTION('',({fmt(d[0])},{fmt(d[1])},{fmt(d[2])}))")
    e_v = S.add(f"VECTOR('',#{e_d},1.)")
    return S.add(f"LINE('',#{e_p},#{e_v})")

# ─── Build face planes ─────────────────────────────────────────────────────────
bot_o = (0.,0.,-12.5); bot_n=(0.,0.,-1.); bot_u=norm3(sub3(BV[1],BV[0]))
top_o = (0.,0., 12.5); top_n=(0.,0., 1.); top_u=norm3(sub3(TV[1],TV[0]))

bot_plane, bot_n, bot_u, bot_v = make_plane(bot_o, bot_n, bot_u)
top_plane, top_n, top_u, top_v = make_plane(top_o, top_n, top_u)

lat_planes = []
lat_ns = []; lat_us = []; lat_vs = []; lat_os = []
for i in range(6):
    i1 = (i+1)%6
    # Lateral face i: V[i], V[i+1], V[i+7%6+6], V[i+6]
    # Correct: connects BV[i]->BV[i1] bottom edge to TV[i1]->TV[i] top (reversed)
    quad = [BV[i], BV[i1], TV[i1], TV[i]]
    # Newell normal
    nx=ny=nz=0.
    for j in range(4):
        a,b=quad[j],quad[(j+1)%4]
        nx += (a[1]-b[1])*(a[2]+b[2])
        ny += (a[2]-b[2])*(a[0]+b[0])
        nz += (a[0]-b[0])*(a[1]+b[1])
    n = norm3((nx,ny,nz))
    # Verify outward (should point away from z-axis)
    cx = sum(q[0] for q in quad)/4; cy = sum(q[1] for q in quad)/4
    if n[0]*cx + n[1]*cy < 0: n = (-n[0],-n[1],-n[2])
    u_init = norm3(sub3(BV[i1], BV[i]))
    origin = tuple(sum(q[k] for q in quad)/4 for k in range(3))
    ep, n_, u_, v_ = make_plane(origin, n, u_init)
    lat_planes.append(ep); lat_ns.append(n_); lat_us.append(u_); lat_vs.append(v_); lat_os.append(origin)

# ─── Build vertices ────────────────────────────────────────────────────────────
vert_eids = []
for v in VERTS:
    e_c = S.add(f"CARTESIAN_POINT('',({fmt(v[0])},{fmt(v[1])},{fmt(v[2])}))")
    vert_eids.append(S.add(f"VERTEX_POINT('',#{e_c})"))

# ─── Build edges ───────────────────────────────────────────────────────────────
def make_edge(v_start, v_end, face_a_plane, face_a_o, face_a_u, face_a_v,
                              face_b_plane, face_b_o, face_b_u, face_b_v):
    """Create EDGE_CURVE with two PCURVEs. Returns edge_curve entity ID."""
    p1, p2 = VERTS[v_start], VERTS[v_end]
    e_l3d = make_line3(p1, p2)
    uv_a_s = uv(p1, face_a_o, face_a_u, face_a_v)
    uv_a_e = uv(p2, face_a_o, face_a_u, face_a_v)
    uv_b_s = uv(p1, face_b_o, face_b_u, face_b_v)
    uv_b_e = uv(p2, face_b_o, face_b_u, face_b_v)
    pc_a = make_pcurve(face_a_plane, uv_a_s, uv_a_e)
    pc_b = make_pcurve(face_b_plane, uv_b_s, uv_b_e)
    e_sc = S.add(f"SURFACE_CURVE('',#{e_l3d},(#{pc_a},#{pc_b}),.PCURVE_S1.)")
    return S.add(f"EDGE_CURVE('',#{vert_eids[v_start]},#{vert_eids[v_end]},#{e_sc},.T.)")

# Bottom edges: E[i] = V[i] -> V[(i+1)%6]
# Adjacent faces: bottom face and lateral face i
bot_edges = []
for i in range(6):
    i1=(i+1)%6
    ec = make_edge(i, i1, bot_plane, bot_o, bot_u, bot_v,
                         lat_planes[i], lat_os[i], lat_us[i], lat_vs[i])
    bot_edges.append(ec)

# Top edges: E[i] = V[i+6] -> V[(i+1)%6+6]
# Adjacent faces: top face and lateral face i
top_edges = []
for i in range(6):
    i1=(i+1)%6
    ec = make_edge(6+i, 6+i1, top_plane, top_o, top_u, top_v,
                           lat_planes[i], lat_os[i], lat_us[i], lat_vs[i])
    top_edges.append(ec)

# Lateral edges: E[i] = V[i] -> V[i+6]
# Adjacent faces: lateral face i and lateral face (i-1)%6
lat_edges = []
for i in range(6):
    im1=(i-1)%6
    ec = make_edge(i, 6+i, lat_planes[i],   lat_os[i],   lat_us[i],   lat_vs[i],
                           lat_planes[im1], lat_os[im1], lat_us[im1], lat_vs[im1])
    lat_edges.append(ec)

# ─── Build faces ───────────────────────────────────────────────────────────────
def oe(ec, fwd): return S.add(f"ORIENTED_EDGE('',*,*,#{ec},.{'T' if fwd else 'F'}.)")

def make_adv_face(plane_eid, oriented_edges, same_sense):
    el = ','.join(f'#{e}' for e in oriented_edges)
    e_el = S.add(f"EDGE_LOOP('',({el}))")
    e_fb = S.add(f"FACE_BOUND('',#{e_el},.T.)")
    flag = '.T.' if same_sense else '.F.'
    return S.add(f"ADVANCED_FACE('',(#{e_fb}),#{plane_eid},{flag})")

# Bottom face: plane normal = (0,0,-1) = outward. same_sense=.T.
# Boundary traversal with n=(0,0,-1): CCW when viewed from -z.
# V0->V5->V4->V3->V2->V1->V0 (reversed order = bot_edges in reverse)
bot_oes = [
    oe(bot_edges[5], False),  # E50 rev = V0->V5
    oe(bot_edges[4], False),  # E45 rev = V5->V4
    oe(bot_edges[3], False),  # E34 rev = V4->V3
    oe(bot_edges[2], False),  # E23 rev = V3->V2
    oe(bot_edges[1], False),  # E12 rev = V2->V1
    oe(bot_edges[0], False),  # E01 rev = V1->V0
]
bot_face = make_adv_face(bot_plane, bot_oes, True)

# Top face: plane normal = (0,0,+1) = outward. same_sense=.T.
# CCW from +z: V6->V7->V8->V9->V10->V11->V6
top_oes = [oe(top_edges[i], True) for i in range(6)]
top_face = make_adv_face(top_plane, top_oes, True)

# Lateral faces: for face i, same_sense=.T. (plane normal already set outward)
# Boundary: V[i]->V[i+1] (bot edge fwd), V[i+1]->V[i+7](=i1+6) (lat_edges[i1] fwd),
#           V[i+7]->V[i+6] (top_edges[i] rev), V[i+6]->V[i] (lat_edges[i] rev)
lat_faces = []
for i in range(6):
    i1=(i+1)%6
    lat_oes = [
        oe(bot_edges[i],  True),   # V[i]->V[i+1]
        oe(lat_edges[i1], True),   # V[i+1]->V[i+7]
        oe(top_edges[i],  False),  # V[i+7]->V[i+6]  (top_edges[i] is V6+i -> V6+i1, reversed = V6+i1->V6+i)
        oe(lat_edges[i],  False),  # V[i+6]->V[i]
    ]
    lat_faces.append(make_adv_face(lat_planes[i], lat_oes, True))

# ─── Closed shell and solid ────────────────────────────────────────────────────
all_faces = [bot_face, top_face] + lat_faces
e_shell = S.add(f"CLOSED_SHELL('',({','.join(f'#{f}' for f in all_faces)}))")
e_solid = S.add(f"MANIFOLD_SOLID_BREP('',#{e_shell})")

# ─── Global frame ──────────────────────────────────────────────────────────────
e_go = S.add("CARTESIAN_POINT('',(0.,0.,0.))")
e_gz = S.add("DIRECTION('',(0.,0.,1.))")
e_gx = S.add("DIRECTION('',(1.,0.,-0.))")
e_ga = S.add(f"AXIS2_PLACEMENT_3D('',#{e_go},#{e_gz},#{e_gx})")

# ─── Product boilerplate ───────────────────────────────────────────────────────
e_pctx = S.add("PRODUCT_CONTEXT('',#2,'mechanical')")
e_pdctx = S.add("PRODUCT_DEFINITION_CONTEXT('part definition',#2,'design')")
e_prod  = S.add(f"PRODUCT('Open CASCADE STEP translator 7.9 1',\n  'Open CASCADE STEP translator 7.9 1','',(#{e_pctx}))")
e_pdf   = S.add(f"PRODUCT_DEFINITION_FORMATION('','',#{e_prod})")
e_pd    = S.add(f"PRODUCT_DEFINITION('design','',#{e_pdf},#{e_pdctx})")
e_pds   = S.add(f"PRODUCT_DEFINITION_SHAPE('','',#{e_pd})")
e_abrep = S.add(f"ADVANCED_BREP_SHAPE_REPRESENTATION('',( #{e_ga},#{e_solid}),#{e_ctx})")
e_sdr   = S.add(f"SHAPE_DEFINITION_REPRESENTATION(#{e_pds},#{e_abrep})")
S.add(f"PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#{e_prod}))")

out = sys.argv[1] if len(sys.argv)>1 else str(Path(__file__).parent / 'shape.step')
S.write(out)
print(f"Written {S._n-1} entities to {out}")
