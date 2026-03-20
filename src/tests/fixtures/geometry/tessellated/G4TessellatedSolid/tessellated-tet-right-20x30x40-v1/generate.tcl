# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline face_a_wire 0 0 0 20 0 0 0 30 0 0 0 0
mkplane face_a face_a_wire
polyline face_b_wire 0 0 0 20 0 0 0 0 40 0 0 0
mkplane face_b face_b_wire
polyline face_c_wire 20 0 0 0 30 0 0 0 40 20 0 0
mkplane face_c face_c_wire
polyline face_d_wire 0 30 0 0 0 40 0 0 0 0 30 0
mkplane face_d face_d_wire
sewing stitched_shell 1.0e-06 face_a face_b face_c face_d
mkvolume tessellated_shape stitched_shell
stepwrite a tessellated_shape [file join $script_dir shape.step]
