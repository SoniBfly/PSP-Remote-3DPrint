import './style.css'
import { createSlicer } from 'three-slicer'
import {
  applyPreset,
  deriveKernelParams,
  filamentPresets,
  presetOptionKeys,
  printerDefaultPreset,
  printerSettings,
  processPresets,
} from 'three-slicer/settings'
import { Viewport3D } from './viewport3d.js'

const CV_W = 240
const CV_H = 200
const MAX_GCODE = 4 * 1024 * 1024
const MACHINE = 'FLSun QQ-S Pro 0.4 nozzle'
const FILAMENT = 'FLSun Generic PLA'

const FILL_PATTERNS = [
  'gyroid',
  'grid',
  'honeycomb',
  'cubic',
  'triangles',
  'crosshatch',
  'lightning',
  'rectilinear',
  'concentric',
  '3dhoneycomb',
  'adaptivecubic',
  'hilbertcurve',
]

const SUPPORT_TYPES = [
  'normal(auto)',
  'tree(auto)',
  'normal(manual)',
  'tree(manual)',
]

const SUPPORT_STYLES = [
  'default',
  'grid',
  'snug',
  'organic',
  'tree_slim',
  'tree_strong',
  'tree_hybrid',
]

const app = document.getElementById('app')
app.innerHTML = `
  <section id="view-lib">
    <header class="top">
      <h1>FLSUN — Models</h1>
      <button type="button" id="libRefresh" class="sec sm">Refresh</button>
    </header>
    <p class="hint">Models already on the PSP inbox. Tap <strong>+</strong> to add STL (slice) or G-code.
    FLPV thumbnails are for the library and phone Preview — not shown while uploading.</p>
    <div id="libGrid" class="lib-grid" aria-label="Inbox models"></div>
    <pre id="logLib">loading inbox…</pre>
  </section>

  <section id="view-add" class="hide">
    <header class="top">
      <button type="button" id="backLib" class="sec sm">← Models</button>
      <h1>Add model</h1>
    </header>
    <label class="drop" id="dropZone">
      <span>Drop <code>.stl</code> or <code>.gcode</code> here, or choose a file</span>
      <input type="file" id="addFile" accept=".stl,.gcode,.gco,.g">
    </label>
    <div id="fileKind" class="meta">no file</div>

    <div id="workArea" class="hide">
      <div id="view3d" class="viewport" aria-label="3D viewport"></div>
      <div class="view-controls">
        <label class="layer">Layer
          <input type="range" id="vLayer" min="0" max="0" value="0" disabled>
          <span id="vLayerLbl">—</span>
        </label>
        <label class="chk"><input type="checkbox" id="vTravel"> travels</label>
        <label class="chk"><input type="checkbox" id="vMesh" checked> mesh</label>
        <div id="supToggle" class="seg hide" role="group" aria-label="Supports preview">
          <button type="button" class="seg-btn" data-sup="no">no supports</button>
          <button type="button" class="seg-btn on" data-sup="yes">with supports</button>
        </div>
      </div>

      <section id="panel-stl" class="panel hide">
        <div class="grid">
          <label>Scale % <input type="number" id="scale" value="100" min="5" max="400" step="1"></label>
          <label>Process <select id="process"></select></label>
          <label>Infill % <input type="number" id="infill" value="15" min="0" max="100" step="1"></label>
          <label>Fill pattern <select id="pattern"></select></label>
          <label>Layer mm <input type="number" id="layer" value="0.2" min="0.08" max="0.4" step="0.02"></label>
          <label>Nozzle °C <input type="number" id="nozzleTemp" value="220" min="150" max="300" step="5"></label>
          <label>Bed °C <input type="number" id="bedTemp" value="45" min="0" max="110" step="5"></label>
          <label class="chk wide"><input type="checkbox" id="supports" checked> Supports (print gcode)</label>
          <label>Support type <select id="supType"></select></label>
          <label>Support style <select id="supStyle"></select></label>
          <label>Overhang ° <input type="number" id="supAngle" value="30" min="0" max="90" step="1"></label>
        </div>
        <p class="row">
          <button id="slice" disabled>Slice</button>
          <button id="sendStl" class="sec" disabled>Send to inbox</button>
        </p>
        <div id="stlMeta" class="meta">no STL</div>
      </section>

      <section id="panel-gcode" class="panel hide">
        <p class="hint tight">G-code: FLPV for the phone is built offscreen (not shown here). 3D toolpath above.</p>
        <div id="gMeta" class="meta">no file</div>
        <p class="row">
          <button id="sendG" disabled>Send to inbox</button>
        </p>
      </section>
    </div>

    <pre id="logAdd">pick a file</pre>
  </section>
  <dialog id="conflictDialog">
    <p>A model with this name already exists.</p>
    <p>Overwrite it or save both?</p>
    <div class="row">
      <button type="button" id="conflictOverwrite">Overwrite</button>
      <button type="button" id="conflictKeep">Save both</button>
      <button type="button" id="conflictCancel" class="sec">Cancel</button>
    </div>
  </dialog>
`

const $ = (id) => document.getElementById(id)

function activeLog() {
  return $('view-add').classList.contains('hide') ? $('logLib') : $('logAdd')
}
const log = (m) => { activeLog().textContent = m }

/* ---------- FLPV decode (library thumbs only) ---------- */
function flpvToDataUrl(buf) {
  const u8 = buf instanceof Uint8Array ? buf : new Uint8Array(buf)
  if (u8.byteLength < 8) return null
  if (u8[0] !== 70 || u8[1] !== 76 || u8[2] !== 80 || u8[3] !== 86) return null
  const w = u8[4] | (u8[5] << 8)
  const h = u8[6] | (u8[7] << 8)
  if (w <= 0 || h <= 0 || w > 512 || h > 512) return null
  if (u8.byteLength < 8 + w * h * 2) return null
  const canvas = document.createElement('canvas')
  canvas.width = w
  canvas.height = h
  const ctx = canvas.getContext('2d')
  const img = ctx.createImageData(w, h)
  let o = 8
  for (let i = 0; i < w * h; i++) {
    const lo = u8[o++]
    const hi = u8[o++]
    const v = lo | (hi << 8)
    const r = (v >> 11) & 31
    const g = (v >> 5) & 63
    const b = v & 31
    img.data[i * 4] = (r * 255 / 31) | 0
    img.data[i * 4 + 1] = (g * 255 / 63) | 0
    img.data[i * 4 + 2] = (b * 255 / 31) | 0
    img.data[i * 4 + 3] = 255
  }
  ctx.putImageData(img, 0, 0)
  return canvas.toDataURL('image/png')
}

function fmtSize(n) {
  if (n < 1024) return n + ' B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + ' KB'
  return (n / (1024 * 1024)).toFixed(2) + ' MB'
}

function baseName(name) {
  return String(name || '').replace(/\.(gcode|gco|g)$/i, '')
}

async function loadLibrary() {
  const grid = $('libGrid')
  grid.innerHTML = ''
  log('Loading inbox…')
  let data
  try {
    const r = await fetch('/inbox.json', { cache: 'no-store' })
    if (!r.ok) throw new Error('HTTP ' + r.status)
    data = await r.json()
  } catch (e) {
    grid.innerHTML = `
      <button type="button" class="card add-card" id="addBtnEmpty">
        <span class="plus">+</span>
        <span>Add model</span>
      </button>`
    $('addBtnEmpty').addEventListener('click', showAdd)
    log('Inbox unavailable (' + e.message + '). Open this page via PPSSPP :8081.')
    return
  }
  const files = (data && data.files) || []
  for (const f of files) {
    const card = document.createElement('article')
    card.className = 'card'
    card.dataset.name = f.name
    const thumb = document.createElement('div')
    thumb.className = 'thumb'
    thumb.textContent = '…'
    const title = document.createElement('div')
    title.className = 'card-title'
    title.textContent = f.name
    const meta = document.createElement('div')
    meta.className = 'card-meta'
    meta.textContent = fmtSize(f.size || 0) +
      (f.flpv_sup ? ' · dual preview' : f.flpv ? ' · preview' : ' · no preview')
    card.appendChild(thumb)
    card.appendChild(title)
    card.appendChild(meta)
    grid.appendChild(card)

    const previewName = f.flpv_sup
      ? baseName(f.name) + '_sup.flpv'
      : (f.flpv ? baseName(f.name) + '.flpv' : null)
    if (previewName) {
      fetch('/inbox/' + encodeURIComponent(previewName), { cache: 'no-store' })
        .then((r) => { if (!r.ok) throw new Error('no flpv'); return r.arrayBuffer() })
        .then((buf) => {
          const url = flpvToDataUrl(buf)
          if (!url) { thumb.textContent = '—'; return }
          thumb.textContent = ''
          const img = document.createElement('img')
          img.alt = f.name
          img.src = url
          thumb.appendChild(img)
        })
        .catch(() => { thumb.textContent = '—'; })
    } else {
      thumb.textContent = 'no FLPV'
    }
  }

  const add = document.createElement('button')
  add.type = 'button'
  add.className = 'card add-card'
  add.id = 'addBtn'
  add.innerHTML = '<span class="plus">+</span><span>Add model</span>'
  add.addEventListener('click', showAdd)
  grid.appendChild(add)

  log(files.length
    ? ('Inbox: ' + files.length + ' model' + (files.length === 1 ? '' : 's'))
    : 'Inbox empty — tap + to add')
}

$('libRefresh').addEventListener('click', () => loadLibrary())

/* ---------- views ---------- */
let view = null

function showLib() {
  $('view-add').classList.add('hide')
  $('view-lib').classList.remove('hide')
  loadLibrary()
}

function showAdd() {
  $('view-lib').classList.add('hide')
  $('view-add').classList.remove('hide')
  log('Drop STL or G-code, or choose a file')
}

$('backLib').addEventListener('click', () => {
  resetAdd()
  showLib()
})

function fillSelect(el, values, selected) {
  el.innerHTML = ''
  for (const v of values) {
    const opt = document.createElement('option')
    opt.value = v
    opt.textContent = v
    if (v === selected) opt.selected = true
    el.appendChild(opt)
  }
}

fillSelect($('pattern'), FILL_PATTERNS, 'gyroid')
fillSelect($('supType'), SUPPORT_TYPES, 'normal(auto)')
fillSelect($('supStyle'), SUPPORT_STYLES, 'default')

function syncSupportUi() {
  const on = $('supports').checked
  $('supType').disabled = !on
  $('supStyle').disabled = !on
  $('supAngle').disabled = !on
}
$('supports').addEventListener('change', () => {
  syncSupportUi()
  scheduleSavePrefs()
})
syncSupportUi()

/* ---------- naming / offscreen FLPV ---------- */
function asciiName(name, fallback) {
  name = (name || fallback).split(/[/\\]/).pop()
  let out = ''
  for (let i = 0; i < name.length; i++) {
    const c = name[i]
    if (/[a-zA-Z0-9._-]/.test(c)) out += c
    else if (c === ' ') out += '_'
  }
  return out || fallback
}
function baseOf(name) {
  return asciiName(name, 'model').replace(/\.(gcode|gco|g|stl)$/i, '')
}

function parseGcode2d(text) {
  const segs = []
  let x = 0, y = 0, z = 0, e = 0, absE = true
  let minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9
  const zSet = new Set()
  const lines = text.split(/\r?\n/)
  const maxSegs = 120000
  for (let li = 0; li < lines.length; li++) {
    let line = lines[li]
    const sc = line.indexOf(';')
    if (sc >= 0) line = line.slice(0, sc)
    line = line.trim()
    if (!line) continue
    const up = line.toUpperCase()
    if (up.includes('M82')) { absE = true; continue }
    if (up.includes('M83')) { absE = false; continue }
    const isG0 = /\bG0\b/.test(up) || /\bG00\b/.test(up)
    const isG1 = /\bG1\b/.test(up) || /\bG01\b/.test(up)
    if (!isG0 && !isG1) continue
    let nx = x, ny = y, nz = z, ne = e, hasE = false
    const parts = up.replace(/([A-Z])/g, ' $1').trim().split(/\s+/)
    for (const p of parts) {
      if (p.length < 2) continue
      const code = p[0]
      const val = parseFloat(p.slice(1))
      if (!isFinite(val)) continue
      if (code === 'X') nx = val
      else if (code === 'Y') ny = val
      else if (code === 'Z') nz = val
      else if (code === 'E') { hasE = true; ne = absE ? val : e + val }
    }
    if (hasE && ne > e + 1e-5 && segs.length < maxSegs) {
      segs.push({ x0: x, y0: y, z0: z, x1: nx, y1: ny, z1: nz })
      for (const v of [x, nx]) { if (v < minX) minX = v; if (v > maxX) maxX = v }
      for (const v of [y, ny]) { if (v < minY) minY = v; if (v > maxY) maxY = v }
      zSet.add(Math.round(nz * 1000) / 1000)
    }
    x = nx; y = ny; z = nz; e = ne
  }
  const zLevels = Array.from(zSet).sort((a, b) => a - b)
  if (!segs.length) { minX = minY = 0; maxX = maxY = 1 }
  return { segs, zLevels, minX, maxX, minY, maxY }
}

function drawModel(ctx, m, layerIdx) {
  ctx.fillStyle = '#0a0a12'
  ctx.fillRect(0, 0, CV_W, CV_H)
  if (!m || !m.segs.length) {
    ctx.fillStyle = '#666'
    ctx.font = '12px sans-serif'
    ctx.fillText('no extrusion', 16, CV_H / 2)
    return
  }
  const pad = 8
  const bw = m.maxX - m.minX || 1
  const bh = m.maxY - m.minY || 1
  const s = Math.min((CV_W - 2 * pad) / bw, (CV_H - 2 * pad) / bh)
  const ox = pad + (CV_W - 2 * pad - bw * s) / 2
  const oy = pad + (CV_H - 2 * pad - bh * s) / 2
  const mapX = (v) => ox + (v - m.minX) * s
  const mapY = (v) => oy + (m.maxY - v) * s
  let zLo = -1e9, zHi = 1e9
  if (layerIdx >= 0 && layerIdx < m.zLevels.length) {
    const z = m.zLevels[layerIdx]
    zLo = z - 0.05; zHi = z + 0.05
  }
  ctx.lineWidth = 1
  ctx.strokeStyle = '#3d9cff'
  ctx.beginPath()
  for (const sg of m.segs) {
    if (sg.z1 < zLo || sg.z1 > zHi) continue
    ctx.moveTo(mapX(sg.x0), mapY(sg.y0))
    ctx.lineTo(mapX(sg.x1), mapY(sg.y1))
  }
  ctx.stroke()
  ctx.strokeStyle = '#333'
  ctx.strokeRect(ox, oy, bw * s, bh * s)
}

/** Offscreen canvas → FLPV ArrayBuffer (never shown in the page). */
function gcodeToFlpv(text) {
  const canvas = document.createElement('canvas')
  canvas.width = CV_W
  canvas.height = CV_H
  const ctx = canvas.getContext('2d')
  const model = parseGcode2d(text)
  drawModel(ctx, model, -1)
  const img = ctx.getImageData(0, 0, CV_W, CV_H)
  const d = img.data
  const pix = new Uint8Array(8 + CV_W * CV_H * 2)
  pix[0] = 70; pix[1] = 76; pix[2] = 80; pix[3] = 86
  pix[4] = CV_W & 255; pix[5] = (CV_W >> 8) & 255
  pix[6] = CV_H & 255; pix[7] = (CV_H >> 8) & 255
  let o = 8
  for (let i = 0; i < CV_W * CV_H; i++) {
    const r = d[i * 4], g = d[i * 4 + 1], b = d[i * 4 + 2]
    const v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    pix[o++] = v & 255
    pix[o++] = (v >> 8) & 255
  }
  return pix.buffer
}

function chooseConflictMode() {
  return new Promise((resolve) => {
    const dialog = $('conflictDialog')
    const done = (mode) => {
      dialog.close()
      $('conflictOverwrite').onclick = null
      $('conflictKeep').onclick = null
      $('conflictCancel').onclick = null
      resolve(mode)
    }
    $('conflictOverwrite').onclick = () => done('overwrite')
    $('conflictKeep').onclick = () => done('keep_both')
    $('conflictCancel').onclick = () => done('')
    dialog.showModal()
  })
}

async function postBytes(path, name, buf, mode = '') {
  const query = '?name=' + encodeURIComponent(name) + (mode ? '&mode=' + mode : '')
  const r = await fetch(path + query, {
    method: 'POST',
    body: buf,
    headers: { 'Content-Type': 'application/octet-stream' },
  })
  const t = await r.text()
  if (r.status === 409 && !mode) {
    const choice = await chooseConflictMode()
    if (!choice) throw new Error('upload cancelled')
    return postBytes(path, name, buf, choice)
  }
  if (!r.ok) throw new Error(t || ('HTTP ' + r.status))
  return t
}

function savedSlug(response, fallback) {
  const match = /(?:^|\s)slug=([^\s]+)/.exec(response)
  return match ? match[1] : fallback
}

function gcodeToText(g) {
  if (typeof g === 'string') return g
  return new TextDecoder().decode(g)
}

function scaleBinaryStl(buf, scalePct) {
  const s = scalePct / 100
  if (Math.abs(s - 1) < 1e-6) return buf
  const u8 = buf instanceof Uint8Array ? buf.slice() : new Uint8Array(buf.slice(0))
  if (u8.byteLength < 84) throw new Error('STL too small')
  const n = new DataView(u8.buffer, u8.byteOffset, u8.byteLength).getUint32(80, true)
  const need = 84 + n * 50
  if (u8.byteLength < need) throw new Error('truncated binary STL')
  const dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength)
  for (let i = 0; i < n; i++) {
    const base = 84 + i * 50 + 12
    for (let v = 0; v < 9; v++) {
      const o = base + v * 4
      dv.setFloat32(o, dv.getFloat32(o, true) * s, true)
    }
  }
  return u8.buffer
}

/* ---------- view layer / toggles ---------- */
function syncLayerUi(nLayers) {
  const el = $('vLayer')
  if (!nLayers || nLayers <= 0) {
    el.disabled = true
    el.min = 0
    el.max = 0
    el.value = 0
    $('vLayerLbl').textContent = '—'
    return
  }
  el.disabled = false
  el.min = 0
  el.max = nLayers
  el.value = nLayers
  $('vLayerLbl').textContent = 'all (' + nLayers + ')'
  if (view) view.setLayerRange(0, nLayers - 1)
}

function applyLayerSlider() {
  if (!view) return
  const n = view.layerCount()
  if (n <= 0) return
  const v = +$('vLayer').value
  if (v === 0 || v >= n) {
    $('vLayerLbl').textContent = 'all (' + n + ')'
    view.setLayerRange(0, n - 1)
  } else {
    $('vLayerLbl').textContent = v + '/' + n
    view.setLayerRange(0, v - 1)
  }
}

$('vLayer').addEventListener('input', applyLayerSlider)
$('vTravel').addEventListener('change', () => {
  if (view) view.setTravelVisible($('vTravel').checked)
})
$('vMesh').addEventListener('change', () => {
  if (view) view.setMeshVisible($('vMesh').checked)
})

function showSupToggle(on) {
  $('supToggle').classList.toggle('hide', !on)
}

function setSupMode(mode) {
  document.querySelectorAll('#supToggle .seg-btn').forEach((b) => {
    b.classList.toggle('on', b.dataset.sup === mode)
  })
  if (!sliced || !view) return
  const text = mode === 'no' ? sliced.gcodeNo : sliced.gcodeText
  const n = view.showToolpath(text, { hideMesh: !$('vMesh').checked })
  syncLayerUi(n)
  view.setMeshVisible($('vMesh').checked)
  view.setTravelVisible($('vTravel').checked)
}

document.querySelectorAll('#supToggle .seg-btn').forEach((btn) => {
  btn.addEventListener('click', () => setSupMode(btn.dataset.sup))
})

/* ---------- slicer state ---------- */
let stlBuf = null
let stlName = 'model.stl'
let baseSettings = null
let processCatalog = null
let filamentSettings = null
let filamentKeys = null
let sliced = null
let slicerPromise = null
let gFile = null
let gFlpv = null
let gText = null
let addMode = null /* 'stl' | 'gcode' | null */

function getSlicer() {
  if (!slicerPromise) {
    log('Loading WASM slicer…')
    slicerPromise = createSlicer().then((s) => {
      log('WASM ready')
      return s
    })
  }
  return slicerPromise
}

function collectUiPrefs() {
  return {
    v: 2,
    machine: MACHINE,
    filament: FILAMENT,
    scale: parseFloat($('scale').value) || 100,
    process: $('process').value || '',
    infill: parseFloat($('infill').value) || 15,
    layer: parseFloat($('layer').value) || 0.2,
    nozzle_temp: parseInt($('nozzleTemp').value, 10) || 220,
    bed_temp: parseInt($('bedTemp').value, 10) || 0,
    pattern: $('pattern').value || 'gyroid',
    supports: !!$('supports').checked,
    support_type: $('supType').value || 'normal(auto)',
    support_style: $('supStyle').value || 'default',
    support_threshold_angle: parseInt($('supAngle').value, 10) || 30,
  }
}

async function saveUiPrefs() {
  try {
    const body = JSON.stringify(collectUiPrefs())
    const r = await fetch('/slicer.json', {
      method: 'POST',
      body,
      headers: { 'Content-Type': 'application/json' },
    })
    if (!r.ok) throw new Error(await r.text())
  } catch (e) {
    console.warn('slicer.json save', e)
  }
}

async function loadUiPrefs() {
  try {
    const r = await fetch('/slicer.json', { cache: 'no-store' })
    if (!r.ok) return null
    return await r.json()
  } catch {
    return null
  }
}

function applyUiPrefs(prefs) {
  if (!prefs || typeof prefs !== 'object') return
  if (prefs.scale != null) $('scale').value = prefs.scale
  if (prefs.infill != null) $('infill').value = prefs.infill
  if (prefs.layer != null) $('layer').value = prefs.layer
  if (prefs.nozzle_temp != null) $('nozzleTemp').value = prefs.nozzle_temp
  if (prefs.bed_temp != null) $('bedTemp').value = prefs.bed_temp
  if (prefs.pattern) {
    for (const o of $('pattern').options) {
      if (o.value === prefs.pattern) { $('pattern').value = prefs.pattern; break }
    }
  }
  if (prefs.supports != null) $('supports').checked = !!prefs.supports
  if (prefs.support_type) {
    for (const o of $('supType').options) {
      if (o.value === prefs.support_type) { $('supType').value = prefs.support_type; break }
    }
  }
  if (prefs.support_style) {
    for (const o of $('supStyle').options) {
      if (o.value === prefs.support_style) { $('supStyle').value = prefs.support_style; break }
    }
  }
  if (prefs.support_threshold_angle != null) $('supAngle').value = prefs.support_threshold_angle
  if (prefs.process) {
    for (const o of $('process').options) {
      if (o.value === prefs.process) { $('process').value = prefs.process; break }
    }
  }
  syncSupportUi()
}

let saveTimer = null
function scheduleSavePrefs() {
  clearTimeout(saveTimer)
  saveTimer = setTimeout(() => { saveUiPrefs() }, 400)
}

async function loadPresets() {
  const machine = printerSettings(MACHINE)
  if (!machine) throw new Error('missing printer profile ' + MACHINE)
  processCatalog = await processPresets()
  const filaments = await filamentPresets()
  const defaultProc = printerDefaultPreset(MACHINE) || '0.20mm Standard @FLSun QQSPro'
  const saved = await loadUiPrefs()
  const procName = (saved && saved.process) || defaultProc
  const proc = processCatalog.settingsFor(procName) || processCatalog.settingsFor(defaultProc)
  filamentSettings = filaments.settingsFor(FILAMENT)
  filamentKeys = filaments.keys
  if (filamentSettings) {
    const nz = Array.isArray(filamentSettings.nozzle_temperature)
      ? filamentSettings.nozzle_temperature[0] : 220
    const bd = Array.isArray(filamentSettings.hot_plate_temp)
      ? filamentSettings.hot_plate_temp[0] : 45
    $('nozzleTemp').value = nz
    $('bedTemp').value = bd
  }

  const sel = $('process')
  sel.innerHTML = ''
  const list = processCatalog.listFor(MACHINE) || []
  for (const name of list) {
    const opt = document.createElement('option')
    opt.value = name
    opt.textContent = name
    if (name === (saved && saved.process ? saved.process : procName)) opt.selected = true
    sel.appendChild(opt)
  }

  $('layer').value = (proc && proc.layer_height) || 0.2
  const inf = proc && proc.sparse_infill_density
  if (typeof inf === 'string') $('infill').value = parseFloat(inf) || 15
  else if (typeof inf === 'number') $('infill').value = inf
  else $('infill').value = 15
  if (!saved) $('scale').value = 100
  applyUiPrefs(saved)

  baseSettings = rebuildSettingsFromUi()
  log(saved
    ? ('Presets + phone prefs · ' + MACHINE)
    : ('Presets loaded · ' + MACHINE))
}

function rebuildSettingsFromUi() {
  let s = {}
  s = applyPreset(s, printerSettings(MACHINE), presetOptionKeys('machine'))
  if (filamentSettings)
    s = applyPreset(s, filamentSettings, filamentKeys || presetOptionKeys('filament'))
  const procName = $('process').value
  if (processCatalog && procName) {
    const proc = processCatalog.settingsFor(procName)
    if (proc) s = applyPreset(s, proc, processCatalog.keys)
  }
  s.layer_height = parseFloat($('layer').value) || 0.2
  s.initial_layer_print_height = s.layer_height
  s.sparse_infill_density = (parseFloat($('infill').value) || 15) + '%'
  s.sparse_infill_pattern = $('pattern').value || 'gyroid'

  /* Temperatures from the panel: applied to every plate type so the
   * machine profile picks them up regardless of bed_type. */
  const nz = Math.min(300, Math.max(150, parseInt($('nozzleTemp').value, 10) || 220))
  const bd = Math.min(110, Math.max(0, parseInt($('bedTemp').value, 10) || 0))
  s.nozzle_temperature = [nz]
  s.nozzle_temperature_initial_layer = [nz]
  for (const pk of [
    'hot_plate_temp', 'hot_plate_temp_initial_layer',
    'cool_plate_temp', 'cool_plate_temp_initial_layer',
    'textured_plate_temp', 'textured_plate_temp_initial_layer',
    'textured_cool_plate_temp', 'textured_cool_plate_temp_initial_layer',
    'supertack_plate_temp', 'supertack_plate_temp_initial_layer',
    'eng_plate_temp', 'eng_plate_temp_initial_layer',
  ]) s[pk] = [bd]

  /* The stock start gcode preheats to a hardcoded S100 and resolves temps
   * from placeholders; if a placeholder fails the nozzle prints at 100C.
   * Substitute real numbers and preheat straight to the target. */
  if (typeof s.machine_start_gcode === 'string') {
    s.machine_start_gcode = s.machine_start_gcode
      .replace('M104 S100 ; preheat extruder to no ooze temp',
               'M104 S' + nz + ' ; preheat extruder to target')
      .replaceAll('[nozzle_temperature_initial_layer_single]', String(nz))
      .replaceAll('[nozzle_temperature_initial_layer]', String(nz))
      .replaceAll('[bed_temperature_initial_layer_single]', String(bd))
      .replaceAll('[bed_temperature_initial_layer]', String(bd))
  }
  return s
}

function currentSettings() {
  return rebuildSettingsFromUi()
}

;['process', 'infill', 'layer', 'nozzleTemp', 'bedTemp', 'pattern', 'supType', 'supStyle', 'supAngle'].forEach((id) => {
  const el = $(id)
  el.addEventListener('change', () => {
    baseSettings = rebuildSettingsFromUi()
    scheduleSavePrefs()
  })
  el.addEventListener('input', scheduleSavePrefs)
})

$('scale').addEventListener('input', () => {
  const pct = parseFloat($('scale').value) || 100
  if (view) view.setMeshScale(pct)
  scheduleSavePrefs()
})
$('scale').addEventListener('change', () => {
  baseSettings = rebuildSettingsFromUi()
  scheduleSavePrefs()
})

function resetAdd() {
  stlBuf = null
  sliced = null
  gFile = null
  gFlpv = null
  gText = null
  addMode = null
  $('addFile').value = ''
  $('fileKind').textContent = 'no file'
  $('workArea').classList.add('hide')
  $('panel-stl').classList.add('hide')
  $('panel-gcode').classList.add('hide')
  showSupToggle(false)
  $('slice').disabled = true
  $('sendStl').disabled = true
  $('sendG').disabled = true
  $('stlMeta').textContent = 'no STL'
  $('gMeta').textContent = 'no file'
  if (view) {
    view.clearToolpath()
    view.clearMesh()
  }
  syncLayerUi(0)
  $('logAdd').textContent = 'pick a file'
}

function ensureView() {
  if (!view) {
    try {
      view = new Viewport3D($('view3d'))
    } catch (e) {
      log('3D viewport unavailable: ' + e)
      view = null
    }
  }
  return view
}

async function handleChosenFile(f) {
  if (!f) return
  sliced = null
  gFile = null
  gFlpv = null
  gText = null
  showSupToggle(false)
  $('sendStl').disabled = true
  $('sendG').disabled = true
  syncLayerUi(0)

  const lower = f.name.toLowerCase()
  $('workArea').classList.remove('hide')

  if (lower.endsWith('.stl')) {
    addMode = 'stl'
    $('panel-stl').classList.remove('hide')
    $('panel-gcode').classList.add('hide')
    stlName = asciiName(f.name, 'model.stl')
    stlBuf = await f.arrayBuffer()
    $('slice').disabled = false
    $('fileKind').textContent = 'STL · slicer'
    $('stlMeta').textContent = stlName + ' · ' + stlBuf.byteLength + ' bytes'
    log('Loading STL into 3D…')
    const v = ensureView()
    if (v) {
      try {
        v.clearToolpath()
        await v.loadStl(stlBuf, stlName)
        v.setMeshScale(parseFloat($('scale').value) || 100)
        v.setMeshVisible(true)
        $('vMesh').checked = true
        log('STL ready. Set fill / supports, then Slice.')
      } catch (e) {
        log('STL load error: ' + e)
      }
    } else {
      log('STL loaded (no 3D). Set fill / supports, then Slice.')
    }
    getSlicer().catch((e) => log('WASM load error: ' + e))
    return
  }

  if (/\.(gcode|gco|g)$/.test(lower)) {
    addMode = 'gcode'
    $('panel-gcode').classList.remove('hide')
    $('panel-stl').classList.add('hide')
    stlBuf = null
    $('slice').disabled = true
    gFile = f
    $('sendG').disabled = false
    $('fileKind').textContent = 'G-code · preview for phone (offscreen)'
    log('Parsing ' + f.name + '…')
    gText = await f.text()
    gFlpv = gcodeToFlpv(gText) /* offscreen — not displayed */
    const v = ensureView()
    let n = 0
    if (v) {
      try {
        n = v.showToolpath(gText, { hideMesh: true })
        v.clearMesh()
        v.setTravelVisible($('vTravel').checked)
      } catch (e) {
        log('3D toolpath error: ' + e)
      }
    }
    $('vMesh').checked = false
    syncLayerUi(n)
    $('gMeta').textContent = (n ? n + ' layers · ' : '') + f.size + ' bytes'
    log('G-code ready. Send uploads gcode + FLPV for PSP.')
    return
  }

  log('Unsupported file type')
  $('workArea').classList.add('hide')
}

$('addFile').addEventListener('change', async () => {
  const f = $('addFile').files[0]
  if (f) await handleChosenFile(f)
})

const drop = $('dropZone')
;['dragenter', 'dragover'].forEach((ev) => {
  drop.addEventListener(ev, (e) => {
    e.preventDefault()
    drop.classList.add('drag')
  })
})
;['dragleave', 'drop'].forEach((ev) => {
  drop.addEventListener(ev, (e) => {
    e.preventDefault()
    drop.classList.remove('drag')
  })
})
drop.addEventListener('drop', async (e) => {
  const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0]
  if (f) await handleChosenFile(f)
})

$('slice').addEventListener('click', async () => {
  if (!stlBuf) return
  $('slice').disabled = true
  $('sendStl').disabled = true
  showSupToggle(false)
  try {
    if (!baseSettings) await loadPresets()
    const slicer = await getSlicer()
    const scale = parseFloat($('scale').value) || 100
    const scaled = scaleBinaryStl(stlBuf, scale)
    const base = currentSettings()
    const name = baseOf(stlName)
    const wantSup = $('supports').checked
    const angle = parseInt($('supAngle').value, 10) || 30

    log('1/2 Slicing without supports…')
    const no = slicer.slice(scaled, deriveKernelParams({
      ...base,
      enable_support: false,
    }))
    if (no.error) throw new Error(String(no.error))
    const gNo = gcodeToText(no.gcode)
    const flpvNo = gcodeToFlpv(gNo)

    let gYes = gNo
    let flpvSup = null
    let yesStats = no.stats
    if (wantSup) {
      log('2/2 Slicing with supports…')
      const yes = slicer.slice(scaled, deriveKernelParams({
        ...base,
        enable_support: true,
        support_type: $('supType').value || 'normal(auto)',
        support_style: $('supStyle').value || 'default',
        support_threshold_angle: angle,
      }))
      if (yes.error) throw new Error(String(yes.error))
      gYes = gcodeToText(yes.gcode)
      flpvSup = gcodeToFlpv(gYes)
      yesStats = yes.stats
    } else {
      log('2/2 Supports off — print gcode = no supports')
    }

    if (gYes.length > MAX_GCODE) {
      throw new Error('gcode ' + gYes.length + ' bytes > 4MiB inbox limit — lower scale/infill')
    }

    sliced = {
      name,
      gcodeText: gYes,
      gcodeNo: gNo,
      flpvNo,
      flpvSup,
      withSupports: wantSup,
    }
    await saveUiPrefs()

    log('Building 3D toolpath…')
    const n = view.showToolpath(gYes, { hideMesh: true })
    $('vMesh').checked = false
    syncLayerUi(n)
    view.setTravelVisible($('vTravel').checked)
    if (wantSup) {
      showSupToggle(true)
      setSupMode('yes')
    } else {
      showSupToggle(false)
    }

    $('stlMeta').textContent =
      name + ' · print gcode ' + gYes.length + ' B · filament ~' +
      Math.round(yesStats.filament_mm || 0) + ' mm' +
      (wantSup
        ? (' (with supports) · nosup ' + Math.round(no.stats.filament_mm || 0) + ' mm')
        : '') +
      ' · ' + n + ' layers · ' + ($('pattern').value || '')
    $('sendStl').disabled = false
    log('Slice OK. Send drops ' + name + '.gcode' +
      (wantSup ? ' + .flpv + _sup.flpv' : ' + .flpv'))
  } catch (e) {
    log('Slice error: ' + e)
  }
  $('slice').disabled = false
})

$('sendStl').addEventListener('click', async () => {
  if (!sliced) return
  $('sendStl').disabled = true
  try {
    const gname = sliced.name + '.gcode'
    const blob = new Blob([sliced.gcodeText], { type: 'application/octet-stream' })
    log('Uploading ' + gname + '…')
    const response = await postBytes('/upload', gname, blob)
    const slug = savedSlug(response, sliced.name)
    log('Preview without supports…')
    await postBytes('/preview', slug + '.flpv', sliced.flpvNo, 'overwrite')
    if (sliced.flpvSup) {
      log('Preview with supports…')
      await postBytes('/preview', slug + '_sup.flpv', sliced.flpvSup, 'overwrite')
    }
    log('OK — in inbox. Back to Models…')
    resetAdd()
    showLib()
  } catch (e) {
    log('Send error: ' + e)
    $('sendStl').disabled = false
  }
})

$('sendG').addEventListener('click', async () => {
  if (!gFile || !gFlpv) return
  $('sendG').disabled = true
  try {
    const gname = asciiName(gFile.name, 'upload.gcode')
    log('1/2 Uploading ' + gname + '…')
    const response = await postBytes('/upload', gname, gFile)
    const slug = savedSlug(response, baseOf(gname))
    const pname = slug + '.flpv'
    log('2/2 Uploading preview (offscreen FLPV)…')
    await postBytes('/preview', pname, gFlpv, 'overwrite')
    log('OK — ' + gname + ' + ' + pname)
    resetAdd()
    showLib()
  } catch (e) {
    log('Send error: ' + e)
    $('sendG').disabled = false
  }
})

loadPresets().catch((e) => console.warn('Preset load', e))
loadLibrary()
