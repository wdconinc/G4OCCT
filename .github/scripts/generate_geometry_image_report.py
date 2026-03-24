# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Generate an HTML gallery of geometry fixture JPEG images rendered by
Geant4's RayTracer visualisation driver.

Usage:
    python3 generate_geometry_image_report.py <images_dir> <output.html>

Each JPEG in <images_dir> must follow the naming convention produced by
render_geometry_fixtures:
    <safe_fixture_id>_native.jpeg
    <safe_fixture_id>_imported.jpeg

The fixture_id and label are inferred from the filename.
"""

import sys
from pathlib import Path

from jinja2 import Environment, FileSystemLoader

from report_utils import timestamp, write_report

_SCRIPTS_DIR = Path(__file__).parent


def _load_metadata(images_dir: Path, html_path: Path) -> list:
    """Return a list of dicts describing each JPEG in images_dir.

    ``image_rel`` is the path to each image relative to the HTML output file.
    """
    try:
        rel_prefix = images_dir.resolve().relative_to(html_path.resolve().parent)
    except ValueError:
        # If images_dir is not under html's parent, fall back to just the name.
        rel_prefix = Path(images_dir.name)

    items = []
    for img_path in sorted(images_dir.glob("*.jpeg")):
        stem = img_path.stem  # e.g. "direct-primitives_g4box-box-20x30x40-v1_native"

        # Infer label and fixture_id from the filename suffix.  The suffix
        # _native or _imported is stripped so fixture_id contains only the
        # safe-encoded fixture identifier (SafeFilename in C++ maps '/' → '_').
        fixture_id = stem
        label      = ""
        if stem.endswith("_native"):
            label      = "native"
            fixture_id = stem[: -len("_native")]
        elif stem.endswith("_imported"):
            label      = "imported"
            fixture_id = stem[: -len("_imported")]

        items.append({
            "fixture_id":   fixture_id,
            "geant4_class": "",
            "label":        label,
            "image_rel":    str(rel_prefix / img_path.name).replace("\\", "/"),
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

    items = _load_metadata(images_dir, html_path)
    html  = _render_gallery(items)
    write_report(html_path, html, label="Geometry image gallery", suffix=f" ({len(items)} images)")


if __name__ == "__main__":
    main()
