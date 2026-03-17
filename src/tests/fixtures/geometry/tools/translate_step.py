#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Translate all 3D CARTESIAN_POINTs in a STEP file by (dx, dy, dz).

2D parametric (UV) CARTESIAN_POINTs are left unchanged.
Usage: translate_step.py <step_file> <dx> <dy> <dz>
"""

import re
import sys
from pathlib import Path

CART_3D = re.compile(
    r"(CARTESIAN_POINT\('',\()([^,)]+),([^,)]+),([^,)]+)(\)\))"
)


def shift_match(match: re.Match, dx: float, dy: float, dz: float) -> str:
    x = float(match.group(2)) + dx
    y = float(match.group(3)) + dy
    z = float(match.group(4)) + dz

    def fmt(v: float) -> str:
        s = repr(v)
        # STEP convention: uppercase E for scientific notation
        s = s.replace("e", "E")
        # STEP convention: trailing dot without zero ("0." not "0.0", "1.E+10" not "1.0E+10")
        s = re.sub(r"\.0($|E)", r".\1", s)
        return s

    return f"{match.group(1)}{fmt(x)},{fmt(y)},{fmt(z)}{match.group(5)}"


def main() -> int:
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} <step_file> <dx> <dy> <dz>", file=sys.stderr)
        return 1

    path = Path(sys.argv[1])
    dx, dy, dz = float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])

    text = path.read_text(encoding="utf-8")
    result = CART_3D.sub(lambda m: shift_match(m, dx, dy, dz), text)
    path.write_text(result, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
