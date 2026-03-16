# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Render G4Polyhedron mesh JSON files exported by render_geometry_fixtures
as PNG images using matplotlib's 3-D polygon collection.

Usage:
    python3 render_geometry_images.py <mesh_dir> <images_dir>

For each <mesh_dir>/*.json file a PNG is written to <images_dir>/ using the
same basename with a .png extension.
"""

import json
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")  # non-interactive / headless backend

import matplotlib.pyplot as plt
import numpy as np
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


def _render_mesh(faces: list, title: str, output_path: Path) -> None:
    """Render a list of polygon faces (each [[x,y,z], ...]) to a PNG."""
    if not faces:
        return

    all_verts = np.vstack([np.array(f) for f in faces])
    x_min, x_max = float(all_verts[:, 0].min()), float(all_verts[:, 0].max())
    y_min, y_max = float(all_verts[:, 1].min()), float(all_verts[:, 1].max())
    z_min, z_max = float(all_verts[:, 2].min()), float(all_verts[:, 2].max())

    span   = max(x_max - x_min, y_max - y_min, z_max - z_min, 1e-9)
    margin = 0.12 * span

    polys      = [np.array(f) for f in faces]
    collection = Poly3DCollection(
        polys,
        facecolor="#4c9fde",
        edgecolor="#1a4f7a",
        linewidths=0.2,
        alpha=0.85,
        zsort="average",
    )

    fig = plt.figure(figsize=(5, 5), dpi=100)
    ax  = fig.add_subplot(111, projection="3d")
    ax.add_collection3d(collection)

    ax.set_xlim(x_min - margin, x_max + margin)
    ax.set_ylim(y_min - margin, y_max + margin)
    ax.set_zlim(z_min - margin, z_max + margin)

    ax.set_xlabel("X (mm)", fontsize=7)
    ax.set_ylabel("Y (mm)", fontsize=7)
    ax.set_zlabel("Z (mm)", fontsize=7)
    ax.tick_params(labelsize=6)
    ax.set_title(title, fontsize=8, pad=4)

    # Isometric-ish viewpoint (elevation=25°, azimuth=45°)
    ax.view_init(elev=25, azim=45)
    ax.set_box_aspect([1, 1, 1])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(str(output_path), dpi=100, bbox_inches="tight")
    plt.close(fig)


def _process_file(json_path: Path, images_dir: Path) -> bool:
    """Read one mesh JSON file and write the corresponding PNG.  Returns True
    on success."""
    try:
        data = json.loads(json_path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as exc:
        print(f"Warning: could not load {json_path}: {exc}", file=sys.stderr)
        return False

    fixture_id   = data.get("fixture_id", json_path.stem)
    geant4_class = data.get("geant4_class", "")
    label        = data.get("label", "")
    faces        = data.get("faces", [])

    if not faces:
        print(f"Warning: {json_path.name}: no faces, skipping", file=sys.stderr)
        return False

    title      = f"{fixture_id} ({geant4_class})\n[{label}]"
    output_png = images_dir / (json_path.stem + ".png")
    _render_mesh(faces, title, output_png)
    return True


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <mesh_dir> <images_dir>", file=sys.stderr)
        sys.exit(1)

    mesh_dir   = Path(sys.argv[1])
    images_dir = Path(sys.argv[2])

    if not mesh_dir.is_dir():
        print(f"Warning: {mesh_dir} is not a directory, no images rendered.", file=sys.stderr)
        return

    json_files = sorted(mesh_dir.glob("*.json"))
    if not json_files:
        print(f"Warning: no JSON files found in {mesh_dir}", file=sys.stderr)
        return

    ok_count   = 0
    fail_count = 0
    for json_path in json_files:
        if _process_file(json_path, images_dir):
            ok_count += 1
        else:
            fail_count += 1

    print(f"Rendered {ok_count} mesh image(s); {fail_count} skipped/failed.")


if __name__ == "__main__":
    main()
