# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

# G4Paraboloid(dz=20, r1=6, r2=18): surface r^2 = k*(z + c), k=7.2, c=25, vertex at z=-25
#
# OCCT Geom_Parabola parameterisation: C(t) = (t, 0, -c + t^2/(4*F))
#   where F is the focal distance. Comparing with r^2 = k*(z+c): 4F = k => F = 7.2/4 = 1.8
#   Profile parameters: t1=r1=6 at z=-20, t2=r2=18 at z=+20
#
# Placement: vertex at (0,0,-25), normal=(0,1,0) puts curve in XZ plane,
#   Xdir=(0,0,1) so Ydir = N x Xdir = (0,1,0)x(0,0,1) = (1,0,0) (radial direction).

parabola para 0 0 -25   0 1 0   0 0 1   1.8
mkedge eparc para 6 18

# Bottom disk at z=-20, radius r1=6 (line from axis to rim, revolved)
line lbot 0 0 -20  1 0 0
mkedge ebot lbot 0 6

# Top disk at z=+20, radius r2=18 (line from rim to axis, revolved)
line ltop 18 0 20  -1 0 0
mkedge etop ltop 0 18

# Revolve each edge 360 degrees around the Z axis
revol flateral eparc 0 0 0  0 0 1  360
revol fbot     ebot  0 0 0  0 0 1  360
revol ftop     etop  0 0 0  0 0 1  360

# Sew the three faces into a closed shell, then promote to solid
sewing shellresult 1e-3 flateral fbot ftop
mkvolume brepsolid shellresult

# Convert all surfaces to rational B-Spline (required for STEP readback)
nurbsconvert seed_shape brepsolid
stepwrite a seed_shape [file join $script_dir shape.step]
