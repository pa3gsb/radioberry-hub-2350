(async () => {
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  const tests = [];
  const check = (condition, label) => { if (!condition) throw new Error(label); tests.push(label); };
  const p = window.radioBerryPCMProbe;
  const mode = window.__pcmSSB || (window.__pcmAM ? 'am' : 'tone'), am = mode !== 'tone';
  const modeFlag = mode === 'usb' ? 4 : mode === 'lsb' ? 8 : 0;
  const endpoint = '/api/audio-' + mode, expectedHz = mode === 'lsb' ? 1900 : am ? 1000 : 600;
  const mime = am ? 'application/x-rbpcm-' + mode : 'application/x-rbpcm', size = am ? 520 : 512;
  if (am && window.__pcmFixture) {
    await fetch('/__test/control', {method:'POST',body:JSON.stringify({real:true})});
    await sleep(300);
  }
  document.getElementById('audio-mode').value = mode;
  // Capture a production C frame; independently check every TCP split point.
  const abort = new AbortController();
  const response = await fetch(endpoint, {signal: abort.signal});
  check(response.ok && response.headers.get('content-type') === mime, 'PCM endpoint and MIME');
  const reader = response.body.getReader(); const bytes = new Uint8Array(size); let used = 0;
  while (used < size) { const {value, done} = await reader.read(); check(!done, 'Frame available');
    const n = Math.min(value.length, size - used); bytes.set(value.subarray(0, n), used); used += n; }
  abort.abort(); await reader.cancel().catch(() => {}); await sleep(300);
  for (let split = 1; split < size; ++split) {
    const frames = [], decoder = new RadioBerryPCMDecoder(frame => frames.push(frame));
    decoder.feed(bytes.subarray(0, split)); decoder.feed(bytes.subarray(split)); decoder.finish();
    check(frames.length === 1 && frames[0].samples.length === 240, `TCP split ${split}`);
  }
  const frames = [], decoder = new RadioBerryPCMDecoder(f => frames.push(f)); decoder.feed(bytes);
  const power = frames[0].samples.reduce((sum, x) => sum + x * x, 0) / 240;
  check(am ? !frames[0].tone && frames[0].frequency >= 100000 && frames[0].config > 0 :
    Math.abs(Math.sqrt(power) - .2 / Math.sqrt(2)) < .001, 'PCM amplitude or AM metadata');
  const malformed = bytes.slice(); malformed[30] = 24;
  let rejected = false; try { new RadioBerryPCMDecoder(() => {}).feed(malformed); } catch { rejected = true; }
  check(rejected, 'Malformed format rejected');
  rejected = false; try { decoder.feed(bytes); } catch { rejected = true; }
  check(rejected, 'Duplicate frame rejected');
  const makeFrame = (sequence, sample, flags = am ? 0 : 2) => {
    const data = bytes.slice(), view = new DataView(data.buffer);
    view.setUint32(8, sequence, true); view.setBigUint64(16, sample, true); data[5] = flags | modeFlag;
    const header = view.getUint16(6, true);
    view.setInt16(header, -32768, true); view.setInt16(header + 2, 32767, true); return data;
  };
  const boundary = [], wrapped = new RadioBerryPCMDecoder(f => boundary.push(f));
  wrapped.feed(makeFrame(0xfffffffe, 0n)); wrapped.feed(makeFrame(0xffffffff, 240n));
  wrapped.feed(makeFrame(0, 480n)); wrapped.feed(makeFrame(3, 1200n, am ? 1 : 3));
  check(boundary[0].samples[0] === -1 && boundary[0].samples[1] === 32767 / 32768,
    'Signed16 boundary values');
  check(boundary[2].missing === 0 && boundary[3].missing === 2 && boundary[3].gap,
    'Sequence wrap and sample-accurate gap detection');
  rejected = false; try { wrapped.feed(makeFrame(4, 0n)); } catch { rejected = true; }
  check(rejected, 'Inconsistent sample counter rejected');
  if (mode === 'usb' || mode === 'lsb') {
    const mixed=bytes.slice();mixed[5]=12;rejected=false;
    try{new RadioBerryPCMDecoder(()=>{}).feed(mixed);}catch{rejected=true;}
    check(rejected,'Conflicting sideband flags rejected');
    const changed=makeFrame(1,240n);changed[5]=mode==='usb'?8:4;
    const fixedMode=new RadioBerryPCMDecoder(()=>{});fixedMode.feed(makeFrame(0,0n));rejected=false;
    try{fixedMode.feed(changed);}catch{rejected=true;}
    check(rejected,'Sideband change within a stream rejected');
  }
  const accept = p.accept.bind(p); let pcmRMS = 0;
  p.accept = frame => { pcmRMS = Math.sqrt(frame.samples.reduce((sum,x)=>sum+x*x,0)/240); accept(frame); };
  p.start(); await sleep(1600);
  check(p.running && p.blocks >= 30 && p.context.state === 'running', 'Actual network PCM playback');
  const busy = await fetch(endpoint); check(busy.status === 409, 'Second listener rejected');
  const analyser = p.context.createAnalyser(); analyser.fftSize = 4096;
  analyser.smoothingTimeConstant = 0;
  const mute = p.context.createGain(); mute.gain.value = 0;
  p.gain.connect(analyser); analyser.connect(mute); mute.connect(p.context.destination);
  await sleep(600); const bins = new Float32Array(analyser.frequencyBinCount); analyser.getFloatFrequencyData(bins);
  let peak = 1; for (let n = 2; n < bins.length; ++n) if (bins[n] > bins[peak]) peak = n;
  const peakHz = peak * p.context.sampleRate / analyser.fftSize;
  check(Math.abs(peakHz - expectedHz) < 40 && bins[peak] > -60,
    'Known tone reaches browser audio graph: ' + JSON.stringify({peakHz,level:bins[peak],pcmRMS,player:p.snapshot(),expected:p.expectedConfig,badge:document.getElementById('source-badge')?.textContent}));
  let rejectionDb = null;
  if (mode === 'usb' || mode === 'lsb') {
    const unwantedHz = mode === 'usb' ? 1900 : 1000;
    let unwantedLevel = -Infinity;
    for(let n=Math.floor((unwantedHz-50)*analyser.fftSize/p.context.sampleRate);n<=Math.ceil((unwantedHz+50)*analyser.fftSize/p.context.sampleRate);++n)
      unwantedLevel=Math.max(unwantedLevel,bins[n]);
    rejectionDb=bins[peak]-unwantedLevel;
    check(rejectionDb > 40, 'Opposite sideband suppressed in actual browser audio: ' + rejectionDb + ' dB');
  }
  p.gain.disconnect(analyser); analyser.disconnect(); mute.disconnect();
  const before = p.snapshot(), wall = performance.now();
  // Spectrum/status stay active while the audio connection owns one socket.
  while (performance.now() - wall < window.__audioCheckSeconds * 1000) {
    const [status, spectrum] = await Promise.all([fetch('/api/status'), fetch('/api/spectrum')]);
    check(status.ok && (spectrum.ok || spectrum.status === 204), 'Concurrent status/spectrum');
    await status.arrayBuffer(); await spectrum.arrayBuffer(); await sleep(1000);
  }
  const stable = p.snapshot();
  const rate = (stable.blocks - before.blocks) * 240 / ((performance.now() - wall) / 1000);
  check(p.running && stable.missing === 0 && stable.recoveries === 0, 'Continuous audio without gaps or buffer recovery: ' + JSON.stringify({before, stable, events: p.recoveryDetails}));
  check(rate > 11500 && rate < 12500 && stable.maxSources <= 24 && stable.maxBufferMs <= 520,
    '12 kHz sustained with bounded scheduling');
  p.setVolume(0); check(p.volume === 0, 'Mute'); p.setVolume(.15);
  if(window.__pcmFixture&&document.getElementById('agc-mode')) {
    const modeControl=document.getElementById('agc-mode'),thresholdControl=document.getElementById('agc-threshold');
    const control=body=>fetch('/__test/control',{method:'POST',body:JSON.stringify(body)});
    const status=()=>fetch('/api/status').then(r=>r.json());
    const waitFor=async predicate=>{const deadline=performance.now()+5000;while(!predicate()){if(performance.now()>deadline)throw new Error('AGC acknowledgement timeout: '+JSON.stringify({disabled:modeControl.disabled,mode:modeControl.value,threshold:thresholdControl.value,message:document.getElementById('agc-status').textContent,connection:document.getElementById('run-state').textContent,board:await status()}));await sleep(50);}};
    await waitFor(()=>!modeControl.disabled);
    const initial=await status(),initialMissing=p.missing,initialRecoveries=p.recoveries,initialBlocks=p.blocks;
    const setControls=async (m,t)=>{
      thresholdControl.value=String(t);modeControl.value=String(m);modeControl.dispatchEvent(new Event('change',{bubbles:true}));
      await waitFor(()=>document.getElementById('agc-status').textContent==='Door het board bevestigd'||document.getElementById('agc-status').textContent.startsWith('AGC uit'));
      const applied=await status();check(applied.agc_mode===m&&applied.agc_threshold_dbfs===t,'AGC board acknowledgement');
      check(applied.config_id===initial.config_id&&applied.frequency_hz===initial.frequency_hz,'AGC does not retune or restart RF configuration');
      await sleep(700);
    };
    await setControls(1,-100);const regulated=pcmRMS;
    await setControls(2,-20);const reduced=pcmRMS;
    if(mode==='usb'||mode==='lsb')check(reduced<regulated*.6,'Higher threshold reduces actual DSP audio gain');
    await setControls(0,-20);check(thresholdControl.disabled,'OFF disables the threshold slider');
    if(mode==='usb'||mode==='lsb')check(pcmRMS<regulated*.3,'OFF removes automatic audio gain');
    await setControls(3,-100);check(!thresholdControl.disabled,'Slow AGC enables threshold control');
    await control({delay_ms:180});
    for(let n=0;n<10;++n){thresholdControl.value=String(-100+n*2);thresholdControl.dispatchEvent(new Event('input',{bubbles:true}));await sleep(15);}
    await sleep(1200);await control({delay_ms:0});
    const dragged=await status();check(dragged.agc_mode===3&&dragged.agc_threshold_dbfs===-82,'Rapid AGC slider changes retain the last requested value');
    const paused=document.getElementById('pause').getAttribute('aria-pressed')==='true';
    if(!paused)document.getElementById('pause').click();await setControls(2,-100);
    check(p.running,'AGC controls work while spectrum is paused');
    if(!paused)document.getElementById('pause').click();
    await control({reject_agc:true});modeControl.value='1';modeControl.dispatchEvent(new Event('change',{bubbles:true}));await sleep(350);
    const refused=await status();check(refused.agc_mode===2,'Failed backend AGC write does not change board state');
    await control({reject_agc:false});await sleep(1200);
    await setControls(initial.agc_mode,initial.agc_threshold_dbfs);
    await fetch('/api/agc',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({agc_mode:3,agc_threshold_dbfs:-60})});
    await waitFor(()=>modeControl.value==='3'&&thresholdControl.value==='-60'&&document.getElementById('agc-status').textContent==='Door het board bevestigd');
    check(true,'External AGC change resynchronizes the controls without changing RF configuration');
    await setControls(initial.agc_mode,initial.agc_threshold_dbfs);
    check(p.running&&p.blocks>initialBlocks&&p.missing===initialMissing&&p.recoveries===initialRecoveries,
      'Changing AGC keeps the production audio stream running without gaps');
  }
  if (am && window.__pcmFixture) {
    const old = p.discarded, target = (p.expectedConfig?.frequency_hz ?? 7100000) + 1000;
    const tune = await fetch('/api/tune', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({frequency_hz:target})});
    const configuration = await tune.json(); p.setConfig(configuration); await sleep(1200);
    check(p.running && p.message.includes(mode.toUpperCase()) && p.expectedConfig.config_id === configuration.config_id,
      'Live tuning resynchronizes radio audio');
    const late = {tone:false, mode, config:configuration.config_id-1, stream:configuration.stream_id,
      frequency:target-1000, missing:0,gap:false,samples:new Float32Array(240)};
    p.accept(late); check(p.discarded > old, 'Stale audio configuration discarded');
  }
  if (window.__pcmFixture) {
    const control = body => fetch('/__test/control', {method: 'POST', body: JSON.stringify(body)});
    await control({audio_hold: true}); await sleep(500); await control({audio_hold: false}); await sleep(800);
    check(p.missing > 0 && p.recoveries > 0 && p.running, 'Missing audio reported and playback resynchronized');
    await control({audio_hold: true}); await sleep(3400);
    check(!p.running && !p.starting && p.sources.size === 0, 'Stalled stream watchdog releases playback');
    await control({audio_hold: false}); await sleep(300);
  }
  p.stop(); await sleep(300);
  p.start(); await sleep(1000); check(p.running && p.blocks > 10, 'Stop and reconnect');
  p.stop(); await sleep(100); check(p.sources.size === 0 && !p.running, 'Stop releases audio sources');
  if (mode === 'usb' || mode === 'lsb') {
    const selection=document.getElementById('audio-mode');selection.value=mode==='usb'?'lsb':'usb';
    selection.dispatchEvent(new Event('change',{bubbles:true}));
    check(!p.running&&!p.starting&&p.sources.size===0,'Changing sideband stops old audio');
    await sleep(300);p.start();await sleep(1200);
    check(p.running&&p.blocks>10&&p.mode===selection.value,'Other sideband starts a new DSP stream');
    p.stop();await sleep(300);selection.value=mode;
  }
  return {passed: true, tests: tests.length, stable, samplesPerSecond: rate, peakHz, format: '12 kHz signed16 mono',
    mode, rejectionDb, physicalListening: 'Requires user listening test'};
})()
