// RadioBerry browser experiment. Local preview or board-generated spectrum.
// Built together with the attributed UberSDR helpers; see ../README.md.
(() => {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const spectrum = $('spectrum');
  const waterfall = $('waterfall');
  const BIN_COUNT = 512;
  const ROWS = 200;
  const INTERVAL_MS = 100;
  const MIN_HZ = 100000;
  const MAX_HZ = 30000000;
  const RANGE_DB = 80;
  const boardMode = /^https?:$/.test(location.protocol) && new URLSearchParams(location.search).get('demo') !== '1';
  if (!boardMode) $('iq-link').href = 'iq-probe.html?demo=1';
  if (!boardMode && window.radioBerryPCMProbe) {
    window.radioBerryPCMProbe.available = false;
    window.radioBerryPCMProbe.message = 'The test tone is available on the board.';
    window.radioBerryPCMProbe.update();
  }
  let client, connection = 'connecting', boardConfig = null, captureMs = null, timeRemainder = 0;
  let frequencyEdited = false;
  const state = { frequency: 7100000, reference: 7100000, span: 48000,
    floor: -110, palette: 'ice', paused: false, time: 0, sequence: 0 };
  const ring = document.createElement('canvas');
  ring.width = BIN_COUNT;
  ring.height = ROWS;
  const ringCtx = ring.getContext('2d', { alpha: false });
  const scanline = ringCtx.createImageData(BIN_COUNT, 1);
  let head = 0;
  let rowCount = 0;
  let currentFrame;
  let pixelBins = new Float32Array(0);
  const hzFormatter = new Intl.NumberFormat('en-GB', { maximumFractionDigits: 0 });

  function clearHistory() {
    head = 0;
    rowCount = 0;
    captureMs = null; timeRemainder = 0;
    ringCtx.fillStyle = '#0d1518';
    ringCtx.fillRect(0, 0, BIN_COUNT, ROWS);
  }

  // Low-to-high relative dB levels. Each value represents the bin centre.
  function makeFrame() {
    const bins = new Float32Array(BIN_COUNT);
    const startHz = state.frequency - state.span / 2;
    const binWidth = state.span / BIN_COUNT;
    const movingHz = state.reference + 4200 + 1300 * Math.sin(state.time / 6);
    const power = (db) => Math.pow(10, db / 10);
    const bell = (hz, centre, width, level) => power(level) * Math.exp(-0.5 * ((hz - centre) / width) ** 2);
    for (let i = 0; i < BIN_COUNT; i++) {
      const hz = startHz + (i + 0.5) * binWidth;
      const noise = -103 + 3 * Math.sin(hz * 0.017 + state.time * 3.1)
        + 2 * Math.sin(hz * 0.071 - state.time * 4.3);
      let p = power(noise);
      p += bell(hz, state.reference - 6800, 100, -43);
      p += bell(hz, movingHz, 100, -35 + 2 * Math.sin(state.time));
      p += bell(hz, state.reference + 12600, 950, -61);
      p += bell(hz, state.reference + 12600, 75, -47);
      bins[i] = 10 * Math.log10(p);
    }
    return { bins, centerFreq: state.frequency, span: state.span,
      sequence: ++state.sequence, time: state.time, units: 'relative-dB' };
  }

  function pushRow(frame) {
    const lut = getPalette(state.palette);
    for (let i = 0; i < BIN_COUNT; i++) {
      const t = Math.max(0, Math.min(1, (frame.bins[i] - state.floor) / RANGE_DB));
      const color = Math.round(t * 255) * 3;
      const offset = i * 4;
      scanline.data[offset] = lut[color];
      scanline.data[offset + 1] = lut[color + 1];
      scanline.data[offset + 2] = lut[color + 2];
      scanline.data[offset + 3] = 255;
    }
    head = (head - 1 + ROWS) % ROWS;
    ringCtx.putImageData(scanline, 0, head);
    rowCount = Math.min(ROWS, rowCount + 1);
  }

  function canvasSize(canvas) {
    const rect = canvas.getBoundingClientRect();
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const width = Math.max(1, Math.round(rect.width * dpr));
    const height = Math.max(1, Math.round(rect.height * dpr));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    const ctx = canvas.getContext('2d', { alpha: false });
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return { ctx, width: rect.width, height: rect.height, dpr,
      left: rect.width < 550 ? 48 : 56, right: 18 };
  }

  function drawSpectrum(frame) {
    const { ctx: c, width, height, dpr, left, right } = canvasSize(spectrum);
    const top = 24;
    const bottom = height - 36;
    const plotW = width - left - right;
    const plotH = bottom - top;
    c.fillStyle = '#0d1518'; c.fillRect(0, 0, width, height);
    c.font = '10px system-ui, sans-serif'; c.lineWidth = 1; c.textAlign = 'right';
    c.fillStyle = '#819b9e'; c.fillText('dB rel.', left - 9, 14);
    const mode = $('audio-mode').value;
    const passband = ({usb: [300, 2700], lsb: [-2700, -300], am: [-4000, 4000]})[mode];
    $('passband-key').hidden = !passband;
    $('passband-label').textContent = passband ? `Passband ${mode.toUpperCase()} · ${
      ({usb: '+300 to +2700 Hz', lsb: '−2700 to −300 Hz', am: '−4 to +4 kHz'})[mode]}` :
      'Test tone · no receive filter';
    if (passband) {
      const xAt = offset => left + (.5 + (state.frequency + offset - frame.centerFreq) / frame.span) * plotW;
      const start = xAt(passband[0]), end = xAt(passband[1]);
      const clippedStart = Math.max(left, start), clippedEnd = Math.min(width - right, end);
      if (clippedEnd > clippedStart) {
        c.fillStyle = '#8cdbd21c'; c.fillRect(clippedStart, top, clippedEnd - clippedStart, plotH);
        c.strokeStyle = '#8cdbd2aa'; c.lineWidth = 1;
        c.beginPath();
        for (const edge of [start, end]) if (edge >= left && edge <= width - right) {
          c.moveTo(edge, top); c.lineTo(edge, bottom);
        }
        c.moveTo(clippedStart, top); c.lineTo(clippedEnd, top); c.stroke();
      }
    }
    for (let db = Math.ceil(state.floor / 20) * 20; db <= state.floor + RANGE_DB; db += 20) {
      const y = bottom - (db - state.floor) / RANGE_DB * plotH;
      c.strokeStyle = '#203035';
      c.beginPath(); c.moveTo(left, y); c.lineTo(width - right, y); c.stroke();
      c.fillText(String(db), left - 9, y + 3);
    }
    // The upstream helper aims for 110 px per label. Our short kHz labels
    // fit in 70 px, so a phone also shows useful labels beside the centre.
    const ticks = frequencyTicks(frame, plotW * 110 / 70).ticks;
    c.textAlign = 'center';
    for (const tick of ticks) {
      const x = left + tick.frac * plotW;
      c.strokeStyle = tick.major ? '#25363b' : '#1a292d';
      c.beginPath(); c.moveTo(x, tick.major ? top : bottom - 4); c.lineTo(x, bottom + 4); c.stroke();
      if (tick.major) {
        const label = (tick.hz / 1000).toLocaleString('en-GB', { maximumFractionDigits: 1 });
        const half = c.measureText(label).width / 2;
        if (x - half >= left - 3 && x + half <= width - right + 3) c.fillText(label, x, bottom + 19);
      }
    }
    const count = Math.max(1, Math.round(plotW * dpr));
    if (pixelBins.length !== count) pixelBins = new Float32Array(count);
    binsToPixels(frame.bins, count, pixelBins);
    const yAt = (v) => Math.max(top, Math.min(bottom, bottom - (v - state.floor) / RANGE_DB * plotH));
    c.beginPath(); c.moveTo(left, bottom);
    for (let i = 0; i < count; i++) c.lineTo(left + (i + 0.5) / count * plotW, yAt(pixelBins[i]));
    c.lineTo(width - right, bottom); c.closePath();
    const fill = c.createLinearGradient(0, top, 0, bottom);
    fill.addColorStop(0, '#b4e7bb33'); fill.addColorStop(1, '#b4e7bb03');
    c.fillStyle = fill; c.fill(); c.beginPath();
    for (let i = 0; i < count; i++) {
      const x = left + (i + 0.5) / count * plotW;
      if (i === 0) c.moveTo(x, yAt(pixelBins[i])); else c.lineTo(x, yAt(pixelBins[i]));
    }
    c.strokeStyle = '#b4e7bb'; c.lineWidth = 1.25; c.stroke();
    const centre = left + (0.5 + (state.frequency - frame.centerFreq) / frame.span) * plotW;
    c.setLineDash([3, 4]); c.strokeStyle = '#edbb7580'; c.lineWidth = 1;
    c.beginPath(); c.moveTo(centre, top); c.lineTo(centre, bottom); c.stroke(); c.setLineDash([]);
    c.fillStyle = '#edbb75';
    c.beginPath(); c.moveTo(centre - 4, top - 8); c.lineTo(centre + 4, top - 8); c.lineTo(centre, top - 2); c.fill();
    c.fillStyle = '#819b9e'; c.textAlign = 'right'; c.fillText('kHz', width - right, height - 3);
  }

  function drawWaterfall() {
    const { ctx: c, width, height, left, right } = canvasSize(waterfall);
    c.fillStyle = '#0d1518'; c.fillRect(0, 0, width, height);
    const plotH = height - 12;
    c.imageSmoothingEnabled = false;
    for (const slice of ringSlices(head, ROWS, ROWS)) {
      c.drawImage(ring, 0, slice.sy, BIN_COUNT, slice.sh,
        left, slice.dy / ROWS * plotH, width - left - right, slice.sh / ROWS * plotH);
    }
    c.font = '10px system-ui, sans-serif'; c.textAlign = 'right'; c.fillStyle = '#819b9e';
    for (const seconds of [0, 5, 10, 15]) c.fillText(seconds === 0 ? 'now' : `−${seconds}s`, left - 9, 10 + seconds / 20 * plotH);
    $('history-status').textContent = `${(rowCount / 10).toFixed(1)} s history`;
  }

  function draw() {
    drawSpectrum(currentFrame || { bins: new Float32Array(512).fill(state.floor),
      centerFreq: state.frequency - (boardMode ? 46.875 : 0), span: state.span });
    drawWaterfall();
    if (!currentFrame) { $('peak-value').innerHTML = '— <small>dB rel.</small>'; return; }
    const peak = Math.max(...currentFrame.bins);
    $('peak-value').innerHTML = `${peak.toFixed(1)} <small>dB rel.</small>`;
  }

  function nextFrame(advance = false) {
    if (boardMode) { draw(); return; }
    if (advance) state.time += INTERVAL_MS / 1000;
    currentFrame = makeFrame(); pushRow(currentFrame); draw();
  }

  function updateFrequencyUi(discardEdit = false) {
    if (discardEdit) frequencyEdited = false;
    // Status polling also reports gain/AGC changes. Keep an unfinished entry,
    // its selection and validation until submitted or explicitly cancelled.
    if (discardEdit || (!frequencyEdited && (!client || client.target(null) === null))) {
      const formatted = (state.frequency / 1000).toFixed(3);
      if ($('frequency').value !== formatted) $('frequency').value = formatted;
      $('frequency').removeAttribute('aria-invalid'); $('frequency-error').hidden = true;
    }
    $('tuned-value').innerHTML = `${hzFormatter.format(state.frequency)} <small>Hz</small>`;
    document.querySelectorAll('[data-preset]').forEach((b) => {
      b.setAttribute('aria-pressed', String(Number(b.dataset.preset) === state.frequency));
    });
  }

  function tune(hz) {
    if (!Number.isFinite(hz) || hz < MIN_HZ || hz > MAX_HZ) return false;
    const rounded = Math.round(hz);
    frequencyEdited = false;
    if (boardMode) { client.tune(rounded); return true; }
    if (rounded !== state.frequency) {
      state.frequency = rounded; clearHistory(); nextFrame();
    }
    updateFrequencyUi(); return true;
  }

  $('tune-form').addEventListener('submit', (event) => {
    event.preventDefault();
    const text = $('frequency').value.trim();
    // One decimal separator, comma or dot; no ambiguous digit grouping.
    const hz = /^\d+(?:[.,]\d{1,3})?$/.test(text) ? Number(text.replace(',', '.')) * 1000 : NaN;
    if (!tune(hz)) {
      frequencyEdited = true;
      $('frequency').setAttribute('aria-invalid', 'true');
      $('frequency-error').textContent = 'Enter 100–30000 kHz with up to three decimal places (dot or comma).';
      $('frequency-error').hidden = false;
    }
  });
  $('frequency').addEventListener('input', () => { frequencyEdited = true; });
  $('frequency').addEventListener('keydown', (e) => { if (e.key === 'Escape') updateFrequencyUi(true); });
  function step(direction) { tune(Math.max(MIN_HZ, Math.min(MAX_HZ, (client ? client.target(state.frequency) : state.frequency) + direction * Number($('step').value)))); }
  $('step-down').addEventListener('click', () => step(-1));
  $('step-up').addEventListener('click', () => step(1));
  spectrum.addEventListener('keydown', (e) => {
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') { e.preventDefault(); step(e.key === 'ArrowLeft' ? -1 : 1); }
  });
  spectrum.addEventListener('click', (e) => {
    const rect = spectrum.getBoundingClientRect();
    const left = rect.width < 550 ? 48 : 56;
    const x = e.clientX - rect.left - left;
    const plotW = rect.width - left - 18;
    if (x >= 0 && x <= plotW && e.clientY - rect.top >= 24 && e.clientY - rect.top <= rect.height - 36) {
      tune((currentFrame ? currentFrame.centerFreq : state.frequency) + (x / plotW - 0.5) * state.span);
    }
  });
  document.querySelectorAll('[data-preset]').forEach((button) => button.addEventListener('click', () => {
    if (boardMode) { tune(Number(button.dataset.preset)); return; }
    state.reference = Number(button.dataset.preset); state.time = 0; state.frequency = state.reference;
    clearHistory(); nextFrame(); updateFrequencyUi(true);
  }));
  $('span').addEventListener('change', () => {
    state.span = Number($('span').value); clearHistory(); nextFrame();
  });
  $('audio-mode').addEventListener('change', draw);
  $('floor').addEventListener('input', () => {
    state.floor = Number($('floor').value); $('floor-value').value = `${state.floor} dB`;
    clearHistory(); nextFrame();
  });
  $('rx-gain').addEventListener('input', () => {
    if (!boardMode || $('rx-gain').disabled) return;
    const gain = Number($('rx-gain').value);
    $('gain-value').value = `${gain > 0 ? '+' : ''}${gain} dB`;
    $('gain-status').textContent = 'Applying…';
    client.setGain(gain);
  });
  function setAgc() {
    if(!boardMode||!client||$('agc-mode').disabled)return;
    const mode=Number($('agc-mode').value),threshold=Number($('agc-threshold').value);
    $('agc-threshold').disabled=mode===0;$('agc-value').value=`${threshold} dBFS`;
    $('agc-status').textContent='Applying…';client.setAgc(mode,threshold);
  }
  $('agc-mode').addEventListener('change',setAgc);
  $('agc-threshold').addEventListener('input',setAgc);
  $('palette').addEventListener('change', () => {
    state.palette = $('palette').value; clearHistory(); nextFrame();
  });
  function setPaused(paused) {
    state.paused = paused;
    $('pause').setAttribute('aria-pressed', String(paused));
    $('pause').textContent = paused ? 'Resume' : 'Pause';
    $('run-state').textContent = paused ? 'Paused' : boardMode ? connectionText() : 'Active';
    $('running-dot').style.color = paused ? '#edbb75' : '#b4e7bb';
    if (boardMode && !paused && client) client.resume();
  }
  $('pause').addEventListener('click', () => setPaused(!state.paused));
  $('reset').addEventListener('click', () => {
    Object.assign(state, { frequency: boardMode ? state.frequency : 7100000, reference: 7100000, span: 48000,
      floor: -110, palette: 'ice', time: 0, sequence: 0 });
    $('span').value = '48000'; $('floor').value = '-110'; $('floor-value').value = '−110 dB';
    $('palette').value = 'ice'; $('step').value = '1000';
    setPaused(false); clearHistory(); updateFrequencyUi(true); nextFrame();
    if (boardMode) tune(7100000);
  });
  function connectionText() {
    return ({ connecting: 'Connecting…', connected: 'Board connected', waiting: 'Waiting for reception…',
      offline: 'Disconnected', tuning: 'Tuning…', gain: 'Applying RX gain…',agc:'Applying AGC…' })[connection];
  }
  function receiveStatus(s) {
    window.radioBerryPCMProbe?.setConfig(s);
    const key = `${s.stream_id}:${s.config_id}`;
    if (key !== boardConfig) { clearHistory(); currentFrame = null; boardConfig = key; }
    state.frequency = s.frequency_hz;
    const gain = client ? client.gainTarget(s.rx_gain_db) : s.rx_gain_db;
    $('rx-gain').disabled = false;
    $('rx-gain').value = String(gain);
    $('gain-value').value = `${gain > 0 ? '+' : ''}${gain} dB`;
    $('gain-status').textContent = gain === s.rx_gain_db ? 'AD9866 VGA' : 'Applying…';
    const supportsAgc=Number.isInteger(s.agc_mode);
    $('agc-mode').disabled=!supportsAgc;
    if(supportsAgc) {
      const agc=client?client.agcTarget(s):s;
      $('agc-mode').value=String(agc.agc_mode);$('agc-threshold').value=String(agc.agc_threshold_dbfs);
      $('agc-value').value=`${agc.agc_threshold_dbfs} dBFS`;$('agc-threshold').disabled=agc.agc_mode===0;
      $('agc-status').textContent=agc.agc_mode!==s.agc_mode||agc.agc_threshold_dbfs!==s.agc_threshold_dbfs?'Applying…':
        s.agc_mode===0?'AGC off · fixed gain':'Confirmed by the board';
    } else {$('agc-threshold').disabled=true;$('agc-status').textContent='AGC controls are unavailable on this board';}
    $('source-badge').textContent = s.synthetic ? 'ETHERNET · DEMO' : 'ETHERNET · LIVE';
    $('iq-link').href = s.synthetic ? 'iq-probe.html?demo=1' : 'iq-probe.html';
    $('source-description').textContent = s.synthetic ?
      'Synthetic spectrum from your board, directly over your local network.' :
      'Radio spectrum from your RadioBerry, directly over your local network.';
    updateFrequencyUi();
    draw();
  }
  function receiveFrame(frame) {
    if (captureMs !== null) {
      const elapsed = (frame.captureMs - captureMs) >>> 0;
      if (elapsed >= 0x80000000 || frame.inputGap) clearHistory();
      else {
        timeRemainder += elapsed;
        captureMs = frame.captureMs;
        const rows = Math.floor(timeRemainder / INTERVAL_MS);
        if (!rows) { currentFrame = frame; draw(); return; }
        timeRemainder -= rows * INTERVAL_MS;
        if (rows >= ROWS) clearHistory();
        else for (let i = 1; i < rows; i++) {
          head = (head - 1 + ROWS) % ROWS;
          ringCtx.fillStyle = '#0d1518'; ringCtx.fillRect(0, head, BIN_COUNT, 1);
          rowCount = Math.min(ROWS, rowCount + 1);
        }
      }
    }
    captureMs = frame.captureMs; currentFrame = frame; pushRow(frame); draw();
  }
  function receiveConnection(value) {
    connection = value;
    if (!state.paused) $('run-state').textContent = connectionText();
    $('running-dot').style.color = value === 'connected' ? '#b4e7bb' : '#edbb75';
    if (value === 'offline' || value === 'connecting') {
      $('rx-gain').disabled = true;
      $('gain-value').value = '— dB';
      $('gain-status').textContent = 'Disconnected';
      $('agc-mode').disabled=$('agc-threshold').disabled=true;$('agc-status').textContent='Disconnected';
      boardConfig = null; currentFrame = null; clearHistory();
      draw();
      $('tuned-value').innerHTML = '— <small>Hz</small>';
    } else if (value === 'waiting') {
      currentFrame = null; clearHistory(); draw();
    }
  }
  const observer = new ResizeObserver(draw);
  observer.observe(spectrum); observer.observe(waterfall);
  clearHistory(); updateFrequencyUi();
  if (boardMode) {
    $('span').disabled = true;
    $('source-badge').textContent = 'ETHERNET';
    $('source-description').textContent = 'Connected to your RadioBerry over your local network.';
    client = createBoardClient({ onStatus: receiveStatus, onFrame: receiveFrame,
      onConnection: receiveConnection, isPaused: () => state.paused });
    document.addEventListener('visibilitychange', () => { if (!document.hidden) client.resume(); });
  } else {
    $('gain-status').textContent = 'Requires a board';
    $('agc-status').textContent='AGC controls are available on the board';
    nextFrame();
    setInterval(() => { if (!state.paused && !document.hidden) nextFrame(true); }, INTERVAL_MS);
  }
})();
