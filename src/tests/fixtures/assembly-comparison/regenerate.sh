#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

# Regenerate assembly-comparison STEP fixtures.
#
# DRAWEXE (generate.tcl) is required to regenerate fixtures.
#
# Usage:
#   bash src/tests/fixtures/assembly-comparison/regenerate.sh
#   DRAWEXE=/path/to/DRAWEXE bash src/tests/fixtures/assembly-comparison/regenerate.sh

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
drawexe=${DRAWEXE:-/opt/local/bin/DRAWEXE}

run_fixture() {
  local fixture_dir="$1"

  if ! command -v "$drawexe" >/dev/null 2>&1; then
    echo "Error: DRAWEXE not found at '$drawexe'. Set DRAWEXE=/path/to/DRAWEXE." >&2
    exit 1
  fi
  echo "Using DRAWEXE: $drawexe"
  "$drawexe" -b -f "${fixture_dir}/generate.tcl"
}

run_fixture "${script_dir}/triple-box-v1"
run_fixture "${script_dir}/string-array-v1"

normalizer="${script_dir}/../geometry/tools/normalize_step_header.py"
if [ ! -f "$normalizer" ]; then
  echo "Warning: normalize_step_header.py not found at $normalizer; skipping header normalisation"
else
  python3 "$normalizer" "${script_dir}/triple-box-v1/shape.step"
  python3 "$normalizer" "${script_dir}/string-array-v1/shape.step"
fi
