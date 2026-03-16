# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Generate a three.js point-cloud viewer (requires CDN access) from per-fixture JSON files.

Usage:
    python generate_point_cloud_report.py <point-cloud-dir> <output.html>

Each JSON file in <point-cloud-dir> must contain:
    fixture_id                  – qualified fixture path (family/id)
    geant4_class                – Geant4 solid class name
    ray_count                   – number of rays fired
    native_pre_step_origin      – [x, y, z]  (native solid launch point)
    imported_pre_step_origin    – [x, y, z]  (imported solid launch point)
    native_post_step_hits       – [[x,y,z], ...]
    imported_post_step_hits     – [[x,y,z], ...]
"""

import json
import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from report_utils import timestamp, write_report

_SCRIPTS_DIR = Path(__file__).parent


def _load_fixture_data(point_cloud_dir: Path) -> list:
    """Read all *.json files in the directory; return list of dicts."""
    fixtures = []
    for json_path in sorted(point_cloud_dir.glob("*.json")):
        try:
            data = json.loads(json_path.read_text(encoding="utf-8"))
            for key in ("fixture_id", "geant4_class", "ray_count",
                        "native_pre_step_origin", "imported_pre_step_origin",
                        "native_post_step_hits", "imported_post_step_hits"):
                if key not in data:
                    print(f"Warning: {json_path.name}: missing key '{key}', skipping",
                          file=sys.stderr)
                    break
            else:
                fixtures.append(data)
        except (json.JSONDecodeError, OSError) as exc:
            print(f"Warning: could not load {json_path}: {exc}", file=sys.stderr)
    return fixtures


def _make_viewer_html(fixture_json: str, count_str: str) -> str:
    """Build Jinja2 environment, load assets, and render the viewer template."""
    # autoescape=False: css_content and js_content are trusted local files;
    # fixture_json is compact JSON whose values are geometry-only strings.
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
        timestamp=timestamp(),
        count_str=count_str,
    )


def _render_report(fixtures: list) -> str:
    """Render a self-contained HTML viewer using the Jinja2 template."""
    return _make_viewer_html(
        # Escape "</" so the HTML parser cannot encounter "</script>" (or any
        # other closing tag) while reading the embedded <script> element,
        # regardless of the MIME type attribute.  "<\/" is valid JSON.
        fixture_json=json.dumps(fixtures, separators=(",", ":")).replace("</", "<\\/"),
        count_str=f"{len(fixtures)} fixture(s)",
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page using the Jinja2 template."""
    return _make_viewer_html(fixture_json="[]", count_str=f"Error: {message}")


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

    fixtures = _load_fixture_data(cloud_dir)
    html     = _render_report(fixtures)
    write_report(html_path, html, label="Point-cloud viewer",
                 suffix=f" ({len(fixtures)} fixture(s))")


if __name__ == "__main__":
    main()
