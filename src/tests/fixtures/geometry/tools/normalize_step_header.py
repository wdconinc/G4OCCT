#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

import re
import sys
from pathlib import Path

FIXED_TIMESTAMP = '2024-01-01T00:00:00'
FILE_NAME_PATTERN = re.compile(r"(FILE_NAME\('[^']*',')([^']*)(')")


def main() -> int:
    for raw_path in sys.argv[1:]:
        path = Path(raw_path)
        text = path.read_text(encoding='utf-8')
        normalized, count = FILE_NAME_PATTERN.subn(
            lambda match: f"{match.group(1)}{FIXED_TIMESTAMP}{match.group(3)}",
            text,
            count=1,
        )
        if count != 1:
            raise RuntimeError(f'Could not normalize FILE_NAME timestamp in {path}')
        path.write_text(normalized, encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
