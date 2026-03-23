// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2026 G4OCCT Contributors

// Fixture metadata is supplied by the HTML page as an inline JSON script element so
// that this file remains a static asset with no runtime replacements.
// Point cloud data is supplied as base64-encoded gzip blobs (one per fixture) and is
// decoded lazily only when the user selects a fixture, keeping initial load fast.
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';

const ALL_FIXTURES  = JSON.parse(document.getElementById('fixture-data').textContent);
const FIXTURE_BLOBS = JSON.parse(document.getElementById('fixture-blobs').textContent);

// ── UI elements ──────────────────────────────────────────────────────────────
const selectEl          = document.getElementById('fixture-select');
const btnNative         = document.getElementById('btn-native');
const btnImp            = document.getElementById('btn-imported');
const btnViewX          = document.getElementById('btn-view-x');
const btnViewY          = document.getElementById('btn-view-y');
const btnViewZ          = document.getElementById('btn-view-z');
const btnGrid           = document.getElementById('btn-grid');
const btnOrtho          = document.getElementById('btn-ortho');
const statsEl           = document.getElementById('stats-body');
const countEl           = document.getElementById('count-overlay');
const gridSpacingEl     = document.getElementById('grid-spacing-overlay');
const emptyMsg          = document.getElementById('empty-msg');
const sidebarEl         = document.getElementById('sidebar');
const sidebarToggle     = document.getElementById('sidebar-toggle');
const offsetSlider      = document.getElementById('offset-slider');
const offsetValue       = document.getElementById('offset-value');
offsetValue.textContent = `${parseFloat(offsetSlider.value).toFixed(1)}%`;

// ── Three.js setup ────────────────────────────────────────────────────────────
const container = document.getElementById('canvas-container');
const renderer  = new THREE.WebGLRenderer({antialias : true});
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setClearColor(0x1a1a2e);
container.appendChild(renderer.domElement);

const scene  = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(60, 1, 0.001, 1e6);
camera.position.set(0, 0, 200);

const orthoCamera = new THREE.OrthographicCamera(-100, 100, 100, -100, -1e6, 1e6);
// Frustum bounds above are placeholder; syncOrthoFrustum() recomputes them on first toggle.
let useOrtho = false;

const controls              = new OrbitControls(camera, renderer.domElement);
controls.enableDamping      = true;
controls.dampingFactor      = 0.08;
controls.screenSpacePanning = true;

// Axes helper — always visible to provide orientation reference
scene.add(new THREE.AxesHelper(50));

// ── Adaptive infinite grid ────────────────────────────────────────────────────
// Grids are rebuilt whenever the "nice" step size changes with camera distance,
// and their centres are snapped to multiples of the step so they tile infinitely.
const GRID_DIVS_COUNT   = 100;
const GRID_COLOR_CENTER = 0x444466;
const GRID_COLOR_LINE   = 0x333355;
let gridXY              = null;
let gridXZ              = null;
let gridYZ              = null;
let currentGridStep     = 0; // 0 = uninitialised
let showGrid            = false;

// Return the smallest "round" step (1/2/5 × 10^n) such that ≈5 steps fit in dist/2.
function niceGridStep(dist) {
  const raw = dist / 5;
  if (raw <= 0) {
    return 1;
  }
  const exp  = Math.floor(Math.log10(raw));
  const base = Math.pow(10, exp);
  if (raw / base < 1.5) {
    return base;
  }
  if (raw / base < 3.5) {
    return 2 * base;
  }
  if (raw / base < 7.5) {
    return 5 * base;
  }
  return 10 * base;
}

// Rebuild all three GridHelpers with a new step size and make them match showGrid.
function buildGrid(step) {
  for (const g of [gridXY, gridXZ, gridYZ]) {
    if (g) {
      scene.remove(g);
      g.geometry.dispose();
      g.material.dispose();
    }
  }
  const size = step * GRID_DIVS_COUNT;
  gridXY     = new THREE.GridHelper(size, GRID_DIVS_COUNT, GRID_COLOR_CENTER, GRID_COLOR_LINE);
  gridXY.rotation.x = Math.PI / 2; // XZ → XY plane
  gridXY.visible    = showGrid;
  scene.add(gridXY);
  gridXZ         = new THREE.GridHelper(size, GRID_DIVS_COUNT, GRID_COLOR_CENTER, GRID_COLOR_LINE);
  gridXZ.visible = showGrid; // already in XZ plane
  scene.add(gridXZ);
  gridYZ = new THREE.GridHelper(size, GRID_DIVS_COUNT, GRID_COLOR_CENTER, GRID_COLOR_LINE);
  gridYZ.rotation.z = Math.PI / 2; // XZ → YZ plane
  gridYZ.visible    = showGrid;
  scene.add(gridYZ);
  currentGridStep = step;
}

// Update the grid-spacing overlay text.
function updateGridSpacingOverlay() {
  if (!showGrid || currentGridStep === 0) {
    gridSpacingEl.textContent = '';
    return;
  }
  let label;
  if (currentGridStep >= 1) {
    label = String(currentGridStep);
  } else {
    const decimals = Math.max(0, -Math.floor(Math.log10(currentGridStep)));
    label          = currentGridStep.toFixed(decimals);
  }
  gridSpacingEl.textContent = `grid: ${label} mm`;
}

// Move each grid centre to follow the camera target (snapped to step) and
// rebuild with a new step size when the camera distance changes significantly.
function updateGrid() {
  if (!showGrid) {
    return;
  }
  const target = controls.target;

  // Derive a scale metric for the grid step.
  // In perspective mode use the camera-target distance as before.
  // In orthographic mode OrbitControls zooms by adjusting orthoCamera.zoom rather
  // than moving the camera, so use the visible frustum size in world units instead.
  let scaleMetric;
  if (useOrtho && orthoCamera && typeof orthoCamera.zoom === 'number' && orthoCamera.zoom > 0) {
    const visibleHeight = (orthoCamera.top - orthoCamera.bottom) / orthoCamera.zoom;
    const visibleWidth  = (orthoCamera.right - orthoCamera.left) / orthoCamera.zoom;
    scaleMetric         = Math.max(Math.abs(visibleWidth), Math.abs(visibleHeight));
  } else {
    scaleMetric = camera.position.distanceTo(target);
  }

  const step = niceGridStep(scaleMetric);
  if (step !== currentGridStep) {
    buildGrid(step);
    updateGridSpacingOverlay();
  }
  // Snap grid centres to the nearest multiple of step so the grid tiles infinitely.
  const sx = Math.round(target.x / step) * step;
  const sy = Math.round(target.y / step) * step;
  const sz = Math.round(target.z / step) * step;
  gridXY.position.set(sx, sy, 0); // XY plane: follow x,y; fixed at z = 0
  gridXZ.position.set(sx, 0, sz); // XZ plane: follow x,z; fixed at y = 0
  gridYZ.position.set(0, sy, sz); // YZ plane: follow y,z; fixed at x = 0
}

// ── Point-cloud state ─────────────────────────────────────────────────────────
let nativeCloud   = null;
let importedCloud = null;
let showNative    = true;
let showImported  = true;

// ── Axis-view state ───────────────────────────────────────────────────────────
let activeAxis   = null; // null | 'x' | 'y' | 'z'
let axisPositive = true; // true → positive direction, false → negative

function updateAxisButtons() {
  for (const [btn, axis] of [[ btnViewX, 'x' ], [ btnViewY, 'y' ], [ btnViewZ, 'z' ]]) {
    const isActive = activeAxis === axis;
    btn.classList.toggle('active', isActive);
    const prefix    = isActive ? (axisPositive ? '+' : '−') : '';
    btn.textContent = prefix + axis.toUpperCase();
  }
}

function setAxisView(axis) {
  if (activeAxis === axis) {
    axisPositive = !axisPositive;
  } else {
    activeAxis   = axis;
    axisPositive = true;
  }
  updateAxisButtons();

  const target    = controls.target.clone();
  const activeCam = useOrtho ? orthoCamera : camera;
  const dist      = activeCam.position.distanceTo(target);
  const sign      = axisPositive ? 1 : -1;
  const pos       = target.clone();
  let up;
  if (axis === 'x') {
    pos.x += sign * dist;
    up = new THREE.Vector3(0, 1, 0);
  } else if (axis === 'y') {
    pos.y += sign * dist;
    up = new THREE.Vector3(0, 0, 1);
  } else {
    pos.z += sign * dist;
    up = new THREE.Vector3(0, 1, 0);
  }
  activeCam.position.copy(pos);
  activeCam.up.copy(up);
  controls.update();
}

// ── Grid toggle ───────────────────────────────────────────────────────────────
function toggleGrid() {
  showGrid = !showGrid;
  if (showGrid && currentGridStep === 0) {
    // Build the grid for the first time using the current scale metric.
    let scaleMetric;
    if (useOrtho && orthoCamera && typeof orthoCamera.zoom === 'number' && orthoCamera.zoom > 0) {
      const visibleHeight = (orthoCamera.top - orthoCamera.bottom) / orthoCamera.zoom;
      const visibleWidth  = (orthoCamera.right - orthoCamera.left) / orthoCamera.zoom;
      scaleMetric         = Math.max(Math.abs(visibleWidth), Math.abs(visibleHeight));
    } else {
      scaleMetric = camera.position.distanceTo(controls.target);
    }
    buildGrid(niceGridStep(scaleMetric));
  } else {
    for (const g of [gridXY, gridXZ, gridYZ]) {
      if (g) {
        g.visible = showGrid;
      }
    }
  }
  btnGrid.classList.toggle('active', showGrid);
  updateGridSpacingOverlay();
}

// ── Projection toggle ─────────────────────────────────────────────────────────
function syncOrthoFrustum() {
  // Derive the orthographic frustum size from the current perspective camera and target.
  const target       = controls.target.clone();
  const dist         = camera.position.distanceTo(target);
  const fovRad       = camera.fov * Math.PI / 180;
  const halfH        = Math.tan(fovRad / 2) * dist;
  const aspect       = container.clientWidth / container.clientHeight;
  orthoCamera.left   = -halfH * aspect;
  orthoCamera.right  = halfH * aspect;
  orthoCamera.top    = halfH;
  orthoCamera.bottom = -halfH;
  orthoCamera.updateProjectionMatrix();
}

function toggleProjection() {
  if (!useOrtho) {
    // Perspective → orthographic
    syncOrthoFrustum();
    orthoCamera.position.copy(camera.position);
    orthoCamera.up.copy(camera.up);
    useOrtho        = true;
    controls.object = orthoCamera;
  } else {
    // Orthographic → perspective
    camera.position.copy(orthoCamera.position);
    camera.up.copy(orthoCamera.up);
    useOrtho        = false;
    controls.object = camera;
    camera.aspect   = container.clientWidth / container.clientHeight;
    camera.updateProjectionMatrix();
  }
  controls.update();
  btnOrtho.classList.toggle('active', useOrtho);
  btnOrtho.textContent = useOrtho ? 'Ortho' : 'Persp';
}

function makeCloud(points, color) {
  if (points.length === 0) {
    return null;
  }
  const positions = new Float32Array(points.length * 3);
  for (let i = 0; i < points.length; i++) {
    positions[i * 3]     = points[i][0];
    positions[i * 3 + 1] = points[i][1];
    positions[i * 3 + 2] = points[i][2];
  }
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  const mat = new THREE.PointsMaterial({color, size : 1.5, sizeAttenuation : false});
  return new THREE.Points(geo, mat);
}

function clearClouds() {
  if (nativeCloud) {
    scene.remove(nativeCloud);
    nativeCloud.geometry.dispose();
    nativeCloud.material.dispose();
    nativeCloud = null;
  }
  if (importedCloud) {
    scene.remove(importedCloud);
    importedCloud.geometry.dispose();
    importedCloud.material.dispose();
    importedCloud = null;
  }
}

function escHtml(s) {
  return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function fixtureIdFromUrl() {
  // Check URL query string first (used when linked via docsify-routed bench report)
  const searchFixture = new URLSearchParams(window.location.search).get('fixture');
  if (searchFixture) {
    return searchFixture;
  }
  // Fall back to hash fragment (canonical form after first load)
  const hash =
      window.location.hash.startsWith('#') ? window.location.hash.slice(1) : window.location.hash;
  if (!hash) {
    return '';
  }
  const params  = new URLSearchParams(hash);
  const fixture = params.get('fixture');
  return fixture ? fixture : decodeURIComponent(hash);
}

function setHashForFixture(fixtureId) {
  const nextHash = fixtureId ? `#fixture=${encodeURIComponent(fixtureId)}` : '';
  // Also update when a query string is present: on first load the fixture may be
  // supplied via ?fixture=id (from the bench report link), and we need to migrate
  // that to the canonical #fixture=id form by clearing the search string.
  if (window.location.hash === nextHash && !window.location.search) {
    return;
  }
  // Drop query string; hash is the canonical way to deep-link within the viewer.
  const nextUrl = `${window.location.pathname}${nextHash}`;
  window.history.replaceState(null, '', nextUrl);
}

// Apply a small displacement to the imported cloud so that both point clouds
// remain visually distinct when they are in perfect positional agreement.
// The offset is only active while both clouds are simultaneously visible; when
// either cloud is hidden the imported cloud is shown at its true position.
function updateImportedOffset() {
  if (!importedCloud) {
    return;
  }
  if (showNative && showImported && nativeCloud) {
    const box      = new THREE.Box3().setFromObject(nativeCloud);
    const size     = box.getSize(new THREE.Vector3());
    const maxDim   = Math.max(size.x, size.y, size.z, 1e-3);
    const fraction = parseFloat(offsetSlider.value) / 100.0;
    const offset   = maxDim * fraction;
    importedCloud.position.set(offset, offset, offset);
  } else {
    importedCloud.position.set(0, 0, 0);
  }
}

// Show fixture metadata immediately from the metadata-only object supplied by ALL_FIXTURES.
// Hit-count cells are filled with a placeholder that is replaced by loadFixture() once the
// full blob has been decoded.  This ensures the sidebar is never empty while loading.
function showFixtureMeta(meta) {
  if (!meta) {
    return;
  }
  statsEl.innerHTML = `
    <tr><td>Fixture</td><td>${escHtml(meta.fixture_id)}</td></tr>
    <tr><td>Class</td><td>${escHtml(meta.geant4_class)}</td></tr>
    <tr><td>Rays fired</td><td>${escHtml(String(meta.ray_count))}</td></tr>
    <tr><td>Native hits</td><td id="stat-native-hits">…</td></tr>
    <tr><td>Imported hits</td><td id="stat-imported-hits">…</td></tr>
    <tr><td>Native origin</td><td>${
      escHtml(meta.native_pre_step_origin.map(v => v.toFixed(2)).join(', '))}</td></tr>
    <tr><td>Imported origin</td><td>${
      escHtml(meta.imported_pre_step_origin.map(v => v.toFixed(2)).join(', '))}</td></tr>
  `;
  countEl.textContent = '';
}

function loadFixture(fixture) {
  clearClouds();
  // Reset axis-view state and restore default camera up vectors so that
  // OrbitControls is not left operating with a non-standard up after a prior
  // setAxisView() call (e.g. Y-axis view sets up to Z).
  activeAxis   = null;
  axisPositive = true;
  camera.up.set(0, 1, 0);
  orthoCamera.up.set(0, 1, 0);
  updateAxisButtons();
  if (!fixture) {
    return;
  }

  nativeCloud   = makeCloud(fixture.native_post_step_hits, 0x4a90d9);
  importedCloud = makeCloud(fixture.imported_post_step_hits, 0xe87040);
  if (nativeCloud) {
    nativeCloud.visible = showNative;
    scene.add(nativeCloud);
  }
  if (importedCloud) {
    importedCloud.visible = showImported;
    scene.add(importedCloud);
  }
  updateImportedOffset();

  // Fit camera to the bounding box of visible hits.  Prefer a visible cloud so
  // the camera doesn't move based on a currently-hidden dataset.
  let cloudForBounds = null;
  if (showNative && nativeCloud) {
    cloudForBounds = nativeCloud;
  } else if (showImported && importedCloud) {
    cloudForBounds = importedCloud;
  } else {
    cloudForBounds = nativeCloud || importedCloud;
  }
  if (cloudForBounds) {
    const box    = new THREE.Box3().setFromObject(cloudForBounds);
    const center = new THREE.Vector3();
    box.getCenter(center);
    const size   = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z, 1e-3);
    const newPos =
        center.clone().addScaledVector(new THREE.Vector3(1, 0.8, 1).normalize(), maxDim * 2.0);
    camera.position.copy(newPos);
    if (useOrtho) {
      orthoCamera.position.copy(newPos);
      syncOrthoFrustum();
    }
    controls.target.copy(center);
    controls.update();
  }

  const nh = fixture.native_post_step_hits.length;
  const ih = fixture.imported_post_step_hits.length;
  // Update hit-count cells in place when showFixtureMeta() pre-populated the table,
  // or write the full table when called without a preceding showFixtureMeta().
  const nativeHitsEl   = document.getElementById('stat-native-hits');
  const importedHitsEl = document.getElementById('stat-imported-hits');
  if (nativeHitsEl && importedHitsEl) {
    nativeHitsEl.textContent   = nh;
    importedHitsEl.textContent = ih;
  } else {
    statsEl.innerHTML = `
      <tr><td>Fixture</td><td>${escHtml(fixture.fixture_id)}</td></tr>
      <tr><td>Class</td><td>${escHtml(fixture.geant4_class)}</td></tr>
      <tr><td>Rays fired</td><td>${escHtml(String(fixture.ray_count))}</td></tr>
      <tr><td>Native hits</td><td>${nh}</td></tr>
      <tr><td>Imported hits</td><td>${ih}</td></tr>
      <tr><td>Native origin</td><td>${
        escHtml(fixture.native_pre_step_origin.map(v => v.toFixed(2)).join(', '))}</td></tr>
      <tr><td>Imported origin</td><td>${
        escHtml(fixture.imported_pre_step_origin.map(v => v.toFixed(2)).join(', '))}</td></tr>
    `;
  }
  countEl.textContent = `native: ${nh} pts  |  imported: ${ih} pts`;
}

// ── Lazy loading ──────────────────────────────────────────────────────────────
// Decode the base64-encoded gzip blob for a fixture and return the full data
// (including point arrays) as a parsed object.  The browser's DecompressionStream
// API (gzip) is used so no third-party library is required.  Blob.stream() is
// piped through the DecompressionStream so the pipe mechanism drives backpressure
// correctly — writing to the writable side and reading from the readable side happen
// concurrently, which avoids the deadlock that would arise if all compressed bytes
// were written before anyone started consuming the decompressed output.
// Results are cached so repeated selections of the same fixture do not
// re-decompress or re-parse the data.
const fixtureCache = new Map();

async function decodeFixtureBlob(fixtureId) {
  if (fixtureCache.has(fixtureId)) {
    return fixtureCache.get(fixtureId);
  }
  if (!(fixtureId in FIXTURE_BLOBS)) {
    return null;
  }
  const bytes = Uint8Array.from(atob(FIXTURE_BLOBS[fixtureId]), c => c.charCodeAt(0));
  const text =
      await new Response(new Blob([ bytes ]).stream().pipeThrough(new DecompressionStream('gzip')))
          .text();
  const data = JSON.parse(text);
  fixtureCache.set(fixtureId, data);
  return data;
}

// Monotonically-increasing load token used to discard stale async results when
// the user changes the selection before a previous decode has completed.
let currentLoadToken = 0;

// Select a fixture by id: decode its blob lazily (with caching), then render.
// If a newer selection arrives before this one completes the result is discarded.
// Decode failures are surfaced in the stats panel so the user gets feedback.
async function selectFixtureById(fixtureId) {
  const token = ++currentLoadToken;
  // Show metadata immediately from ALL_FIXTURES so the sidebar is never blank while
  // the (potentially slow) blob decode is in progress.
  const meta = ALL_FIXTURES.find(x => x.fixture_id === fixtureId);
  if (meta) {
    showFixtureMeta(meta);
  }
  try {
    const data = await decodeFixtureBlob(fixtureId);
    if (token !== currentLoadToken) {
      return; // a newer selection superseded this one
    }
    if (!data) {
      // Blob not found: clear any stale point clouds, show "—" for hit counts.
      clearClouds();
      const nativeHitsEl   = document.getElementById('stat-native-hits');
      const importedHitsEl = document.getElementById('stat-imported-hits');
      if (nativeHitsEl)
        nativeHitsEl.textContent = '—';
      if (importedHitsEl)
        importedHitsEl.textContent = '—';
      countEl.textContent = '';
      return;
    }
    loadFixture(data);
  } catch (err) {
    if (token !== currentLoadToken) {
      return; // stale — discard silently
    }
    clearClouds();
    const nativeHitsEl   = document.getElementById('stat-native-hits');
    const importedHitsEl = document.getElementById('stat-imported-hits');
    if (nativeHitsEl && importedHitsEl) {
      // Pre-populated table is present: show error inline without clobbering the metadata rows.
      nativeHitsEl.textContent   = 'Error';
      importedHitsEl.textContent = 'Error';
      // Append a row with the full error reason so the user knows why loading failed.
      const errRow     = statsEl.insertRow();
      errRow.innerHTML = `<td colspan="2" style="color:#e87040">${escHtml(String(err))}</td>`;
    } else {
      statsEl.innerHTML = `<tr><td colspan="2" style="color:#e87040">Error loading fixture: ${
          escHtml(String(err))}</td></tr>`;
    }
    countEl.textContent = '';
    console.error('Failed to load fixture:', err);
  }
}

// ── Populate dropdown ─────────────────────────────────────────────────────────
function populateSelect() {
  if (ALL_FIXTURES.length === 0) {
    emptyMsg.style.display = 'block';
    return;
  }
  const families = {};
  for (const f of ALL_FIXTURES) {
    const family = f.fixture_id.split('/')[0] || 'other';
    if (!families[family]) {
      families[family] = [];
    }
    families[family].push(f);
  }
  for (const [family, fixtures] of Object.entries(families)) {
    const group = document.createElement('optgroup');
    group.label = family;
    for (const f of fixtures) {
      const opt       = document.createElement('option');
      opt.value       = f.fixture_id;
      opt.textContent = f.fixture_id.split('/').slice(1).join('/') + ' (' + f.geant4_class + ')';
      group.appendChild(opt);
    }
    selectEl.appendChild(group);
  }
}

populateSelect();
if (ALL_FIXTURES.length > 0) {
  const requestedFixture = fixtureIdFromUrl();
  const initialMeta = ALL_FIXTURES.find(x => x.fixture_id === requestedFixture) || ALL_FIXTURES[0];
  selectEl.value    = initialMeta.fixture_id;
  setHashForFixture(initialMeta.fixture_id);
  selectFixtureById(initialMeta.fixture_id)
      .catch(err => console.error('Failed to load fixture:', err));
}

selectEl.addEventListener('change', () => {
  const fixtureId = selectEl.value;
  const meta      = ALL_FIXTURES.find(x => x.fixture_id === fixtureId);
  if (meta) {
    setHashForFixture(meta.fixture_id);
    selectFixtureById(meta.fixture_id).catch(err => console.error('Failed to load fixture:', err));
  } else {
    loadFixture(null);
  }
});

window.addEventListener('hashchange', () => {
  const fixtureId = fixtureIdFromUrl();
  if (!fixtureId || fixtureId === selectEl.value) {
    return;
  }
  const meta = ALL_FIXTURES.find(x => x.fixture_id === fixtureId);
  if (!meta) {
    return;
  }
  selectEl.value = meta.fixture_id;
  selectFixtureById(meta.fixture_id).catch(err => console.error('Failed to load fixture:', err));
});

// ── Toggle buttons ────────────────────────────────────────────────────────────
btnNative.addEventListener('click', () => {
  showNative = !showNative;
  btnNative.classList.toggle('off', !showNative);
  if (nativeCloud) {
    nativeCloud.visible = showNative;
  }
  updateImportedOffset();
});
btnImp.addEventListener('click', () => {
  showImported = !showImported;
  btnImp.classList.toggle('off', !showImported);
  if (importedCloud) {
    importedCloud.visible = showImported;
  }
  updateImportedOffset();
});

// ── View-control buttons ──────────────────────────────────────────────────────
btnViewX.addEventListener('click', () => setAxisView('x'));
btnViewY.addEventListener('click', () => setAxisView('y'));
btnViewZ.addEventListener('click', () => setAxisView('z'));
btnGrid.addEventListener('click', toggleGrid);
btnOrtho.addEventListener('click', toggleProjection);

// Clear the active axis highlight when the user manually rotates/pans/zooms
// so the buttons don't show a stale snapped state after free-form interaction.
controls.addEventListener('start', () => {
  if (activeAxis !== null) {
    activeAxis = null;
    updateAxisButtons();
  }
});

offsetSlider.addEventListener('input', () => {
  offsetValue.textContent = `${parseFloat(offsetSlider.value).toFixed(1)}%`;
  updateImportedOffset();
});

// ── Sidebar toggle ────────────────────────────────────────────────────────────
// Use a single persistent transitionend/transitioncancel handler so rapid
// toggles cannot accumulate multiple once-listeners that never fire.
sidebarEl.addEventListener('transitionend', onResize);
sidebarEl.addEventListener('transitioncancel', onResize);

sidebarToggle.addEventListener('click', () => {
  const hidden              = sidebarEl.classList.toggle('hidden');
  sidebarToggle.textContent = hidden ? '▶' : '◀';
  sidebarToggle.title       = hidden ? 'Show sidebar' : 'Hide sidebar';
  sidebarToggle.setAttribute('aria-label', hidden ? 'Show sidebar' : 'Hide sidebar');
  sidebarToggle.setAttribute('aria-expanded', String(!hidden));
  // Remove hidden sidebar from the tab order and hide from assistive tech.
  if (hidden) {
    sidebarEl.inert = true;
    sidebarEl.setAttribute('aria-hidden', 'true');
  } else {
    sidebarEl.inert = false;
    sidebarEl.removeAttribute('aria-hidden');
  }
});

// ── Resize handler ────────────────────────────────────────────────────────────
function onResize() {
  const w       = container.clientWidth;
  const h       = container.clientHeight;
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
  if (useOrtho) {
    // Preserve the vertical extent; only adjust horizontal to new aspect.
    const aspect      = w / h;
    const halfH       = orthoCamera.top;
    orthoCamera.left  = -halfH * aspect;
    orthoCamera.right = halfH * aspect;
    orthoCamera.updateProjectionMatrix();
  }
  renderer.setSize(w, h);
}
window.addEventListener('resize', onResize);
onResize();

// ── Render loop ───────────────────────────────────────────────────────────────
function animate() {
  requestAnimationFrame(animate);
  controls.update();
  updateGrid();
  renderer.render(scene, useOrtho ? orthoCamera : camera);
}
animate();
