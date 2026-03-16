// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2024 G4OCCT Contributors

// Fixture data is supplied by the HTML page as an inline JSON script element so
// that this file remains a static asset with no runtime replacements.
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';

const ALL_FIXTURES = JSON.parse(document.getElementById('fixture-data').textContent);

// ── UI elements ──────────────────────────────────────────────────────────────
const selectEl      = document.getElementById('fixture-select');
const btnNative     = document.getElementById('btn-native');
const btnImp        = document.getElementById('btn-imported');
const statsEl       = document.getElementById('stats-body');
const countEl       = document.getElementById('count-overlay');
const emptyMsg      = document.getElementById('empty-msg');
const sidebarEl     = document.getElementById('sidebar');
const sidebarToggle = document.getElementById('sidebar-toggle');

// ── Three.js setup ────────────────────────────────────────────────────────────
const container = document.getElementById('canvas-container');
const renderer  = new THREE.WebGLRenderer({antialias : true});
renderer.setPixelRatio(window.devicePixelRatio);
renderer.setClearColor(0x1a1a2e);
container.appendChild(renderer.domElement);

const scene  = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(60, 1, 0.001, 1e6);
camera.position.set(0, 0, 200);

const controls              = new OrbitControls(camera, renderer.domElement);
controls.enableDamping      = true;
controls.dampingFactor      = 0.08;
controls.screenSpacePanning = true;

// Axes helper — always visible to provide orientation reference
scene.add(new THREE.AxesHelper(50));

// ── Point-cloud state ─────────────────────────────────────────────────────────
let nativeCloud   = null;
let importedCloud = null;
let showNative    = true;
let showImported  = true;

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

function fixtureIdFromHash() {
  const hash = window.location.hash.startsWith('#') ? window.location.hash.slice(1)
                                                    : window.location.hash;
  if (!hash) {
    return '';
  }
  const params  = new URLSearchParams(hash);
  const fixture = params.get('fixture');
  return fixture ? fixture : decodeURIComponent(hash);
}

function setHashForFixture(fixtureId) {
  const nextHash = fixtureId ? `#fixture=${encodeURIComponent(fixtureId)}` : '';
  if (window.location.hash === nextHash) {
    return;
  }
  const nextUrl = `${window.location.pathname}${window.location.search}${nextHash}`;
  window.history.replaceState(null, '', nextUrl);
}

// Apply a small displacement to the imported cloud so that both point clouds
// remain visually distinct when they are in perfect positional agreement.
// The offset is only active while both clouds are simultaneously visible; when
// the native cloud is hidden the imported cloud is shown at its true position.
function updateImportedOffset() {
  if (!importedCloud) {
    return;
  }
  if (showNative && nativeCloud) {
    const box    = new THREE.Box3().setFromObject(nativeCloud);
    const size   = box.getSize(new THREE.Vector3());
    const maxDim = Math.max(size.x, size.y, size.z, 1e-3);
    const offset = maxDim * 0.01;
    importedCloud.position.set(offset, offset, offset);
  } else {
    importedCloud.position.set(0, 0, 0);
  }
}

function loadFixture(fixture) {
  clearClouds();
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
    camera.position.copy(center).addScaledVector(new THREE.Vector3(1, 0.8, 1).normalize(),
                                                 maxDim * 2.0);
    controls.target.copy(center);
    controls.update();
  }

  const nh = fixture.native_post_step_hits.length;
  const ih = fixture.imported_post_step_hits.length;
  statsEl.innerHTML = `
    <tr><td>Fixture</td><td>${escHtml(fixture.fixture_id)}</td></tr>
    <tr><td>Class</td><td>${escHtml(fixture.geant4_class)}</td></tr>
    <tr><td>Rays fired</td><td>${fixture.ray_count}</td></tr>
    <tr><td>Native hits</td><td>${nh}</td></tr>
    <tr><td>Imported hits</td><td>${ih}</td></tr>
    <tr><td>Native origin</td><td>${
      fixture.native_pre_step_origin.map(v => v.toFixed(2)).join(', ')}</td></tr>
    <tr><td>Imported origin</td><td>${
      fixture.imported_pre_step_origin.map(v => v.toFixed(2)).join(', ')}</td></tr>
  `;
  countEl.textContent = `native: ${nh} pts  |  imported: ${ih} pts`;
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
  const requestedFixture = fixtureIdFromHash();
  const initialFixture   = ALL_FIXTURES.find(x => x.fixture_id === requestedFixture) || ALL_FIXTURES[0];
  selectEl.value         = initialFixture.fixture_id;
  loadFixture(initialFixture);
  setHashForFixture(initialFixture.fixture_id);
}

selectEl.addEventListener('change', () => {
  const f = ALL_FIXTURES.find(x => x.fixture_id === selectEl.value);
  loadFixture(f || null);
  if (f) {
    setHashForFixture(f.fixture_id);
  }
});

window.addEventListener('hashchange', () => {
  const fixtureId = fixtureIdFromHash();
  if (!fixtureId || fixtureId === selectEl.value) {
    return;
  }
  const fixture = ALL_FIXTURES.find(x => x.fixture_id === fixtureId);
  if (!fixture) {
    return;
  }
  selectEl.value = fixture.fixture_id;
  loadFixture(fixture);
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
