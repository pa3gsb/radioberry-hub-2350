// Board transport for the bounded RBS1 spectrum and radio controls.
// One HTTP request in flight; latest frame only, no accumulating browser queue.
export function decodeBoardFrame(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength !== 560) throw new Error('Frame length');
  const v = new DataView(buffer);
  const u16 = (o) => v.getUint16(o, true);
  const u32 = (o) => v.getUint32(o, true);
  if (u32(0) !== 0x31534252 || v.getUint8(4) !== 1 || u16(6) !== 48) throw new Error('Frame header');
  const flags = v.getUint8(5);
  const synthetic = Boolean(flags & 1);
  if (flags & ~15 || u32(28) !== 48000 || u32(32) !== 48000 ||
      u16(36) !== 512 || v.getInt16(40, true) !== -12750 ||
      u16(42) !== 50 || v.getUint8(47) !== 0 ||
      (synthetic ? u16(38) !== 0 || u16(44) !== 0 || v.getUint8(46) !== 0 :
        u16(38) !== 512 || u16(44) < 1 || v.getUint8(46) !== 1) ||
      u32(16) === 0 || u32(24) < 100000 || u32(24) > 30000000) throw new Error('Frame profile');
  const bins = new Float32Array(512);
  for (let i = 0; i < bins.length; i++) bins[i] = -127.5 + v.getUint8(48 + i) * .5;
  const frequency = u32(24);
  return { bins, frequency, centerFreq: frequency - 48000 / 512 / 2,
    span: 48000, sequence: u32(8), configId: u32(12), streamId: u32(16),
    captureMs: u32(20), time: u32(20) / 1000, inputGap: Boolean(flags & 2),
    synthetic, averages: u16(44), units: 'relative-dB' };
}

export function validateBoardStatus(s) {
  if (!s || !Number.isInteger(s.frequency_hz) || s.frequency_hz < 100000 || s.frequency_hz > 30000000 ||
      !Number.isInteger(s.stream_id) || s.stream_id <= 0 || s.stream_id > 0xffffffff ||
      !Number.isInteger(s.config_id) || s.config_id < 0 || s.config_id > 0xffffffff ||
      typeof s.synthetic !== 'boolean' || s.span_hz !== 48000 || s.sample_rate_hz !== 48000 ||
      s.bin_count !== 512 || s.frame_bytes !== 560 || s.interval_ms !== 100 ||
      s.min_frequency_hz !== 100000 || s.max_frequency_hz !== 30000000 ||
      !Number.isInteger(s.rx_gain_db) || s.rx_gain_db < -12 || s.rx_gain_db > 48 ||
      s.min_rx_gain_db !== -12 || s.max_rx_gain_db !== 48) throw new Error('Board profile');
  if ('agc_mode' in s || 'agc_threshold_dbfs' in s || 'agc_revision' in s) {
    if(!Number.isInteger(s.agc_mode)||s.agc_mode<0||s.agc_mode>3||
       !Number.isInteger(s.agc_threshold_dbfs)||s.agc_threshold_dbfs<-100||s.agc_threshold_dbfs>-20||
       !Number.isInteger(s.agc_revision)||s.agc_revision<0||s.agc_revision>0xffffffff)throw new Error('AGC profile');
  }
  return s;
}

export function createBoardClient({ onStatus, onFrame, onConnection, isPaused }) {
  let confirmed = null, desired = null, desiredGain = null, generation = 0, lastSequence = null;
  let needsStatus = true, failures = 0, waitingForFrame = true;
  let desiredAgc = null,statusAt = 0;
  const delay = (ms) => new Promise(resolve => setTimeout(resolve, ms));
  async function request(path, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2500);
    try {
      const r = await fetch(path, { ...options, signal: controller.signal, cache: 'no-store' });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const type = r.headers.get('Content-Type') || '';
      if (path === '/api/spectrum') {
        if (r.status === 204) return null; /* No fresh RX window yet. */
        if (!type.startsWith('application/octet-stream')) throw new Error('Spectrum content type');
        return decodeBoardFrame(await r.arrayBuffer());
      }
      if (!type.startsWith('application/json')) throw new Error('Status content type');
      return validateBoardStatus(await r.json());
    } finally { clearTimeout(timeout); }
  }
  function acceptStatus(s) {
    const changed = !confirmed || s.stream_id !== confirmed.stream_id || s.config_id !== confirmed.config_id;
    if (changed) { lastSequence = null; waitingForFrame = true; }
    confirmed = s; needsStatus = false; statusAt=performance.now();onStatus(s);
    onConnection(s.synthetic || !waitingForFrame ? 'connected' : 'waiting');
  }
  async function run() {
    for (;;) {
      if (document.hidden || (isPaused()&&desired===null&&desiredGain===null&&desiredAgc===null)) { await delay(100); continue; }
      const started = performance.now();
      const g = generation;
      try {
        if (needsStatus || (desired===null&&desiredGain===null&&desiredAgc===null&&performance.now()-statusAt>2000)) {
          const s = await request('/api/status');
          if (g === generation) acceptStatus(s);
        } else if (desired !== null) {
          const frequency = desired;
          const s = await request('/api/tune', { method: 'POST',
            headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ frequency_hz: frequency }) });
          if (g === generation) { desired = null; acceptStatus(s); }
        } else if (desiredGain !== null) {
          const gain = desiredGain;
          const s = await request('/api/gain', { method: 'POST',
            headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ rx_gain_db: gain }) });
          if (g === generation) { desiredGain = null; acceptStatus(s); }
        } else if(desiredAgc!==null) {
          const s=await request('/api/agc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(desiredAgc)});
          if(g===generation){desiredAgc=null;acceptStatus(s);}
        } else {
          const f = await request('/api/spectrum');
          if (g === generation && !isPaused() && !document.hidden) {
            if (!f) { waitingForFrame = true; needsStatus = true; onConnection('waiting'); }
            else if (f.synthetic !== confirmed.synthetic) throw new Error('Spectrum source mismatch');
            else if (f.streamId !== confirmed.stream_id || f.configId !== confirmed.config_id ||
                f.frequency !== confirmed.frequency_hz) needsStatus = true;
            else {
              const delta = lastSequence === null ? 1 : (f.sequence - lastSequence) >>> 0;
              if (delta && delta < 0x80000000) {
                waitingForFrame = false; lastSequence = f.sequence; onFrame(f); onConnection('connected');
              }
            }
          }
        }
        failures = 0;
      } catch (error) {
        confirmed = null; needsStatus = true; lastSequence = null;
        failures++; onConnection('offline');
      }
      await delay(failures ? Math.min(2000, 500 * failures) : Math.max(0, 100 - (performance.now() - started)));
    }
  }
  onConnection('connecting');
  run();
  return {
    tune(frequency) { desired = frequency; generation++; onConnection('tuning'); },
    setGain(gain) {
      if (!Number.isInteger(gain) || gain < -12 || gain > 48) return;
      desiredGain = gain; generation++; onConnection('gain');
    },
    target(fallback) { return desired === null ? fallback : desired; },
    gainTarget(fallback) { return desiredGain === null ? fallback : desiredGain; },
    setAgc(mode,threshold) {
      if(!Number.isInteger(mode)||mode<0||mode>3||!Number.isInteger(threshold)||threshold<-100||threshold>-20)return;
      desiredAgc={agc_mode:mode,agc_threshold_dbfs:threshold};generation++;onConnection('agc');
    },
    agcTarget(fallback) { return desiredAgc===null?fallback:desiredAgc; },
    resume() { needsStatus = true; generation++; }
  };
}
