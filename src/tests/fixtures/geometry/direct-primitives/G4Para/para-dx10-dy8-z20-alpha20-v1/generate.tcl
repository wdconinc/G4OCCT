# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline bottom_face \
  -12.911761874129513 -8 -20 \
  7.088238125870487 -8 -20 \
  12.911761874129513 8 -20 \
  -7.088238125870487 8 -20 \
  -12.911761874129513 -8 -20
mkplane para_bottom bottom_face
polyline top_face \
  -12.911761874129513 -8 20 \
  7.088238125870487 -8 20 \
  12.911761874129513 8 20 \
  -7.088238125870487 8 20 \
  -12.911761874129513 -8 20
mkplane para_top top_face
polyline side_yneg \
  -12.911761874129513 -8 -20 \
  7.088238125870487 -8 -20 \
  7.088238125870487 -8 20 \
  -12.911761874129513 -8 20 \
  -12.911761874129513 -8 -20
mkplane para_yneg side_yneg
polyline side_xpos \
  7.088238125870487 -8 -20 \
  12.911761874129513 8 -20 \
  12.911761874129513 8 20 \
  7.088238125870487 -8 20 \
  7.088238125870487 -8 -20
mkplane para_xpos side_xpos
polyline side_ypos \
  12.911761874129513 8 -20 \
  -7.088238125870487 8 -20 \
  -7.088238125870487 8 20 \
  12.911761874129513 8 20 \
  12.911761874129513 8 -20
mkplane para_ypos side_ypos
polyline side_xneg \
  -7.088238125870487 8 -20 \
  -12.911761874129513 -8 -20 \
  -12.911761874129513 -8 20 \
  -7.088238125870487 8 20 \
  -7.088238125870487 8 -20
mkplane para_xneg side_xneg

mkvolume seed_shape para_bottom para_top para_yneg para_xpos para_ypos para_xneg
stepwrite a seed_shape [file join $script_dir shape.step]
