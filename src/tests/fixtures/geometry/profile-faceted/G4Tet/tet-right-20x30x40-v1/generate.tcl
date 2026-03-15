# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]

polyline tri123 0 0 0 20 0 0 0 30 0 0 0 0
mkplane f123 tri123
polyline tri124 0 0 0 20 0 0 0 0 40 0 0 0
mkplane f124 tri124
polyline tri134 0 0 0 0 30 0 0 0 40 0 0 0
mkplane f134 tri134
polyline tri234 20 0 0 0 30 0 0 0 40 20 0 0
mkplane f234 tri234
mkvolume seed_shape f123 f124 f134 f234
stepwrite a seed_shape [file join $script_dir shape.step]
