# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline bottom_face \
  -7 -9 -18 \
  7 -9 -18 \
  13 9 -18 \
  -13 9 -18 \
  -7 -9 -18
mkplane trap_bottom bottom_face
polyline top_face \
  -7 -9 18 \
  7 -9 18 \
  13 9 18 \
  -13 9 18 \
  -7 -9 18
mkplane trap_top top_face
polyline side_yneg \
  -7 -9 -18 \
  7 -9 -18 \
  7 -9 18 \
  -7 -9 18 \
  -7 -9 -18
mkplane trap_yneg side_yneg
polyline side_xpos \
  7 -9 -18 \
  13 9 -18 \
  13 9 18 \
  7 -9 18 \
  7 -9 -18
mkplane trap_xpos side_xpos
polyline side_ypos \
  13 9 -18 \
  -13 9 -18 \
  -13 9 18 \
  13 9 18 \
  13 9 -18
mkplane trap_ypos side_ypos
polyline side_xneg \
  -13 9 -18 \
  -7 -9 -18 \
  -7 -9 18 \
  -13 9 18 \
  -13 9 -18
mkplane trap_xneg side_xneg

mkvolume seed_shape trap_bottom trap_top trap_yneg trap_xpos trap_ypos trap_xneg
stepwrite a seed_shape [file join $script_dir shape.step]
