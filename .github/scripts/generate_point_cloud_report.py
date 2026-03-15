# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2024 G4OCCT Contributors

"""Generate a self-contained three.js point-cloud viewer from per-fixture JSON files.

Usage:
    python generate_point_cloud_report.py <point-cloud-dir> <output.html>

Each JSON file in <point-cloud-dir> must contain:
    fixture_id            – fixture path string
    geant4_class          – Geant4 solid class name
    ray_count             – number of rays fired
    pre_step_origin       – [x, y, z]  (shared launch point)
    native_post_step_hits – [[x,y,z], ...]
    imported_post_step_hits – [[x,y,z], ...]
"""

import json
import sys
from datetime import datetime, timezone
from html import escape
from pathlib import Path

# ---------------------------------------------------------------------------
# CSS for the page chrome (sidebar + header)
# ---------------------------------------------------------------------------
_CSS = """
    * { box-sizing: border-box; margin: 0; padding: 0; }
    html, body {
      height: 100%;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: #1a1a2e; color: #e0e0e0;
      display: flex; flex-direction: column;
    }
    #header {
      background: #16213e; padding: 10px 18px;
      display: flex; align-items: center; gap: 16px;
      border-bottom: 1px solid #0f3460; flex-shrink: 0;
    }
    #header h1 { font-size: 1.1rem; color: #e0e0ff; white-space: nowrap; }
    #header .meta { font-size: 0.8rem; color: #888; }
    #main { display: flex; flex: 1; overflow: hidden; }
    #sidebar {
      width: 310px; flex-shrink: 0;
      background: #16213e; border-right: 1px solid #0f3460;
      display: flex; flex-direction: column; overflow: hidden;
    }
    #sidebar-inner { padding: 12px; overflow-y: auto; flex: 1; }
    #sidebar label { font-size: 0.78rem; color: #aaa; display: block; margin-bottom: 4px; }
    #fixture-select {
      width: 100%; background: #0f3460; color: #e0e0e0;
      border: 1px solid #334; border-radius: 4px;
      padding: 6px 8px; font-size: 0.82rem; margin-bottom: 12px;
    }
    .toggle-row { display: flex; gap: 8px; margin-bottom: 12px; }
    .toggle-btn {
      flex: 1; padding: 6px 0; border: none; border-radius: 4px;
      cursor: pointer; font-size: 0.8rem; font-weight: 600;
    }
    .toggle-btn.native  { background: #1e5fa8; color: #fff; }
    .toggle-btn.imported { background: #a85c1e; color: #fff; }
    .toggle-btn.off { opacity: 0.35; }
    .stats-table { width: 100%; border-collapse: collapse; font-size: 0.82rem; }
    .stats-table td { padding: 4px 6px; border-bottom: 1px solid #1e2a4a; }
    .stats-table td:first-child { color: #aaa; }
    .stats-table td:last-child  { color: #e0e0e0; font-weight: 600; text-align: right; }
    .legend { display: flex; gap: 12px; font-size: 0.78rem; margin-top: 10px; }
    .legend-item { display: flex; align-items: center; gap: 5px; }
    .legend-dot {
      width: 10px; height: 10px; border-radius: 50%; display: inline-block;
    }
    .legend-dot.native   { background: #4a90d9; }
    .legend-dot.imported { background: #e87040; }
    #empty-msg {
      text-align: center; padding: 40px 20px;
      color: #666; font-size: 0.9rem; display: none;
    }
    #canvas-container { flex: 1; position: relative; }
    #canvas-container canvas { display: block; }
    #info-overlay {
      position: absolute; bottom: 10px; right: 12px;
      font-size: 0.75rem; color: #556; pointer-events: none;
    }
    #count-overlay {
      position: absolute; bottom: 10px; left: 12px;
      font-size: 0.75rem; color: #556; pointer-events: none;
    }
"""

# ---------------------------------------------------------------------------
# Inline three.js viewer (ES-module, loads from CDN)
# ---------------------------------------------------------------------------
_JS = r"""
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

// ── Data injected by the Python generator ──────────────────────────────────
const ALL_FIXTURES = /*FIXTURE_DATA*/;

// ── UI elements ─────────────────────────────────────────────────────────────
const selectEl   = document.getElementById('fixture-select');
const btnNative  = document.getElementById('btn-native');
const btnImp     = document.getElementById('btn-imported');
const statsEl    = document.getElementById('stats-body');
const countEl    = document.getElementById('count-overlay');
const emptyMsg   = document.getElementById('empty-msg');

// ── Three.js setup ───────────────────────────────────────────────────────────
const container = document.getElementById('canvas-container');
const renderer  = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setClearColor(0x1a1a2e);
container.appendChild(renderer.domElement);

const scene  = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(60, 1, 0.001, 1e6);
camera.position.set(0, 0, 200);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.screenSpacePanning = true;

// Axes helper (toggled with scene)
const axes = new THREE.AxesHelper(50);
scene.add(axes);

// ── Point-cloud state ────────────────────────────────────────────────────────
let nativeCloud   = null;
let importedCloud = null;
let showNative    = true;
let showImported  = true;

function makeCloud(points, color) {
  if (points.length === 0) { return null; }
  const positions = new Float32Array(points.length * 3);
  for (let i = 0; i < points.length; i++) {
    positions[i * 3]     = points[i][0];
    positions[i * 3 + 1] = points[i][1];
    positions[i * 3 + 2] = points[i][2];
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  const mat = new THREE.PointsMaterial({ color, size: 1.5, sizeAttenuation: false });
  return new THREE.Points(geo, mat);
}

function clearClouds() {
  if (nativeCloud)   { scene.remove(nativeCloud);   nativeCloud.geometry.dispose();   nativeCloud = null; }
  if (importedCloud) { scene.remove(importedCloud); importedCloud.geometry.dispose(); importedCloud = null; }
}

function loadFixture(fixture) {
  clearClouds();
  if (!fixture) { return; }

  nativeCloud   = makeCloud(fixture.native_post_step_hits,   0x4a90d9);
  importedCloud = makeCloud(fixture.imported_post_step_hits, 0xe87040);
  if (nativeCloud)   { nativeCloud.visible   = showNative;   scene.add(nativeCloud); }
  if (importedCloud) { importedCloud.visible = showImported; scene.add(importedCloud); }

  // Fit camera to the bounding box of the visible hits
  const all = (nativeCloud || importedCloud);
  if (all) {
    const box    = new THREE.Box3().setFromObject(all);
    const center = new THREE.Vector3();
    box.getCenter(center);
    const size   = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z, 1e-3);
    camera.position.copy(center).addScaledVector(new THREE.Vector3(1, 0.8, 1).normalize(),
                                                  maxDim * 2.0);
    controls.target.copy(center);
    controls.update();
  }

  // Stats
  const nh = fixture.native_post_step_hits.length;
  const ih = fixture.imported_post_step_hits.length;
  statsEl.innerHTML = `
    <tr><td>Fixture</td><td>${escHtml(fixture.fixture_id)}</td></tr>
    <tr><td>Class</td><td>${escHtml(fixture.geant4_class)}</td></tr>
    <tr><td>Rays fired</td><td>${fixture.ray_count}</td></tr>
    <tr><td>Native hits</td><td>${nh}</td></tr>
    <tr><td>Imported hits</td><td>${ih}</td></tr>
    <tr><td>Origin</td><td>${fixture.pre_step_origin.map(v => v.toFixed(2)).join(', ')}</td></tr>
  `;
  countEl.textContent = `native: ${nh} pts  |  imported: ${ih} pts`;
}

function escHtml(s) {
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

// ── Populate dropdown ────────────────────────────────────────────────────────
function populateSelect() {
  if (ALL_FIXTURES.length === 0) {
    emptyMsg.style.display = 'block';
    return;
  }
  // Group by family (first path segment before first '_')
  const families = {};
  for (const f of ALL_FIXTURES) {
    const family = f.fixture_id.split('/')[0] || 'other';
    if (!families[family]) { families[family] = []; }
    families[family].push(f);
  }
  for (const [family, fixtures] of Object.entries(families)) {
    const group = document.createElement('optgroup');
    group.label = family;
    for (const f of fixtures) {
      const opt = document.createElement('option');
      opt.value = f.fixture_id;
      opt.textContent = f.fixture_id.split('/').slice(1).join('/') + ' (' + f.geant4_class + ')';
      group.appendChild(opt);
    }
    selectEl.appendChild(group);
  }
}

populateSelect();
if (ALL_FIXTURES.length > 0) { loadFixture(ALL_FIXTURES[0]); }

selectEl.addEventListener('change', () => {
  const f = ALL_FIXTURES.find(x => x.fixture_id === selectEl.value);
  loadFixture(f || null);
});

// ── Toggle buttons ────────────────────────────────────────────────────────────
btnNative.addEventListener('click', () => {
  showNative = !showNative;
  btnNative.classList.toggle('off', !showNative);
  if (nativeCloud) { nativeCloud.visible = showNative; }
});
btnImp.addEventListener('click', () => {
  showImported = !showImported;
  btnImp.classList.toggle('off', !showImported);
  if (importedCloud) { importedCloud.visible = showImported; }
});

// ── Resize handler ────────────────────────────────────────────────────────────
function onResize() {
  const w = container.clientWidth;
  const h = container.clientHeight;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  renderer.setSize(w, h);
}
window.addEventListener('resize', onResize);
onResize();

// ── Render loop ───────────────────────────────────────────────────────────────
function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}
animate();
"""


def _load_fixture_data(point_cloud_dir: Path) -> list:
    """Read all *.json files in the directory; return list of dicts."""
    fixtures = []
    for json_path in sorted(point_cloud_dir.glob("*.json")):
        try:
            data = json.loads(json_path.read_text(encoding="utf-8"))
            # Validate required keys
            for key in ("fixture_id", "geant4_class", "ray_count",
                        "pre_step_origin",
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


def _render_report(fixtures: list) -> str:
    """Return a self-contained HTML string with embedded three.js viewer."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    count_str = f"{len(fixtures)} fixture(s)"

    # Serialise fixture data as compact JSON
    fixture_json = json.dumps(fixtures, separators=(",", ":"))

    js_code = _JS.replace("/*FIXTURE_DATA*/", fixture_json)

    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "  <title>G4OCCT \u2014 Point Cloud Viewer</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "  <script type=\"importmap\">\n"
        "  {\"imports\":{"
        "\"three\":\"https://cdn.jsdelivr.net/npm/three@0.169.0/build/three.module.js\","
        "\"three/addons/\":\"https://cdn.jsdelivr.net/npm/three@0.169.0/examples/jsm/\""
        "}}\n"
        "  </script>\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"header\">\n"
        "    <h1>G4OCCT \u2014 Geantino Ray Point Cloud</h1>\n"
        f"    <span class=\"meta\">Generated: {escape(timestamp)}"
        f" \u00b7 {escape(count_str)}</span>\n"
        "  </div>\n"
        "  <div id=\"main\">\n"
        "    <div id=\"sidebar\">\n"
        "      <div id=\"sidebar-inner\">\n"
        "        <label for=\"fixture-select\">Fixture</label>\n"
        "        <select id=\"fixture-select\"></select>\n"
        "        <div class=\"toggle-row\">\n"
        "          <button id=\"btn-native\" class=\"toggle-btn native\">Native</button>\n"
        "          <button id=\"btn-imported\" class=\"toggle-btn imported\">Imported</button>\n"
        "        </div>\n"
        "        <table class=\"stats-table\">"
        "<tbody id=\"stats-body\"></tbody></table>\n"
        "        <div class=\"legend\">\n"
        "          <div class=\"legend-item\">"
        "<span class=\"legend-dot native\"></span>Native</div>\n"
        "          <div class=\"legend-item\">"
        "<span class=\"legend-dot imported\"></span>Imported</div>\n"
        "        </div>\n"
        "        <div id=\"empty-msg\">No point-cloud data found.</div>\n"
        "      </div>\n"
        "    </div>\n"
        "    <div id=\"canvas-container\">\n"
        "      <div id=\"count-overlay\"></div>\n"
        "      <div id=\"info-overlay\">drag to rotate \u00b7 scroll to zoom \u00b7 right-drag to pan</div>\n"
        "    </div>\n"
        "  </div>\n"
        "  <script type=\"module\">\n"
        f"{js_code}\n"
        "  </script>\n"
        "</body>\n"
        "</html>\n"
    )


def _render_error(message: str) -> str:
    """Render a minimal HTML error page."""
    timestamp = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    return (
        "<!DOCTYPE html>\n"
        "<html lang=\"en\">\n"
        "<head>\n"
        "  <meta charset=\"UTF-8\">\n"
        "  <title>G4OCCT \u2014 Point Cloud Viewer</title>\n"
        f"  <style>\n{_CSS}\n  </style>\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"header\">\n"
        "    <h1>G4OCCT \u2014 Geantino Ray Point Cloud</h1>\n"
        f"    <span class=\"meta\">Generated: {escape(timestamp)}</span>\n"
        "  </div>\n"
        "  <div style=\"padding:24px; color:#c44;\">\n"
        f"    Could not generate report: {escape(message)}\n"
        "  </div>\n"
        "</body>\n"
        "</html>\n"
    )


def main() -> None:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <point-cloud-dir> <output.html>", file=sys.stderr)
        sys.exit(1)

    cloud_dir  = Path(sys.argv[1])
    html_path  = Path(sys.argv[2])
    html_path.parent.mkdir(parents=True, exist_ok=True)

    if not cloud_dir.is_dir():
        html_path.write_text(_render_error(f"Directory not found: {cloud_dir}"), encoding="utf-8")
        print(f"Warning: {cloud_dir} is not a directory — wrote error page to {html_path}",
              file=sys.stderr)
        return

    fixtures = _load_fixture_data(cloud_dir)
    html     = _render_report(fixtures)
    html_path.write_text(html, encoding="utf-8")
    print(f"Point-cloud viewer written to: {html_path} ({len(fixtures)} fixture(s))")


if __name__ == "__main__":
    main()
