#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Translate all 3D CARTESIAN_POINTs in a STEP file by (dx, dy, dz).

2D parametric (UV) CARTESIAN_POINTs are left unchanged.
The root AXIS2_PLACEMENT_3D of each ADVANCED_BREP_SHAPE_REPRESENTATION
is also left unchanged — it defines the coordinate frame, not geometry.
Usage: translate_step.py <step_file> <dx> <dy> <dz>
"""

import re
import sys
from pathlib import Path

# Matches 3D CARTESIAN_POINT, capturing the entity id and three coordinate values
CART_3D = re.compile(
    r"(#(\d+)\s*=\s*CARTESIAN_POINT\('',\()([^,)]+),([^,)]+),([^,)]+)(\)\))"
)

# Finds the first entity listed in ADVANCED_BREP_SHAPE_REPRESENTATION (the root placement)
ABREP = re.compile(r"ADVANCED_BREP_SHAPE_REPRESENTATION\([^,]*,\(#(\d+)")

# Maps AXIS2_PLACEMENT_3D entity id to its CARTESIAN_POINT entity id
A2P3D = re.compile(r"#(\d+)\s*=\s*AXIS2_PLACEMENT_3D\('',#(\d+)")


def fmt(v: float) -> str:
    # Use :.15g to avoid float repr noise (e.g. 21.200000000000003 vs 21.2).
    s = f"{v:.15g}"
    # Use uppercase E for scientific notation (STEP convention).
    s = s.replace("e", "E")
    # STEP convention: integer values need a trailing dot ("12." not "12").
    if "." not in s and "E" not in s:
        s += "."
    # STEP convention: strip ".0" before E ("1.E+10" not "1.0E+10").
    s = re.sub(r"\.0($|E)", r".\1", s)
    return s


def find_excluded_points(text: str) -> set[str]:
    """Return CARTESIAN_POINT entity ids that must not be translated.

    The root AXIS2_PLACEMENT_3D of each ADVANCED_BREP_SHAPE_REPRESENTATION
    defines the global coordinate frame.  Shifting it would cause OCCT to
    apply the translation twice when reading the file back.
    """
    root_a2p3d_ids = set(ABREP.findall(text))
    a2p3d_to_cp = {m.group(1): m.group(2) for m in A2P3D.finditer(text)}
    return {a2p3d_to_cp[i] for i in root_a2p3d_ids if i in a2p3d_to_cp}


def shift_match(
    match: re.Match, excluded: set[str], dx: float, dy: float, dz: float
) -> str:
    if match.group(2) in excluded:
        return match.group(0)
    x = float(match.group(3)) + dx
    y = float(match.group(4)) + dy
    z = float(match.group(5)) + dz
    return f"{match.group(1)}{fmt(x)},{fmt(y)},{fmt(z)}{match.group(6)}"


def main() -> int:
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <step_file> <dx> <dy> <dz>", file=sys.stderr)
        return 1

    path = Path(sys.argv[1])
    dx, dy, dz = float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])

    text = path.read_text(encoding="utf-8")
    excluded = find_excluded_points(text)
    result = CART_3D.sub(lambda m: shift_match(m, excluded, dx, dy, dz), text)
    path.write_text(result, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
