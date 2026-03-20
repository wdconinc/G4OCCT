# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Upload G4OCCT geometry test fixtures (STEP files) to Onshape and write a
JSON map of ``fixture_id → public_url`` for use by the benchmark report.

Usage
-----
::

    python3 upload_step_to_onshape.py <manifest.yaml> <output_links.json>

The script reads ``ONSHAPE_ACCESS_KEY`` and ``ONSHAPE_SECRET_KEY`` from the
environment.  If either is absent the script exits successfully with an empty
JSON object (so downstream report generators still work).

Fixture IDs
-----------
Fixture IDs match the benchmark output format: ``{family}/{fixture.id}``
(e.g. ``direct-primitives/g4box-box-20x30x40-v1``).  They are derived from
the per-family ``manifest.yaml`` files, not from the directory tree, so the
keys in ``onshape_links.json`` line up exactly with the fixture IDs emitted by
``bench_navigator``.

Deduplication and freshness
---------------------------
Each fixture is uploaded to an Onshape document named
``g4occt/{fixture_id}@{hash8}`` where ``hash8`` is the first 8 hex digits of
the SHA-256 of the STEP file contents.  Including the hash means:

* An unchanged fixture reuses the same document on every CI run (no re-upload).
* A regenerated STEP file gets a new document automatically (hash changes).

Onshape API calls used
----------------------
* ``GET  /api/v10/documents``                 – search for existing document
* ``POST /api/v10/documents``                 – create new document
* ``POST /api/v10/documents/d/{did}/import``  – upload STEP file
* ``GET  /api/v10/translations/{tid}``        – poll translation job state
* ``POST /api/v10/documents/{did}/share``     – make document public
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
import time
from pathlib import Path

import requests
import yaml

# ---------------------------------------------------------------------------
# Onshape helpers
# ---------------------------------------------------------------------------

_BASE = "https://cad.onshape.com/api/v10"
_POLL_INTERVAL_S = 3
_POLL_TIMEOUT_S = 60


def _session(access_key: str, secret_key: str) -> requests.Session:
    """Return a requests Session with Basic-auth and JSON accept header."""
    s = requests.Session()
    s.auth = (access_key, secret_key)
    s.headers.update({"Accept": "application/json"})
    return s


def _find_document(session: requests.Session, name: str) -> str | None:
    """Return an existing document ID matching *name*, or ``None``."""
    resp = session.get(
        f"{_BASE}/documents",
        params={"q": name, "filter": 0},  # filter=0 → MY_DOCUMENTS
    )
    if not resp.ok:
        print(f"  Warning: document search failed ({resp.status_code}): {resp.text[:200]}",
              file=sys.stderr)
        return None
    for item in resp.json().get("items", []):
        if item.get("name") == name:
            return item["id"]
    return None


def _create_document(session: requests.Session, name: str) -> str:
    """Create a new Onshape document and return its ID."""
    resp = session.post(
        f"{_BASE}/documents",
        json={"name": name},
    )
    resp.raise_for_status()
    return resp.json()["id"]


def _import_step(session: requests.Session, did: str, step_path: Path) -> str:
    """Start a STEP import into document *did* and return the translation ID."""
    with step_path.open("rb") as fh:
        resp = session.post(
            f"{_BASE}/documents/d/{did}/import",
            files={"file": (step_path.name, fh, "application/step")},
            data={
                "formatName": "STEP",
                "allowFaultyParts": "true",
                "flattenAssemblies": "false",
                "yAxisIsUp": "false",
            },
        )
    resp.raise_for_status()
    return resp.json()["id"]


def _wait_for_translation(session: requests.Session, tid: str) -> bool:
    """Poll translation *tid* until it finishes.  Returns ``True`` on success."""
    deadline = time.monotonic() + _POLL_TIMEOUT_S
    while time.monotonic() < deadline:
        resp = session.get(f"{_BASE}/translations/{tid}")
        if not resp.ok:
            print(f"  Warning: translation poll failed ({resp.status_code}): {resp.text[:200]}",
                  file=sys.stderr)
            return False
        state = resp.json().get("requestState", "")
        if state == "DONE":
            return True
        if state in ("FAILED", ""):
            print(f"  Translation failed with state={state!r}", file=sys.stderr)
            return False
        time.sleep(_POLL_INTERVAL_S)
    print("  Translation timed out", file=sys.stderr)
    return False


def _share_public(session: requests.Session, did: str) -> None:
    """Make Onshape document *did* publicly viewable (read-only)."""
    resp = session.post(
        f"{_BASE}/documents/{did}/share",
        json={
            "entries": [
                {
                    "permissionSet": ["READ"],
                    "public": True,
                }
            ]
        },
    )
    if not resp.ok:
        print(f"  Warning: share request failed ({resp.status_code}): {resp.text[:200]}",
              file=sys.stderr)


def _public_url(did: str) -> str:
    return f"https://cad.onshape.com/documents/{did}"


def _step_hash8(step_path: Path) -> str:
    """Return the first 8 hex digits of the SHA-256 of *step_path* contents."""
    h = hashlib.sha256()
    with step_path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


# ---------------------------------------------------------------------------
# Fixture discovery
# ---------------------------------------------------------------------------

def _discover_fixtures(manifest_path: Path) -> list[tuple[str, Path]]:
    """Return ``[(fixture_id, step_path), …]`` for all fixtures in *manifest*.

    Fixture IDs are ``{family}/{fixture.id}`` as used by the benchmark output,
    derived from each family's ``manifest.yaml`` rather than the filesystem
    tree.  NIST CTC and other fixtures whose STEP file has not yet been
    downloaded are silently skipped.
    """
    root_manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8"))
    fixture_root = Path(root_manifest["fixture_root"])
    if not fixture_root.is_absolute():
        # fixture_root is relative to the repository root; the script is always
        # invoked from the repository root in CI, so resolve against CWD.
        fixture_root = Path.cwd() / fixture_root
    top_step_name = root_manifest.get("policy", {}).get("step_file_name", "shape.step")

    result: list[tuple[str, Path]] = []
    for family in root_manifest.get("families", []):
        family_dir = fixture_root / family
        if not family_dir.is_dir():
            continue

        family_manifest_path = family_dir / "manifest.yaml"
        if not family_manifest_path.is_file():
            continue

        family_manifest = yaml.safe_load(family_manifest_path.read_text(encoding="utf-8"))
        family_step_name = (
            family_manifest.get("policy", {}).get("step_file_name") or top_step_name
        )

        for fixture in family_manifest.get("fixtures", []):
            fixture_id_value = fixture.get("id")
            rel_dir = fixture.get("relative_directory")
            if not fixture_id_value or not rel_dir:
                continue

            fixture_step_name = fixture.get("step_file") or family_step_name
            step_path = family_dir / rel_dir / fixture_step_name
            if not step_path.is_file():
                # STEP file not yet downloaded (e.g. NIST CTC); skip silently.
                continue

            fixture_id = f"{family}/{fixture_id_value}"
            result.append((fixture_id, step_path))
    return result


# ---------------------------------------------------------------------------
# Upload orchestration
# ---------------------------------------------------------------------------

def _doc_name(fixture_id: str, step_path: Path) -> str:
    """Return the canonical Onshape document name for a fixture.

    The STEP file's SHA-256 prefix is embedded so that a regenerated STEP file
    automatically gets a new document while an unchanged one reuses the same
    existing document.
    """
    return f"g4occt/{fixture_id}@{_step_hash8(step_path)}"


def _upload_fixture(
    session: requests.Session,
    fixture_id: str,
    step_path: Path,
) -> str | None:
    """Ensure the fixture is uploaded to Onshape and return its public URL.

    Returns ``None`` on any unrecoverable error.
    """
    name = _doc_name(fixture_id, step_path)
    print(f"  [{fixture_id}]", file=sys.stderr)

    # Check for existing document first.
    did = _find_document(session, name)
    if did:
        print(f"    Found existing document {did}", file=sys.stderr)
        # Re-apply public sharing in case settings changed since last upload.
        _share_public(session, did)
        url = _public_url(did)
        print(f"    Public URL: {url}", file=sys.stderr)
        return url

    # Create a fresh document.
    try:
        did = _create_document(session, name)
    except requests.HTTPError as exc:
        print(f"    Failed to create document: {exc}", file=sys.stderr)
        return None
    print(f"    Created document {did}", file=sys.stderr)

    # Import the STEP file.
    try:
        tid = _import_step(session, did, step_path)
    except requests.HTTPError as exc:
        print(f"    Failed to start STEP import: {exc}", file=sys.stderr)
        return None
    print(f"    Import job {tid} started", file=sys.stderr)

    # Wait for the translation job to complete.
    if not _wait_for_translation(session, tid):
        return None
    print(f"    Import complete", file=sys.stderr)

    # Share document publicly.
    _share_public(session, did)
    url = _public_url(did)
    print(f"    Public URL: {url}", file=sys.stderr)
    return url


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <manifest.yaml> <output_links.json>",
              file=sys.stderr)
        sys.exit(1)

    manifest_path = Path(sys.argv[1])
    output_path   = Path(sys.argv[2])

    access_key = os.environ.get("ONSHAPE_ACCESS_KEY", "")
    secret_key = os.environ.get("ONSHAPE_SECRET_KEY", "")

    if not access_key or not secret_key:
        print("ONSHAPE_ACCESS_KEY / ONSHAPE_SECRET_KEY not set — skipping upload.",
              file=sys.stderr)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        output_path.write_text("{}\n", encoding="utf-8")
        return

    fixtures = _discover_fixtures(manifest_path)
    print(f"Discovered {len(fixtures)} fixture(s) with STEP files.", file=sys.stderr)

    session = _session(access_key, secret_key)
    links: dict[str, str] = {}

    for fixture_id, step_path in fixtures:
        url = _upload_fixture(session, fixture_id, step_path)
        if url:
            links[fixture_id] = url

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(links, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    print(f"Onshape links written to: {output_path} ({len(links)} entries)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
