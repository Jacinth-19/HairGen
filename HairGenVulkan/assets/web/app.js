/* app.js - drives the native renderer through its little HTTP API:
 *
 *   POST /params {name: value, ...}   update the scene
 *   GET  /stream                      MJPEG preview (rendered by the GPU)
 *   GET  /snapshot.png                one PNG frame
 *
 * The control table below mirrors InitParam() in the original hair-mesh demo
 * (same groups, labels and ranges), so the panel feels like the web version
 * while every pixel is produced by the Vulkan renderer.
 */
'use strict';

/* ---------------------------------------------------------------- groups */

const GROUPS = [
  {
    title: 'Hair',
    params: [
      { id: 'density', name: 'shadows', min: 0.1, max: 100, step: 0.1, value: 2.5, log: true },
      { id: 'lengthVar', name: 'length var.', min: 0, max: 1, step: 0.01, value: 0.1 },
      { id: 'widthPx', name: 'width', min: 0.5, max: 4, step: 0.1, value: 1.9 },
    ],
  },
  {
    title: 'Fuzz',
    params: [
      { id: 'fuzz', name: 'amount', min: 0, max: 0.2, step: 0.005, value: 0.06 },
      { id: 'fuzzDistrib', name: 'distribution', min: 0, max: 1, step: 0.01, value: 0.8 },
      { id: 'fuzzExp', name: 'exp.', min: 0, max: 4, step: 0.05, value: 0 },
    ],
  },
  {
    title: 'Frizz',
    params: [
      { id: 'frizz', name: 'amount', min: 0, max: 0.2, step: 0.002, value: 0.02 },
      { id: 'frizzFreq', name: 'frequency', min: 0, max: 4, step: 0.01, value: 1 },
      { id: 'frizzExp', name: 'exp.', min: 0, max: 4, step: 0.05, value: 0 },
    ],
  },
  {
    title: 'Kink',
    params: [
      { id: 'kink', name: 'amount', min: 0, max: 0.2, step: 0.002, value: 0.02 },
      { id: 'kinkFreqUV', name: 'uv frequency', min: 0, max: 4, step: 0.01, value: 2 },
      { id: 'kinkFreqW', name: 'w frequency', min: 0, max: 2, step: 0.01, value: 1 },
      { id: 'kinkStretch', name: 'stretch', min: 0, max: 1, step: 0.01, value: 0.5 },
      { id: 'kinkExp', name: 'exp.', min: 0, max: 4, step: 0.05, value: 0 },
    ],
  },
  {
    title: 'Clump',
    params: [
      { id: 'clump', name: 'percentage', min: 0, max: 1, step: 0.01, value: 0.9 },
      { id: 'clumpSize', name: 'size', min: 0, max: 0.3, step: 0.005, value: 0 },
      { id: 'clumpNoise', name: 'noise', min: 0, max: 1, step: 0.01, value: 1 },
      { id: 'clumpFreq', name: 'frequency', min: 0, max: 4, step: 0.01, value: 1 },
      { id: 'clumpVar', name: 'variation', min: 0, max: 1, step: 0.01, value: 0.1 },
      { id: 'clumpExp', name: 'exp.', min: 0, max: 4, step: 0.05, value: 0 },
    ],
  },
  {
    title: 'Curl',
    params: [
      { id: 'curl', name: 'amount', min: 0, max: 0.2, step: 0.002, value: 0.02 },
      { id: 'curlFreq', name: 'frequency', min: 0, max: 2, step: 0.01, value: 1 },
      { id: 'curlTwists', name: 'twists', min: 0, max: 20, step: 0.1, value: 10 },
      { id: 'curlTwistExp', name: 'twist \u2194', min: 0, max: 1, step: 0.01, value: 0.3 },
      { id: 'curlExp', name: 'exp.', min: 0, max: 4, step: 0.05, value: 0 },
    ],
  },
];

/* --------------------------------------------------------------- plumbing */

const state = {};
const outputs = {};

/** Slider value -> parameter value (log sliders are geometric). */
function toParam(p, s) {
  return p.log ? Math.pow(10, s) : s;
}
/** Parameter value -> slider position. */
function toSlider(p, v) {
  return p.log ? Math.log10(v) : v;
}

function buildPanel() {
  const host = document.getElementById('cGroups');
  for (const group of GROUPS) {
    const title = document.createElement('div');
    title.className = 'cGroupTitle';
    title.textContent = group.title;
    host.appendChild(title);

    for (const p of group.params) {
      // Log sliders live in log10 space between their min/max.
      const range = p.log
        ? { min: Math.log10(p.min), max: Math.log10(p.max), step: 0.01 }
        : { min: p.min, max: p.max, step: p.step };

      const row = document.createElement('div');
      row.className = 'cRow';

      const name = document.createElement('span');
      name.className = 'cName';
      name.textContent = p.name;

      const field = document.createElement('span');
      field.className = 'cField';

      const input = document.createElement('input');
      input.className = 'cSInput';
      input.type = 'range';
      input.min = range.min;
      input.max = range.max;
      input.step = range.step;
      input.id = p.id;
      input.value = toSlider(p, p.value);

      const out = document.createElement('output');

      input.addEventListener('input', () => {
        const v = toParam(p, parseFloat(input.value));
        state[p.id] = v;
        out.textContent = formatParam(p, v);
        queueUpdate();
      });

      state[p.id] = p.value;
      out.textContent = formatParam(p, p.value);
      outputs[p.id] = out;

      field.appendChild(input);
      field.appendChild(out);
      row.appendChild(name);
      row.appendChild(field);
      host.appendChild(row);
    }
  }
}

function formatParam(p, v) {
  if (p.log) return v >= 10 ? v.toFixed(0) : v.toFixed(1);
  if (Math.abs(v) >= 1) return v.toFixed(v % 1 === 0 ? 0 : 1);
  return v.toFixed(v < 0.01 ? 3 : 2);
}

/* --------------------------------------------------------- event batching */

let pending = null;
let inflight = false;

/** Coalesce slider motion into ~15 Hz parameter posts. */
function queueUpdate() {
  if (pending) return;
  pending = setTimeout(flush, 66);
}

function flush() {
  pending = null;
  if (inflight) {
    // Try again once the previous request finished.
    pending = setTimeout(flush, 80);
    return;
  }
  const body = JSON.stringify(state);
  inflight = true;
  fetch('/params', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body })
    .then(() => { inflight = false; })
    .catch(() => {
      inflight = false;
      document.getElementById('cStatus').textContent = 'renderer unreachable';
    });
}

/* --------------------------------------------------------------- controls */

function wireSimpleInputs() {
  const ids = ['sunRot', 'sunTilt', 'sunIntensity', 'exposure', 'bloom', 'clouds',
               'vignette', 'turbidity', 'rayleigh', 'mie',
               'rain', 'wetness', 'puddles', 'lensDrops', 'wind', 'windDir', 'gravity',
               'rot', 'tilt', 'dist',
               // hair dye
               'dyeStrength', 'rootDarken', 'colorVar'];
  for (const id of ids) {
    const input = document.getElementById(id);
    if (!input) continue;
    const out = input.parentElement.querySelector('output');
    const sync = () => {
      state[id] = parseFloat(input.value);
      if (out) out.textContent = parseFloat(input.value).toFixed(2);
      queueUpdate();
    };
    input.addEventListener('input', sync);
    sync();
  }

  for (const id of ['showHead', 'showHair', 'showGround', 'rootSamples', 'collide', 'showHairMesh']) {
    const box = document.getElementById(id);
    if (!box) continue;
    const sync = () => {
      state[id] = box.checked;
      queueUpdate();
    };
    box.addEventListener('change', sync);
  }

  const mode = document.getElementById('renderMode');
  if (mode) {
    mode.addEventListener('change', () => {
      state.renderMode = parseInt(mode.value, 10);
      queueUpdate();
    });
    state.renderMode = parseInt(mode.value, 10);
  }

  // ---- dye: colour input + preset select post the hex string directly
  const dye = document.getElementById('dye');
  const dyePreset = document.getElementById('dyePreset');
  if (dye) {
    dye.addEventListener('input', () => { state.dye = dye.value; queueUpdate(); });
    state.dye = dye.value;
  }
  if (dyePreset) {
    dyePreset.addEventListener('change', () => {
      if (!dyePreset.value) return;
      if (dye) dye.value = dyePreset.value;
      state.dye = dyePreset.value;
      queueUpdate();
    });
  }

  // ---- hairstyle presets (procedural operator stacks)
  const hairstyle = document.getElementById('hairstyle');
  if (hairstyle) {
    hairstyle.addEventListener('change', () => {
      if (hairstyle.value) state.style = hairstyle.value;
      queueUpdate();
    });
  }

  const style = document.getElementById('hairColor');
  if (style) {
    style.addEventListener('change', () => {
      state.style = style.value;
      // rosa ships its own Marschner lookup tables; everything else reuses the
      // demo's std_blond set and only changes the absorption preset.
      // rosa (the default) has its own LUT set; the blond presets share one.
      state.material = (style.value === 'rosa' || style.value === 'brown') ? 1 : 0;
      queueUpdate();
    });
    state.style = style.value;
  }

  document.getElementById('bSnapshot').addEventListener('click', () => {
    const a = document.createElement('a');
    a.href = '/snapshot.png?t=' + Date.now();
    a.download = 'hairgen.png';
    a.click();
  });

  document.getElementById('bReset').addEventListener('click', () => location.reload());

  // Weather presets: one click to a rainy set, mirrored exactly by the CLI's
  // --preset flag so a still and the live view match.
  const weatherPresets = {
    bDry: { rain: 0, wetness: 0, 'lensDrops': 0, wind: 0.15, clouds: 0.15 },
    bRain: { rain: 0.65, wetness: 0.85, 'lensDrops': 0.55, wind: 0.55, clouds: 0.75 },
    bStorm: { rain: 1, wetness: 1, 'lensDrops': 0.9, wind: 1.0, clouds: 1.0,
              turbidity: 5.0 },
  };
  for (const [id, preset] of Object.entries(weatherPresets)) {
    const btn = document.getElementById(id);
    if (!btn) continue;
    btn.addEventListener('click', () => {
      for (const [key, value] of Object.entries(preset)) {
        const input = document.getElementById(key);
        if (!input) continue;
        input.value = value;
        input.dispatchEvent(new Event('input'));
      }
    });
  }
}

/* --------------------------------------------- orbit / zoom on the preview */

function wireOrbit() {
  const img = document.getElementById('cCanvas');
  let dragging = false, lastX = 0, lastY = 0;

  img.addEventListener('pointerdown', (e) => {
    dragging = true;
    lastX = e.clientX;
    lastY = e.clientY;
    img.setPointerCapture(e.pointerId);
  });
  img.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const rot = document.getElementById('rot');
    const tilt = document.getElementById('tilt');
    rot.value = clamp(parseFloat(rot.value) - (e.clientX - lastX) * 0.008,
                      parseFloat(rot.min), parseFloat(rot.max));
    tilt.value = clamp(parseFloat(tilt.value) + (e.clientY - lastY) * 0.004,
                       parseFloat(tilt.min), parseFloat(tilt.max));
    lastX = e.clientX;
    lastY = e.clientY;
    rot.dispatchEvent(new Event('input'));
    tilt.dispatchEvent(new Event('input'));
  });
  img.addEventListener('pointerup', () => { dragging = false; });
  img.addEventListener('wheel', (e) => {
    e.preventDefault();
    const dist = document.getElementById('dist');
    dist.value = clamp(parseFloat(dist.value) + e.deltaY * 0.002,
                       parseFloat(dist.min), parseFloat(dist.max));
    dist.dispatchEvent(new Event('input'));
  }, { passive: false });
}

function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }

/* ------------------------------------------------------------------- boot */

function startStream() {
  const img = document.getElementById('cCanvas');
  const status = document.getElementById('cStatus');
  // The MJPEG feed is an infinite multipart response; a plain <img> renders it.
  img.addEventListener('load', () => {
    status.textContent = 'live';
  });
  img.src = '/stream?t=' + Date.now();
  setTimeout(() => {
    if (img.naturalWidth === 0) status.textContent = 'renderer unreachable';
  }, 8000);
}

buildPanel();
wireSimpleInputs();
wireOrbit();
// Send the initial state so the renderer matches the panel, then start preview.
flush();
startStream();
