# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline bottom_face \
  -10 -8 -20 \
  10 -8 -20 \
  10 8 -20 \
  -10 8 -20 \
  -10 -8 -20
mkplane trd_bottom bottom_face
polyline top_face \
  -16 -14 20 \
  16 -14 20 \
  16 14 20 \
  -16 14 20 \
  -16 -14 20
mkplane trd_top top_face
polyline side_yneg \
  -10 -8 -20 \
  10 -8 -20 \
  16 -14 20 \
  -16 -14 20 \
  -10 -8 -20
mkplane trd_yneg side_yneg
polyline side_xpos \
  10 -8 -20 \
  10 8 -20 \
  16 14 20 \
  16 -14 20 \
  10 -8 -20
mkplane trd_xpos side_xpos
polyline side_ypos \
  10 8 -20 \
  -10 8 -20 \
  -16 14 20 \
  16 14 20 \
  10 8 -20
mkplane trd_ypos side_ypos
polyline side_xneg \
  -10 8 -20 \
  -10 -8 -20 \
  -16 -14 20 \
  -16 14 20 \
  -10 8 -20
mkplane trd_xneg side_xneg

mkvolume seed_shape trd_bottom trd_top trd_yneg trd_xpos trd_ypos trd_xneg
stepwrite a seed_shape [file join $script_dir shape.step]
