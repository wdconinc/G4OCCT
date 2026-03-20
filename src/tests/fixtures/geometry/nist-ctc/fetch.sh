#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

# fetch.sh — Download the NIST MBE PMI Simplified Test Case STEP files and
# install them into the nist-ctc fixture directories.
#
# Usage:
#   bash fetch.sh
#
# The script downloads NIST-PMI-STEP-Files.zip from the usnistgov/SFA GitHub
# repository, extracts the 11 AP203 geometry-only STEP files, and copies them
# (sorted by filename) to the G4OCCTSolid/nist-ctc-NN-v1/shape.step paths
# expected by the test manifest.
#
# Reference:
#   https://github.com/usnistgov/SFA/tree/master/Release

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Pinned to commit 48bcd4ccef07db7b903baaefaf6a2eb427d02b05 in usnistgov/SFA for reproducibility.
ZIP_URL="https://raw.githubusercontent.com/usnistgov/SFA/48bcd4ccef07db7b903baaefaf6a2eb427d02b05/Release/NIST-PMI-STEP-Files.zip"
TMP_DIR="$(mktemp -d)"
ZIP_FILE="${TMP_DIR}/NIST-PMI-STEP-Files.zip"
EXTRACT_DIR="${TMP_DIR}/extracted"

make_tree_user_writable() {
  local path="$1"

  if [[ -e "${path}" ]]; then
    chmod -R u+rwX "${path}"
  fi
}

cleanup() {
  make_tree_user_writable "${TMP_DIR}"
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

echo "Downloading ${ZIP_URL} ..."
curl -L --fail --max-time 300 --retry 3 -o "${ZIP_FILE}" "${ZIP_URL}"

echo "Validating ZIP integrity ..."
if ! unzip -t "${ZIP_FILE}" > /dev/null 2>&1; then
  echo "ERROR: Downloaded file is not a valid ZIP archive." >&2
  echo "File contents (first 256 bytes):" >&2
  head -c 256 "${ZIP_FILE}" >&2
  exit 1
fi

echo "Extracting ZIP ..."
mkdir -p "${EXTRACT_DIR}"
unzip -q "${ZIP_FILE}" -d "${EXTRACT_DIR}"
# The archive stores extracted directories as read-only, which prevents later
# file removal (including trap cleanup) unless we normalize permissions first.
make_tree_user_writable "${EXTRACT_DIR}"

# Locate the "AP203 geometry only" directory (case-insensitive match).
AP203_DIR=""
while IFS= read -r -d '' dir; do
  base="$(basename "${dir}")"
  base_lower="${base,,}"
  if [[ "${base_lower}" == *"ap203"* ]] || [[ "${base_lower}" == "ap203 geometry only" ]]; then
    # Accept the directory if it contains any .stp or .step files.
    if compgen -G "${dir}/*.stp" > /dev/null 2>&1 || \
       compgen -G "${dir}/*.step" > /dev/null 2>&1; then
      AP203_DIR="${dir}"
      break
    fi
  fi
done < <(find "${EXTRACT_DIR}" -type d -print0 | sort -z)

if [[ -z "${AP203_DIR}" ]]; then
  echo "ERROR: Could not find an 'AP203 geometry only' directory with STEP files." >&2
  echo "ZIP contents:" >&2
  unzip -l "${ZIP_FILE}" >&2
  exit 1
fi

echo "Found AP203 directory: ${AP203_DIR}"

# Collect and sort all STEP files in the AP203 directory.
mapfile -t STP_FILES < <(
  find "${AP203_DIR}" -maxdepth 1 \( -name "*.stp" -o -name "*.step" \) | sort
)

echo "Found ${#STP_FILES[@]} STEP files:"
for f in "${STP_FILES[@]}"; do
  echo "  $(basename "${f}")"
done

if [[ "${#STP_FILES[@]}" -ne 11 ]]; then
  echo "ERROR: Expected exactly 11 STEP files, found ${#STP_FILES[@]}." >&2
  exit 1
fi

# Install each file as shape.step in the corresponding fixture directory.
for i in "${!STP_FILES[@]}"; do
  NUM="$(printf '%02d' $((i + 1)))"
  DEST_DIR="${SCRIPT_DIR}/G4OCCTSolid/nist-ctc-${NUM}-v1"
  if [[ ! -d "${DEST_DIR}" ]]; then
    echo "ERROR: Fixture directory not found: ${DEST_DIR}" >&2
    exit 1
  fi
  cp "${STP_FILES[${i}]}" "${DEST_DIR}/shape.step"
  echo "Installed: $(basename "${STP_FILES[${i}]}") -> ${DEST_DIR}/shape.step"
done

echo "All 11 NIST CTC STEP files installed successfully."
