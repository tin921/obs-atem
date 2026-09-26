'use strict';
// Shared model for the PiP panel mockups.
//
// Mirrors the C++ side so a layout that works here ports straight to
// pip-dock.cpp:
//   - field names match AtemPipState (atem-pip.h)
//   - LIMITS match the constants at the top of pip-dock.cpp
//   - continuous edits are coalesced and "sent" at ~30 Hz, like flushPending()
//   - device-side changes never overwrite a control the operator is editing
//
// Nothing here talks to an ATEM: "sent" commands are printed to the bench log.

const FRAME = { w: 32, h: 18 }; // switcher units: frame edge at X ±16, Y ±9 (16:9)

const LIMITS = {
  positionX:  { slider: [-16, 16], spin: [-32, 32], decimals: 2, step: 0.01 },
  positionY:  { slider: [-9, 9],   spin: [-18, 18], decimals: 2, step: 0.01 },
  sizeX:      { slider: [0, 1],    spin: [0, 1],    decimals: 3, step: 0.001 },
  sizeY:      { slider: [0, 1],    spin: [0, 1],    decimals: 3, step: 0.001 },
  cropTop:    { slider: [0, 18],   spin: [0, 38],   decimals: 2, step: 0.01 },
  cropBottom: { slider: [0, 18],   spin: [0, 38],   decimals: 2, step: 0.01 },
  cropLeft:   { slider: [0, 32],   spin: [0, 52],   decimals: 2, step: 0.01 },
  cropRight:  { slider: [0, 32],   spin: [0, 52],   decimals: 2, step: 0.01 },
};

const SDK_CALL = {
  programInput: 'IBMDSwitcherMixEffectBlock::SetProgramInput',
  pipInput:     'IBMDSwitcherKey::SetInputFill',
  onAir:        'IBMDSwitcherKey::SetOnAir',
  isDVE:        'IBMDSwitcherKey::SetType(bmdSwitcherKeyTypeDVE)',
  positionX:    'IBMDSwitcherKeyFlyParameters::SetPositionX',
  positionY:    'IBMDSwitcherKeyFlyParameters::SetPositionY',
  sizeX:        'IBMDSwitcherKeyFlyParameters::SetSizeX',
  sizeY:        'IBMDSwitcherKeyFlyParameters::SetSizeY',
  cropEnabled:  'IBMDSwitcherKeyDVEParameters::SetMasked',
  cropTop:      'IBMDSwitcherKeyDVEParameters::SetMaskTop',
  cropBottom:   'IBMDSwitcherKeyDVEParameters::SetMaskBottom',
  cropLeft:     'IBMDSwitcherKeyDVEParameters::SetMaskLeft',
  cropRight:    'IBMDSwitcherKeyDVEParameters::SetMaskRight',
  resetDVE:     'IBMDSwitcherKeyFlyParameters::ResetDVE',
  resetMask:    'IBMDSwitcherKeyDVEParameters::ResetMask',
};

// ATEM Mini input ids as the SDK reports them.
const INPUTS = [
  { id: 1,    name: 'Camera 1',       short: 'CAM 1', color: '#35618f', program: true, pip: true },
  { id: 2,    name: 'Camera 2',       short: 'CAM 2', color: '#8f5a35', program: true, pip: true },
  { id: 3,    name: 'Camera 3',       short: 'CAM 3', color: '#3f7d4e', program: true, pip: true },
  { id: 4,    name: 'Camera 4',       short: 'CAM 4', color: '#6f4a8f', program: true, pip: true },
  { id: 0,    name: 'Black',          short: 'BLK',   color: '#000000', program: true, pip: false },
  { id: 1000, name: 'Color Bars',     short: 'BARS',  color: 'bars',    program: true, pip: true },
  { id: 2001, name: 'Color 1',        short: 'COL1',  color: '#b03a48', program: true, pip: true },
  { id: 2002, name: 'Color 2',        short: 'COL2',  color: '#c9a227', program: true, pip: true },
  { id: 3010, name: 'Media Player 1', short: 'MP1',   color: '#2f7f86', program: true, pip: true },
];
const EXTERNAL_INPUTS = INPUTS.filter((i) => i.id >= 1 && i.id <= 4);

function inputById(id) {
  return INPUTS.find((i) => i.id === id) || { id, name: 'Input ' + id, short: String(id), color: '#333' };
}

// Paints an input's picture (operator-chosen PNG) or its placeholder colour.
function paintInput(el, input, image) {
  el.classList.toggle('bars', !image && input.color === 'bars');
  el.style.background = image || input.color === 'bars' ? '' : input.color;
  el.style.backgroundImage = image ? `url("${image}")` : '';
  el.style.backgroundSize = image ? 'cover' : '';
  el.style.backgroundPosition = image ? 'center' : '';
}

// localStorage can be missing or throw (private mode, file:// policies).
function storeGet(key) {
  try { return JSON.parse(localStorage.getItem(key)); } catch { return null; }
}
function storeSet(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); return true; } catch { return false; }
}

function round(v, decimals) {
  const f = Math.pow(10, decimals);
  return Math.round(v * f) / f;
}

class PipModel extends EventTarget {
  constructor() {
    super();
    this.connected = true;
    this.state = {
      programInput: 1,
      pipInput: 2,
      onAir: true,
      isDVE: true,
      canDVE: true,
      canScaleUp: false,
      positionX: 9.6,
      positionY: 5.1,
      sizeX: 0.3,
      sizeY: 0.3,
      cropEnabled: false,
      cropTop: 0,
      cropBottom: 0,
      cropLeft: 0,
      cropRight: 0,
    };
    this.pending = new Map();
    this.flushTimer = null;
    // input id → image data URL, chosen in the panel settings
    this.images = storeGet('pipMockup.images') || {};
  }

  imageFor(id) {
    return this.images[id] || null;
  }

  setImage(id, dataUrl) {
    if (dataUrl) this.images[id] = dataUrl;
    else delete this.images[id];
    if (!storeSet('pipMockup.images', this.images)) this.log('images not saved (browser storage unavailable)', 'dev');
    this.emit('images');
  }

  get(field) {
    return this.state[field];
  }

  limitsFor(field) {
    const l = LIMITS[field];
    if (!l) return null;
    if ((field === 'sizeX' || field === 'sizeY') && this.state.canScaleUp) {
      return { ...l, spin: [0, 2] };
    }
    return l;
  }

  // Operator edit. Continuous values are coalesced like pip-dock.cpp.
  edit(field, value) {
    if (!this.connected) return;
    const l = this.limitsFor(field);
    if (l) {
      value = round(Math.min(l.spin[1], Math.max(l.spin[0], Number(value) || 0)), l.decimals);
      this.state[field] = value;
      this.pending.set(field, value);
      if (!this.flushTimer) this.flushTimer = setTimeout(() => this.flush(), 33);
    } else {
      this.state[field] = value;
      this.send(field, value);
    }
    this.emit('operator');
  }

  editMany(values) {
    for (const [field, value] of Object.entries(values)) this.edit(field, value);
  }

  action(name) {
    if (!this.connected) return;
    if (name === 'resetDVE') Object.assign(this.state, { positionX: 0, positionY: 0, sizeX: 0.5, sizeY: 0.5 });
    if (name === 'resetMask') Object.assign(this.state, { cropTop: 0, cropBottom: 0, cropLeft: 0, cropRight: 0 });
    if (name === 'makeDVE') this.state.isDVE = true;
    this.log(`${name === 'makeDVE' ? SDK_CALL.isDVE : SDK_CALL[name] + '()'}`);
    this.emit('operator');
  }

  flush() {
    this.flushTimer = null;
    for (const [field, value] of this.pending) this.send(field, value);
    this.pending.clear();
  }

  send(field, value) {
    if (field === 'isDVE') return this.log(SDK_CALL.isDVE);
    if (field === 'programInput' || field === 'pipInput')
      return this.log(`${SDK_CALL[field]}(${value})  // ${inputById(value).name}`);
    const arg = typeof value === 'boolean'
      ? (value ? 'TRUE' : 'FALSE')
      : Number(value).toFixed(LIMITS[field] ? LIMITS[field].decimals : 2);
    this.log(`${SDK_CALL[field]}(${arg})`);
  }

  log(text, kind = 'cmd') {
    this.dispatchEvent(new CustomEvent('log', { detail: { text, kind } }));
  }

  // A change made somewhere else (ATEM Software Control, a macro, the
  // hardware buttons). Arrives like an SDK callback → pipChanged.
  device(partial, note) {
    if (!this.connected) return;
    Object.assign(this.state, partial);
    this.log(`device: ${note || JSON.stringify(partial)}`, 'dev');
    this.emit('device');
  }

  setConnected(on) {
    this.connected = on;
    this.log(on ? 'device: connected' : 'device: connection lost', 'dev');
    this.emit('connection');
  }

  emit(source) {
    this.dispatchEvent(new CustomEvent('change', { detail: { source } }));
  }
}

// ── Preview ─────────────────────────────────────────────────
// Draws program output: main input full frame, PiP box on top.
// Position is the box centre; +Y is up (verify on the device).

function pipRect(s) {
  const w = FRAME.w * s.sizeX;
  const h = FRAME.h * s.sizeY;
  let x0 = s.positionX - w / 2;
  let x1 = s.positionX + w / 2;
  let yTop = s.positionY + h / 2;
  let yBot = s.positionY - h / 2;
  if (s.cropEnabled) {
    x0 += s.cropLeft * s.sizeX;
    x1 -= s.cropRight * s.sizeX;
    yTop -= s.cropTop * s.sizeY;
    yBot += s.cropBottom * s.sizeY;
  }
  return {
    left: ((x0 + FRAME.w / 2) / FRAME.w) * 100,
    top: ((FRAME.h / 2 - yTop) / FRAME.h) * 100,
    width: (Math.max(0, x1 - x0) / FRAME.w) * 100,
    height: (Math.max(0, yTop - yBot) / FRAME.h) * 100,
  };
}

function createPreview(container) {
  container.classList.add('preview');
  container.innerHTML = '<div class="main"></div><div class="safe"></div><div class="pip"></div><div class="tag"></div>';
  return {
    root: container,
    main: container.querySelector('.main'),
    pip: container.querySelector('.pip'),
    tag: container.querySelector('.tag'),
  };
}

function renderPreview(preview, model) {
  const s = model.state;
  const main = inputById(s.programInput);
  const pip = inputById(s.pipInput);
  const mainImage = model.imageFor(main.id);
  const pipImage = model.imageFor(pip.id);
  paintInput(preview.main, main, mainImage);
  preview.main.textContent = mainImage ? '' : main.name.toUpperCase();
  paintInput(preview.pip, pip, pipImage);
  preview.pip.textContent = pipImage ? '' : pip.short;
  const r = pipRect(s);
  Object.assign(preview.pip.style, {
    left: r.left + '%',
    top: r.top + '%',
    width: r.width + '%',
    height: r.height + '%',
    display: s.isDVE ? 'flex' : 'none',
  });
  if (pipImage) {
    // The box shows the cropped part of the full source picture: size the
    // background to the uncropped box and shift it by the left/top crop.
    const pw = preview.root.clientWidth, ph = preview.root.clientHeight;
    const fullW = s.sizeX * pw, fullH = s.sizeY * ph;
    const c = s.cropEnabled ? s : { cropLeft: 0, cropTop: 0 };
    preview.pip.style.backgroundSize = `${fullW}px ${fullH}px`;
    preview.pip.style.backgroundPosition = `${-(c.cropLeft / FRAME.w) * fullW}px ${-(c.cropTop / FRAME.h) * fullH}px`;
  }
  preview.pip.classList.toggle('off-air', !s.onAir);
  preview.tag.textContent = s.onAir ? 'PROGRAM' : 'PROGRAM · PiP off air (outline shows where it would be)';
}

// ── Slider + number row (the Qt PipValueControl) ──────────

// onEdit(value) runs after every operator edit (e.g. to keep sizeY locked to sizeX).
function createValueRow(model, field, label, onEdit) {
  const l = LIMITS[field];
  const row = document.createElement('div');
  row.className = 'value-row';
  const id = 'f-' + field + '-' + Math.random().toString(36).slice(2, 7);
  row.innerHTML =
    `<label for="${id}">${label}</label>` +
    `<input type="range" aria-label="${label}" min="${l.slider[0]}" max="${l.slider[1]}" step="${l.step}">` +
    `<input type="number" id="${id}" step="${l.step * 10}">`;
  const slider = row.querySelector('input[type=range]');
  const num = row.querySelector('input[type=number]');
  let dragging = false;

  slider.addEventListener('pointerdown', () => (dragging = true));
  window.addEventListener('pointerup', () => (dragging = false));
  slider.addEventListener('input', () => {
    num.value = Number(slider.value).toFixed(l.decimals);
    model.edit(field, Number(slider.value));
    if (onEdit) onEdit(model.get(field));
  });
  // Like QDoubleSpinBox with keyboardTracking(false): commit on Enter / blur.
  num.addEventListener('change', () => {
    model.edit(field, Number(num.value));
    if (onEdit) onEdit(model.get(field));
  });
  num.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      e.preventDefault();
      const step = e.shiftKey ? 1 : l.step * 10;
      model.edit(field, model.get(field) + (e.key === 'ArrowUp' ? step : -step));
      num.value = Number(model.get(field)).toFixed(l.decimals);
      if (onEdit) onEdit(model.get(field));
    }
  });

  const update = () => {
    const lim = model.limitsFor(field);
    num.min = lim.spin[0];
    num.max = lim.spin[1];
    const editing = dragging || document.activeElement === num;
    if (editing) return; // never fight the operator
    const v = model.get(field);
    slider.value = v;
    num.value = Number(v).toFixed(l.decimals);
  };
  update();
  return { row, update, setLabel: (t) => (row.querySelector('label').textContent = t) };
}

// ── Header ──────────────────────────────────────────────────

// Returns the header's buttons; { settings: true } adds a ⚙ button.
function renderHeader(el, model, options = {}) {
  el.className = 'panel-header';
  el.innerHTML =
    '<span class="dot" aria-hidden="true">●</span><span class="title">ATEM PIP</span>' +
    '<span class="status"></span><button class="tool-btn" data-btn="reload" title="Reload PiP settings from the ATEM">⟳</button>' +
    (options.settings ? '<button class="tool-btn" data-btn="settings" title="PiP settings">⚙</button>' : '');
  const dot = el.querySelector('.dot');
  const status = el.querySelector('.status');
  const update = () => {
    dot.classList.toggle('off', !model.connected);
    status.textContent = model.connected ? 'ATEM Mini' : 'disconnected';
  };
  update();
  model.addEventListener('change', update);
  return { reload: el.querySelector('[data-btn=reload]'), settings: el.querySelector('[data-btn=settings]') };
}

// ── Test bench (right-hand side of every mockup page) ───────

function buildBench(benchEl, model, dockFrame) {
  benchEl.innerHTML = `
    <section>
      <h2>Dock width</h2>
      <div class="row">
        <input type="range" id="dockWidth" min="240" max="460" value="320" style="max-width:260px" aria-label="Dock width">
        <span id="dockWidthLabel">320 px</span>
      </div>
    </section>
    <section>
      <h2>Simulate changes made elsewhere</h2>
      <div class="row" id="deviceButtons"></div>
    </section>
    <section>
      <h2>SDK calls this panel would send <button id="clearLog" style="margin-left:8px">Clear</button></h2>
      <pre class="log" id="log" aria-live="polite"></pre>
    </section>`;

  const width = benchEl.querySelector('#dockWidth');
  const widthLabel = benchEl.querySelector('#dockWidthLabel');
  width.addEventListener('input', () => {
    dockFrame.style.setProperty('--dock-w', width.value + 'px');
    widthLabel.textContent = width.value + ' px';
  });

  const deviceActions = [
    ['PiP → top-left', () => model.device({ positionX: -9.6, positionY: 5.1 }, 'PiP moved to top-left (ATEM Software Control)')],
    ['Main → Camera 3', () => model.device({ programInput: 3 }, 'program input changed to Camera 3')],
    ['Main → Black', () => model.device({ programInput: 0 }, 'program input changed to Black (no camera button lit)')],
    ['PiP off air', () => model.device({ onAir: false }, 'key taken off air (hardware button)')],
    ['Size 0.5', () => model.device({ sizeX: 0.5, sizeY: 0.5 }, 'size set to 0.5 by a macro')],
    ['Crop on', () => model.device({ cropEnabled: true, cropLeft: 6, cropRight: 6 }, 'crop enabled, left/right 6')],
    ['Crop off', () => model.device({ cropEnabled: false, cropLeft: 0, cropRight: 0, cropTop: 0, cropBottom: 0 }, 'crop reset')],
    ['Key not DVE', () => model.device({ isDVE: false }, 'upstream key changed to luma')],
    ['Drop / restore connection', () => model.setConnected(!model.connected)],
  ];
  const buttons = benchEl.querySelector('#deviceButtons');
  for (const [label, fn] of deviceActions) {
    const b = document.createElement('button');
    b.textContent = label;
    b.addEventListener('click', fn);
    buttons.appendChild(b);
  }

  const log = benchEl.querySelector('#log');
  benchEl.querySelector('#clearLog').addEventListener('click', () => (log.textContent = ''));
  model.addEventListener('log', (e) => {
    const line = document.createElement('div');
    const t = new Date().toLocaleTimeString([], { hour12: false }) + '.' + String(Date.now() % 1000).padStart(3, '0');
    line.innerHTML = `<span class="t">${t}</span> `;
    const text = document.createElement('span');
    text.textContent = e.detail.text;
    if (e.detail.kind === 'dev') text.className = 'dev';
    line.appendChild(text);
    log.prepend(line);
    while (log.childNodes.length > 200) log.lastChild.remove();
  });
}
