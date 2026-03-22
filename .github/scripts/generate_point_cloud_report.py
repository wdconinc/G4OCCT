# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 G4OCCT Contributors

"""Generate a three.js point-cloud viewer (requires CDN access) from per-fixture .json.gz files.

Usage:
    python generate_point_cloud_report.py <point-cloud-dir> <output.html>

Each .json.gz file in <point-cloud-dir> must contain:
    fixture_id                  – qualified fixture path (family/id)
    geant4_class                – Geant4 solid class name
    ray_count                   – number of rays fired
    native_pre_step_origin      – [x, y, z]  (native solid launch point)
    imported_pre_step_origin    – [x, y, z]  (imported solid launch point)
    native_post_step_hits       – [[x,y,z], ...]
    imported_post_step_hits     – [[x,y,z], ...]
"""

import base64
import gzip
import json
import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from report_utils import timestamp, write_report

_SCRIPTS_DIR = Path(__file__).parent

_METADATA_KEYS = ("fixture_id", "geant4_class", "ray_count",
                  "native_pre_step_origin", "imported_pre_step_origin")
_REQUIRED_KEYS = _METADATA_KEYS + ("native_post_step_hits", "imported_post_step_hits")


def _load_fixture_data(point_cloud_dir: Path) -> tuple[list[dict[str, object]], dict[str, str]]:
    """Read all *.json.gz files in the directory tree recursively.

    Returns a tuple (metadata, blobs) where:
    - metadata: list of dicts with fixture metadata (no point arrays)
    - blobs: dict mapping fixture_id to base64-encoded raw gzip bytes
    """
    metadata = []
    blobs: dict[str, str] = {}
    for gz_path in sorted(point_cloud_dir.glob("**/*.json.gz")):
        rel = gz_path.relative_to(point_cloud_dir)
        try:
            raw_gz = gz_path.read_bytes()
            with gzip.open(gz_path, "rt", encoding="utf-8") as f:
                data = json.load(f)
            for key in _REQUIRED_KEYS:
                if key not in data:
                    print(f"Warning: {rel}: missing key '{key}', skipping",
                          file=sys.stderr)
                    break
            else:
                fixture_id = data["fixture_id"]
                if fixture_id in blobs:
                    print(
                        f"Warning: {rel}: duplicate fixture_id '{fixture_id}', skipping duplicate",
                        file=sys.stderr,
                    )
                    continue
                metadata.append({k: data[k] for k in _METADATA_KEYS})
                blobs[fixture_id] = base64.b64encode(raw_gz).decode("ascii")
        except (json.JSONDecodeError, OSError) as exc:
            print(f"Warning: could not load {rel}: {exc}", file=sys.stderr)
    return metadata, blobs


def _make_viewer_html(fixture_json: str, fixture_blobs: str, count_str: str) -> str:
    """Build Jinja2 environment, load assets, and render the viewer template."""
    # autoescape=False: css_content and js_content are trusted local files;
    # fixture_json and fixture_blobs are compact JSON whose values are geometry-only strings
    # and base64-encoded gzip blobs.
    env = Environment(
        loader=FileSystemLoader(str(_SCRIPTS_DIR)),
        autoescape=False,
        keep_trailing_newline=True,
    )
    template = env.get_template("point_cloud_viewer.html.jinja2")
    css_content = (_SCRIPTS_DIR / "point_cloud_viewer.css").read_text(encoding="utf-8")
    js_content = (_SCRIPTS_DIR / "point_cloud_viewer.js").read_text(encoding="utf-8")
    return template.render(
        css_content=css_content,
        js_content=js_content,
        fixture_json=fixture_json,
        fixture_blobs=fixture_blobs,
        timestamp=timestamp(),
        count_str=count_str,
    )


def _render_report(metadata: list[dict[str, object]], blobs: dict[str, str]) -> str:
    """Render a self-contained HTML viewer using the Jinja2 template."""
    def _safe_json(obj: object) -> str:
        # Escape "</" so the HTML parser cannot encounter "</script>" (or any
        # other closing tag) while reading the embedded <script> element,
        # regardless of the MIME type attribute.  "<\/" is valid JSON.
        return json.dumps(obj, separators=(",", ":")).replace("</", "<\\/")
    return _make_viewer_html(
        fixture_json=_safe_json(metadata),
        fixture_blobs=_safe_json(blobs),
        count_str=f"{len(metadata)} fixture(s)",
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page using the Jinja2 template."""
    return _make_viewer_html(fixture_json="[]", fixture_blobs="{}", count_str=f"Error: {message}")


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <point-cloud-dir> <output.html>", file=sys.stderr)
        sys.exit(1)

    cloud_dir = Path(sys.argv[1])
    html_path = Path(sys.argv[2])

    if not cloud_dir.is_dir():
        html_path.parent.mkdir(parents=True, exist_ok=True)
        html_path.write_text(_render_error(f"Directory not found: {cloud_dir}"), encoding="utf-8")
        print(f"Warning: {cloud_dir} is not a directory — wrote error page to {html_path}",
              file=sys.stderr)
        return

    metadata, blobs = _load_fixture_data(cloud_dir)
    html             = _render_report(metadata, blobs)
    write_report(html_path, html, label="Point-cloud viewer",
                 suffix=f" ({len(metadata)} fixture(s))")


if __name__ == "__main__":
    main()
