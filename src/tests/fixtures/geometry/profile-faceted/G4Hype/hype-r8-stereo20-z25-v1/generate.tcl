# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

# G4Hype(innerRadius=0, outerRadius=8, innerStereo=0, outerStereo=20deg, halfLenZ=25)
# Outer surface: r^2 = R0^2 + z^2*tan^2(theta) = 64 + z^2*tan^2(20 deg)
#
# OCCT Geom_Hyperbola parameterisation (right branch):
#   C(t) = (a*cosh(t), 0, b*sinh(t))
#   MajorRadius a = R0 = 8  (radial at waist z=0)
#   MinorRadius b = R0/tan(outerStereo) = 8/tan(20 deg)
#   Verify: r^2/a^2 - z^2/b^2 = cosh^2(t) - sinh^2(t) = 1  =>  r^2 = a^2 + z^2*(a/b)^2 = a^2 + z^2*tan^2(theta)
#
# Parameter range: b*sinh(t_max) = dz=25  =>  t_max = asinh(25/b)
# Placement: N=(0,-1,0) => Ydir = N x Xdir = (0,-1,0)x(1,0,0) = (0,0,1) = +Z (axial direction).

set b_val [expr {8.0 / tan(20.0 * 3.14159265358979323846 / 180.0)}]
set u_max [expr {log(25.0 / $b_val + sqrt(1.0 + (25.0 / $b_val) * (25.0 / $b_val)))}]
set r_max [expr {8.0 * cosh($u_max)}]

hyperbola hyp 0 0 0  0 -1 0  1 0 0  8 $b_val
mkedge earc hyp [expr {-$u_max}] $u_max

# Bottom disk at z=-dz (line from axis to rim, revolved)
line lbot 0 0 -25  1 0 0
mkedge ebot lbot 0 $r_max

# Top disk at z=+dz (line from rim to axis, revolved)
line ltop $r_max 0 25  -1 0 0
mkedge etop ltop 0 $r_max

# Revolve each edge 360 degrees around the Z axis
revol flateral earc  0 0 0  0 0 1  360
revol fbot     ebot  0 0 0  0 0 1  360
revol ftop     etop  0 0 0  0 0 1  360

# Sew the three faces into a closed shell, then promote to solid
sewing shellresult 1e-3 flateral fbot ftop
mkvolume brepsolid shellresult

# Convert all surfaces to rational B-Spline (required for STEP readback)
nurbsconvert seed_shape brepsolid
stepwrite a seed_shape [file join $script_dir shape.step]
