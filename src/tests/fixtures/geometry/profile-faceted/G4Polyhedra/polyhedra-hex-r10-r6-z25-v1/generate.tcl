# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

pload ALL

set script_dir [file normalize [file dirname [info script]]]
polyline bottom_hex 10 0 -12.5 5 8.660254037844386 -12.5 -5 8.660254037844387 -12.5 -10 0 -12.5 -5 -8.660254037844384 -12.5 5 -8.660254037844386 -12.5 10 0 -12.5
polyline top_hex 6 0 12.5 3 5.196152422706632 12.5 -3 5.196152422706632 12.5 -6 0 12.5 -3 -5.19615242270663 12.5 3 -5.196152422706632 12.5 6 0 12.5
thrusections seed_shape 1 1 bottom_hex top_hex -safe
stepwrite a seed_shape [file join $script_dir shape.step]
