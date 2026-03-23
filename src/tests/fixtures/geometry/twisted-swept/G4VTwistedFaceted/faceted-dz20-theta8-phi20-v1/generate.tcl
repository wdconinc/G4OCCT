# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# Generates a tessellated STEP solid for G4VTwistedFaceted with parameters:
#   dz=20, phi_twist=30°, theta=8°, phi_tilt=20°, dy1=8, dx1=9, dx2=12,
#   dy2=10, dx3=7, dx4=13, alpha=12°
#
# G4VTwistedFaceted vertex layout (per Geant4 source G4VTwistedFaceted.cc):
#   At section t (z = -dz + 2*dz*t), interpolated half-widths:
#     dy(t)  = dy1 + t*(dy2-dy1)
#     dm(t)  = dx1 + t*(dx3-dx1)   (half-width at -dy)
#     dp(t)  = dx2 + t*(dx4-dx2)   (half-width at +dy)
#   Shear along x: sh = dy(t)*tan(alpha)
#   Tilt offset (centered on z=0): ox = (t-0.5)*shift_x, oy = (t-0.5)*shift_y
#   Twist angle at z: th = phi_twist * z / (2*dz) [degrees]
#   Local (unrotated) corners (CCW from -y):
#     j=0: (-dm - sh, -dy)
#     j=1: ( dm - sh, -dy)
#     j=2: ( dp + sh, +dy)
#     j=3: (-dp + sh, +dy)
#   Global (x,y,z) after rotation by th and tilt offset:
#     x = lx*cos(th) - ly*sin(th) + ox
#     y = lx*sin(th) + ly*cos(th) + oy
#     z = -dz + t*2*dz
#
# Tessellation: nSec sections → (nSec-1) strips, each with 4 lateral quads
# split into 2 triangles, plus 1 triangulated endcap per end (fan from j=0).
# With nSec=64: 63*4*2 = 504 lateral + 2 bottom + 2 top = 508 triangular faces.
#
# DRAWEXE note: variable `pi` is protected; use MY_PI instead.
# DRAWEXE note: DRAW shape commands inside proc bodies are scoped locally and
# not accessible globally — all vertex/edge/wire/mkplane calls are at top level.

pload ALL

set script_dir [file normalize [file dirname [info script]]]

# ── Parameters ────────────────────────────────────────────────────────────────
set MY_PI [expr {acos(-1.0)}]
set d2r   [expr {$MY_PI / 180.0}]

set nSec     64
set dz       20.0
set phi_tw   30.0
set theta_tl  8.0
set azi_tl   20.0
set dy1       8.0
set dx1       9.0
set dx2      12.0
set dy2      10.0
set dx3       7.0
set dx4      13.0
set alpha_deg 12.0

set talpha  [expr {tan($alpha_deg * $d2r)}]
set shift_r [expr {2.0 * $dz * tan($theta_tl * $d2r)}]
set shift_x [expr {$shift_r * cos($azi_tl * $d2r)}]
set shift_y [expr {$shift_r * sin($azi_tl * $d2r)}]

# ── Pure-math helper (no DRAW commands inside) ────────────────────────────────
# Returns flat list {x0 y0 z0  x1 y1 z1  x2 y2 z2  x3 y3 z3} for section i
proc sect_pts {i nSec dz phi_tw dy1 dx1 dx2 dy2 dx3 dx4 talpha shift_x shift_y d2r} {
    set t  [expr {double($i) / ($nSec - 1)}]
    set z  [expr {-$dz + $t * 2.0 * $dz}]
    set th [expr {$phi_tw * $z / (2.0 * $dz)}]
    set dy [expr {$dy1 + $t * ($dy2 - $dy1)}]
    set dm [expr {$dx1 + $t * ($dx3 - $dx1)}]
    set dp [expr {$dx2 + $t * ($dx4 - $dx2)}]
    set sh [expr {$dy * $talpha}]
    set ox [expr {($t - 0.5) * $shift_x}]
    set oy [expr {($t - 0.5) * $shift_y}]
    set ca [expr {cos($th * $d2r)}]
    set sa [expr {sin($th * $d2r)}]
    # local corners (j=0..3)
    set lxs [list [expr {-$dm - $sh}] [expr {$dm - $sh}] [expr {$dp + $sh}] [expr {-$dp + $sh}]]
    set lys [list [expr {-$dy}]        [expr {-$dy}]       [expr {$dy}]        [expr {$dy}]]
    set result {}
    foreach lx $lxs ly $lys {
        lappend result [expr {$lx*$ca - $ly*$sa + $ox}]
        lappend result [expr {$lx*$sa + $ly*$ca + $oy}]
        lappend result $z
    }
    return $result
}

# ── Pre-compute all section coordinates ──────────────────────────────────────
for {set i 0} {$i < $nSec} {incr i} {
    set coords_$i [sect_pts $i $nSec $dz $phi_tw \
        $dy1 $dx1 $dx2 $dy2 $dx3 $dx4 $talpha $shift_x $shift_y $d2r]
}

# ── Create all vertices (v_i_j) at global scope ───────────────────────────────
set nV 4
for {set i 0} {$i < $nSec} {incr i} {
    set coords [set coords_$i]
    for {set j 0} {$j < $nV} {incr j} {
        set base [expr {$j * 3}]
        vertex v_${i}_${j} \
            [lindex $coords $base] \
            [lindex $coords [expr {$base+1}]] \
            [lindex $coords [expr {$base+2}]]
    }
}

# ── Build triangular faces ────────────────────────────────────────────────────
set faces {}
set fid 0

# Lateral faces: for each strip (i→i+1) and each side (j→j+1 mod 4),
# split the quad into 2 triangles.
for {set i 0} {$i < $nSec - 1} {incr i} {
    set ip [expr {$i + 1}]
    for {set j 0} {$j < $nV} {incr j} {
        set jp [expr {($j + 1) % $nV}]
        # Triangle A: (i,j)→(i,jp)→(ip,jp)
        incr fid
        edge ea$fid v_${i}_${j}  v_${i}_${jp}
        edge eb$fid v_${i}_${jp} v_${ip}_${jp}
        edge ec$fid v_${ip}_${jp} v_${i}_${j}
        wire w$fid ea$fid eb$fid ec$fid
        mkplane f$fid w$fid
        lappend faces f$fid
        # Triangle B: (i,j)→(ip,jp)→(ip,j)
        incr fid
        edge ea$fid v_${i}_${j}   v_${ip}_${jp}
        edge eb$fid v_${ip}_${jp} v_${ip}_${j}
        edge ec$fid v_${ip}_${j}  v_${i}_${j}
        wire w$fid ea$fid eb$fid ec$fid
        mkplane f$fid w$fid
        lappend faces f$fid
    }
}

# Bottom endcap (i=0): fan from j=0, triangles (0)→(j)→(j+1) for j=1..nV-2
for {set j 1} {$j < $nV - 1} {incr j} {
    incr fid
    edge ea$fid v_0_0          v_0_$j
    edge eb$fid v_0_$j         v_0_[expr {$j+1}]
    edge ec$fid v_0_[expr {$j+1}] v_0_0
    wire w$fid ea$fid eb$fid ec$fid
    mkplane f$fid w$fid
    lappend faces f$fid
}

# Top endcap (i=nSec-1): fan from j=0, reversed orientation
set L [expr {$nSec - 1}]
for {set j 1} {$j < $nV - 1} {incr j} {
    incr fid
    edge ea$fid v_${L}_0          v_${L}_[expr {$j+1}]
    edge eb$fid v_${L}_[expr {$j+1}] v_${L}_$j
    edge ec$fid v_${L}_$j          v_${L}_0
    wire w$fid ea$fid eb$fid ec$fid
    mkplane f$fid w$fid
    lappend faces f$fid
}

puts "Total faces: $fid"

# ── Sew into a closed shell, then form solid ──────────────────────────────────
eval [concat sewing myshell 1e-4 $faces]
mkvolume mysolid myshell

# ── Verify volume ─────────────────────────────────────────────────────────────
vprops mysolid

# ── Write STEP ───────────────────────────────────────────────────────────────
stepwrite a mysolid [file join $script_dir shape.step]
