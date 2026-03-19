# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
polyline bottom_hex 11.5470053837925 0 -12.5 5.77350269189626 10 -12.5 -5.77350269189626 10 -12.5 -11.5470053837925 0 -12.5 -5.77350269189626 -10 -12.5 5.77350269189626 -10 -12.5 11.5470053837925 0 -12.5
polyline top_hex 6.92820323027551 0 12.5 3.46410161513775 6 12.5 -3.46410161513775 6 12.5 -6.92820323027551 0 12.5 -3.46410161513775 -6 12.5 3.46410161513775 -6 12.5 6.92820323027551 0 12.5
thrusections seed_shape 1 1 bottom_hex top_hex -safe
stepwrite a seed_shape [file join $script_dir shape.step]
