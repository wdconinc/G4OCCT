# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Generate an HTML gallery of geometry fixture PNG images.

Usage:
    python3 generate_geometry_image_report.py <images_dir> <output.html>

Each PNG in <images_dir> must follow the naming convention produced by
render_geometry_images.py:
    <safe_fixture_id>_native.png
    <safe_fixture_id>_imported.png

The corresponding mesh JSON files (from render_geometry_fixtures) are read
from the same directory to extract fixture_id, geant4_class, and label
metadata.  If the JSON files are absent the information is inferred from the
filename.
"""

import json
import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from report_utils import timestamp, write_report

_SCRIPTS_DIR = Path(__file__).parent


def _load_metadata(images_dir: Path) -> list:
    """Return a list of dicts describing each PNG in images_dir."""
    items = []
    for png_path in sorted(images_dir.glob("*.png")):
        stem = png_path.stem  # e.g. "direct-primitives_g4box-box-20x30x40-v1_native"

        # Try to read companion JSON for authoritative metadata.
        json_path = png_path.with_suffix(".json")
        # JSON files may live in a sibling directory (mesh_dir) or the same dir.
        fixture_id   = stem
        geant4_class = ""
        label        = ""
        if json_path.exists():
            try:
                data         = json.loads(json_path.read_text(encoding="utf-8"))
                fixture_id   = data.get("fixture_id",   stem)
                geant4_class = data.get("geant4_class", "")
                label        = data.get("label",        "")
            except (json.JSONDecodeError, OSError):
                pass

        # Fall back to parsing the filename when JSON is absent.
        if not label:
            if stem.endswith("_native"):
                label = "native"
            elif stem.endswith("_imported"):
                label = "imported"
        if not geant4_class and "_" in fixture_id:
            # Best-effort: geant4_class is embedded in the fixture id slug.
            pass

        items.append({
            "fixture_id":   fixture_id,
            "geant4_class": geant4_class,
            "label":        label,
            "png_rel":      png_path.name,
        })
    return items


def _render_gallery(items: list) -> str:
    """Render the HTML gallery using the Jinja2 template."""
    classes = {item["geant4_class"] for item in items if item["geant4_class"]}
    env = Environment(
        loader=FileSystemLoader(str(_SCRIPTS_DIR)),
        autoescape=True,
        keep_trailing_newline=True,
    )
    template   = env.get_template("geometry_image_gallery.html.jinja2")
    count_str  = f"{len(items)} image(s)"
    return template.render(
        items=items,
        classes=classes,
        timestamp=timestamp(),
        count_str=count_str,
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page."""
    return _render_gallery([])


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <images_dir> <output.html>", file=sys.stderr)
        sys.exit(1)

    images_dir = Path(sys.argv[1])
    html_path  = Path(sys.argv[2])

    if not images_dir.is_dir():
        html_path.parent.mkdir(parents=True, exist_ok=True)
        html_path.write_text(_render_gallery([]), encoding="utf-8")
        print(f"Warning: {images_dir} is not a directory — wrote empty gallery to {html_path}",
              file=sys.stderr)
        return

    items = _load_metadata(images_dir)
    html  = _render_gallery(items)
    write_report(html_path, html, label="Geometry image gallery", suffix=f" ({len(items)} images)")


if __name__ == "__main__":
    main()
