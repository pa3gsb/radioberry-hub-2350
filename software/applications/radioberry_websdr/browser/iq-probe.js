'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const demo = location.protocol === 'file:' || new URLSearchParams(location.search).get('demo') === '1';
  const state = {running:false, frames:0, gaps:0, missing:0n, samples:0, reconnections:0, last:null,
    rate:null, started:0, rateAt:0, rateSamples:0, diagnostics:null};
  let controller = null, watchdog = null, timer = null, epoch = 0, decoder = null, diagnosticBusy = false;
  let demoSample = 0, demoSequence = 0, demoAt = 0, demoConfig = 1, demoGap = false;
  function message(text) { $('status').textContent = text; }
  function receive(frame) {
    state.last = frame; ++state.frames; state.samples += 512;
    if (frame.gap) ++state.gaps;
    state.missing += frame.missingSamples;
    message(frame.synthetic ? 'Receiving synthetic IQ data' : 'IQ stream connected to your board');
    if (watchdog) clearTimeout(watchdog);
    if (controller) watchdog = setTimeout(() => controller?.abort(), 3000);
  }
  function draw() {
    const canvas = $('iq-plot'), rect = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    const width = Math.round(rect.width * ratio), height = Math.round(rect.height * ratio);
    if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
    const c = canvas.getContext('2d'); c.clearRect(0, 0, width, height);
    c.strokeStyle = '#2d3b3e'; c.lineWidth = ratio;
    c.beginPath(); c.moveTo(0, height / 2); c.lineTo(width, height / 2); c.stroke();
    if (!state.last) return;
    const data = state.last.samples;
    let amplitude = 0.000001;
    for (let i = 0; i < 256; ++i) amplitude = Math.max(amplitude, Math.abs(data[i]));
    for (let channel = 0; channel < 2; ++channel) {
      c.strokeStyle = channel ? '#7dcae1' : '#b4e7bb'; c.lineWidth = 1.5 * ratio; c.beginPath();
      for (let i = 0; i < 128; ++i) {
        const x = i / 127 * width, y = height / 2 - data[i * 2 + channel] / amplitude * height * .4;
        if (i) c.lineTo(x, y); else c.moveTo(x, y);
      }
      c.stroke();
    }
  }
  function update() {
    const now = performance.now();
    if (state.running && now - state.rateAt >= 1000) {
      state.rate = (state.samples - state.rateSamples) * 1000 / (now - state.rateAt);
      state.rateAt = now; state.rateSamples = state.samples;
    }
    $('rate').textContent = state.running && state.frames && state.rate !== null ? Math.round(state.rate).toLocaleString('en-GB') : '—';
    $('throughput').textContent = state.running && state.frames && state.rate !== null ? (state.rate * 48 / 1000000).toLocaleString('en-GB', {minimumFractionDigits:3, maximumFractionDigits:3}) : '—';
    for (const id of ['frames','gaps','reconnections']) $(id).textContent = state[id].toLocaleString('en-GB');
    $('missing').textContent = state.missing.toString();
    $('start').disabled = state.running; $('stop').disabled = !state.running;
    $('stalls').textContent = state.diagnostics?.rx_stalls ?? '—';
    if (state.last) {
      const f = state.last;
      $('frequency').textContent = `${f.frequency.toLocaleString('en-GB')} Hz`;
      $('sample-counter').textContent = f.firstSample.toString(); $('sequence').textContent = f.sequence;
      $('configuration').textContent = `${f.streamId} / ${f.configId}`;
      let power = 0; for (const sample of f.samples) power += sample * sample;
      const db = power ? 10 * Math.log10(power / 512) : -Infinity;
      $('rms').textContent = Number.isFinite(db) ? `${db.toFixed(1)} dBFS (complex RMS)` : 'Silence';
    }
    draw();
  }
  async function diagnostics() {
    if (demo || diagnosticBusy || !state.running || !controller) return;
    diagnosticBusy = true;
    const current = epoch;
    try {
      const r = await fetch('/api/diagnostics', {cache:'no-store', signal:controller.signal});
      if (r.ok) { const value = await r.json(); if (current === epoch) state.diagnostics = value; }
    } catch {} finally { diagnosticBusy = false; }
  }
  async function stream(current) {
    let attempts = 0;
    while (current === epoch && state.running) {
      const connection = new AbortController(); controller = connection;
      decoder = new RadioBerryIQDecoder(frame => { if (current === epoch) receive(frame); });
      let reader = null;
      watchdog = setTimeout(() => connection.abort(), 3000);
      try {
        const response = await fetch('/api/iq-stream', {cache:'no-store', signal:connection.signal});
        if (!response.ok) {
          if ([400,404,409,503].includes(response.status)) {
            stop(response.status === 409 ? 'The IQ stream is already in use. Stop the other client.' :
              'IQ stream unavailable. Check that the latest WebSDR firmware is loaded.');
            return;
          }
          throw new Error(`HTTP ${response.status}`);
        }
        if (response.headers.get('content-type') !== 'application/x-rbiq' || !response.body)
          throw new RadioBerryIQProtocolError('No valid IQ stream received');
        reader = response.body.getReader();
        while (current === epoch && state.running) {
          const {value,done} = await reader.read();
          if (current !== epoch || !state.running) return;
          if (done) { decoder.finish(); throw new Error('IQ connection closed'); }
          decoder.push(value); attempts = 0;
        }
      } catch (error) {
        if (current !== epoch || !state.running) return;
        if (error instanceof RadioBerryIQProtocolError) { stop(error.message); return; }
        ++state.reconnections;
        message(`Connection interrupted — reconnecting (${error.message})`);
        state.rate = null; state.rateAt = performance.now(); state.rateSamples = state.samples;
      } finally {
        connection.abort();
        if (controller === connection) { clearTimeout(watchdog); watchdog = null; controller = null; }
        if (reader) { try { await reader.cancel(); } catch {} reader.releaseLock(); }
      }
      await new Promise(resolve => setTimeout(resolve, Math.min(4000, 500 * 2 ** attempts++)));
    }
  }
  function makeDemoBlock() {
    const b = new Uint8Array(3120), v = new DataView(b.buffer);
    b.set([82,66,73,49,1,2 | (demoGap ? 1 : 0)]); demoGap = false;
    v.setUint16(6,48,true); v.setUint32(8,3120,true); v.setUint32(12,1,true);
    v.setUint32(16,demoConfig,true); v.setUint32(20,++demoSequence,true);
    v.setBigUint64(24,BigInt(demoSample),true); v.setUint32(32,Math.floor(performance.now()) >>> 0,true);
    v.setUint32(36,7100000,true); v.setUint32(40,48000,true); v.setUint16(44,512,true); b[46]=24;
    for (let i=0;i<512;++i) for (let channel=0;channel<2;++channel) {
      const phase=2*Math.PI*1000*(demoSample+i)/48000;
      const sample=Math.round(.05*8388608*(channel?Math.sin(phase):Math.cos(phase)));
      const at=48+i*6+channel*3; b[at]=sample; b[at+1]=sample>>8; b[at+2]=sample>>16;
    }
    demoSample+=512; return b;
  }
  function demoTick() {
    const due = Math.floor((performance.now()-demoAt)*48/512);
    if (due-demoSequence>8) { demoSample+=(due-demoSequence-1)*512; demoSequence=due-1; demoGap=true; }
    while (demoSequence<due) decoder.push(makeDemoBlock());
  }
  function start() {
    if (state.running) return;
    ++epoch; Object.assign(state,{running:true,frames:0,gaps:0,missing:0n,samples:0,reconnections:0,last:null,rate:null,diagnostics:null});
    state.started = state.rateAt = performance.now(); state.rateSamples = 0;
    message(demo?'Starting synthetic IQ stream…':'Connecting to the board…');
    if (demo) { demoAt=performance.now();demoSample=demoSequence=0;demoConfig=1;demoGap=false;decoder=new RadioBerryIQDecoder(receive); }
    else void stream(epoch);
    let ticks = 0;
    timer = setInterval(() => { if(demo)demoTick(); if(++ticks%4===0)update(); if(ticks%40===0)void diagnostics(); },25);
    update();
  }
  function stop(text='Stream stopped') {
    ++epoch; state.running=false; controller?.abort(); clearTimeout(watchdog); clearInterval(timer);
    message(text); update();
  }
  if (demo) {
    $('badge').textContent='BROWSER DEMO · IQ';
    $('intro').textContent='Synthetic IQ data through the same decoder. Use this to test the display and sample counters on your PC.';
    $('receiver-link').href='index.html?demo=1';
  }
  $('origin').textContent=location.origin;
  $('start').addEventListener('click',start); $('stop').addEventListener('click',()=>stop());
  document.addEventListener('visibilitychange',()=>{if(document.hidden&&state.running)stop('Paused: tab is hidden');});
  window.addEventListener('pagehide',()=>stop()); window.addEventListener('resize',draw);
  window.radioBerryIQProbe={state,start,stop}; update();
})();
