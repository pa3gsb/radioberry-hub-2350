'use strict';
class RadioBerryPCMProbe {
  constructor() {
    this.context = null; this.gain = null; this.sources = new Set(); this.epoch = 0;
    this.running = false; this.starting = false; this.volume = .15; this.message = 'Ready to listen';
    this.available = true;
    this.mode = 'tone'; this.expectedConfig = null; this.discarded = 0;
    this.recoveryDetails = [];
    this.blocks = 0; this.missing = 0; this.recoveries = 0; this.maxSources = 0;
    this.maxBufferMs = 0; this.nextTime = 0; this.lastFrameAt = 0; this.startedAt = 0;
    this.abort = null; this.reader = null; this.watchdog = null; this.elapsed = 0;
  }
  snapshot() {
    const bufferMs = this.running ? Math.max(0, (this.nextTime - this.context.currentTime) * 1000) : 0;
    const seconds = this.running ? (performance.now() - this.startedAt) / 1000 : this.elapsed;
    return {running: this.running, starting: this.starting, blocks: this.blocks, missing: this.missing,
      recoveries: this.recoveries, bufferMs, maxBufferMs: this.maxBufferMs, activeSources: this.sources.size,
      maxSources: this.maxSources, sampleRate: this.context?.sampleRate ?? 0,
      receivedSamplesPerSecond: seconds ? this.blocks * 240 / seconds : 0, seconds,
      message: this.message, volume: this.volume, mode: this.mode, discarded: this.discarded};
  }
  update() {
    const s = this.snapshot();
    document.getElementById('listen').disabled = !this.available || s.running || s.starting;
    document.getElementById('stop').disabled = !s.running && !s.starting;
    document.getElementById('message').textContent = s.message;
    document.getElementById('blocks').textContent = s.blocks.toLocaleString('en-GB');
    document.getElementById('missing').textContent = s.missing;
    document.getElementById('recoveries').textContent = s.recoveries;
    document.getElementById('buffer').textContent = `${Math.round(s.bufferMs)} ms`;
    document.getElementById('rate').textContent = `${Math.round(s.receivedSamplesPerSecond)} samples/s`;
  }
  clearSources() {
    for (const source of this.sources) { source.onended = null; try { source.stop(); } catch {} source.disconnect(); }
    this.sources.clear();
  }
  accept(frame) {
    if (!this.running) return;
    if ((frame.mode ?? (frame.tone ? 'tone' : 'am')) !== this.mode) throw new Error('The audio source does not match the selected mode.');
    this.lastFrameAt = performance.now();
    if (!frame.tone && this.expectedConfig && (frame.config !== this.expectedConfig.config_id ||
        frame.stream !== this.expectedConfig.stream_id || frame.frequency !== this.expectedConfig.frequency_hz)) {
      ++this.discarded; return;
    }
    if (!frame.tone) this.message = `${this.mode.toUpperCase()} · ${(frame.frequency / 1000).toLocaleString('en-GB')} kHz · ${this.mode === 'am' ? '3 kHz audio filter' : '300–2700 Hz speech filter'}`;
    const now = this.context.currentTime;
    const reset = frame.gap || frame.missing || (this.blocks && this.nextTime < now + .004) ||
      this.nextTime > now + .5 || this.sources.size >= 24;
    if (!this.blocks || reset) {
      if (reset && this.blocks) {
        ++this.recoveries;
        this.recoveryDetails.push({block: this.blocks, elapsed: (performance.now() - this.startedAt) / 1000,
          aheadMs: (this.nextTime - now) * 1000, missing: frame.missing, gap: frame.gap, sources: this.sources.size});
        if (this.recoveryDetails.length > 5) this.recoveryDetails.shift();
      }
      this.clearSources(); this.nextTime = now + .16;
    }
    this.missing += frame.missing; ++this.blocks; this.lastFrameAt = performance.now();
    const buffer = this.context.createBuffer(1, 240, 12000), data = buffer.getChannelData(0);
    data.set(frame.samples);
    if (reset || this.blocks === 1) for (let n = 0; n < 60; ++n) data[n] *= n / 60;
    const source = this.context.createBufferSource(); source.buffer = buffer; source.connect(this.gain);
    // Small clock correction prevents long sessions growing a permanent queue.
    const ahead = this.nextTime - now;
    source.playbackRate.value = Math.max(.995, Math.min(1.005, 1 + (ahead - .22) * .02));
    source.onended = () => { this.sources.delete(source); source.disconnect(); };
    this.sources.add(source); source.start(this.nextTime);
    this.nextTime += .02 / source.playbackRate.value;
    this.maxSources = Math.max(this.maxSources, this.sources.size);
    this.maxBufferMs = Math.max(this.maxBufferMs, (this.nextTime - now) * 1000);
  }
  async start() {
    if (this.running || this.starting) return;
    const epoch = ++this.epoch;
    this.mode = document.getElementById('audio-mode')?.value ?? 'tone';
    this.startedAt = performance.now(); this.discarded = 0;
    this.starting = true; this.message = 'Starting the connection and audio…'; this.update();
    try {
      if (!this.context || this.context.state === 'closed') {
        this.context = new (window.AudioContext || window.webkitAudioContext)({latencyHint: 'interactive'});
        this.gain = this.context.createGain(); this.gain.connect(this.context.destination);
        this.context.onstatechange = () => {
          if (this.running && this.context.state !== 'running') this.stop('Audio paused. Click Listen.');
        };
      }
      // Executed directly from the click; ordinary HTTP needs no AudioWorklet.
      await this.context.resume();
      // On Windows the context may report "running" while the audio device
      // is still starting. Wait for its clock before scheduling network PCM.
      const clockStart = this.context.currentTime, clockDeadline = performance.now() + 2000;
      while (epoch === this.epoch && this.context.currentTime - clockStart < .05) {
        if (performance.now() > clockDeadline) throw new Error('The audio clock did not start. Click Listen again.');
        await new Promise(resolve => setTimeout(resolve, 20));
      }
      if (epoch !== this.epoch) { if (!this.running && !this.starting) await this.context.suspend(); return; }
      if (this.context.state !== 'running') throw new Error('Audio is blocked. Click Listen again.');
      const abort = new AbortController(); this.abort = abort;
      const decoder = new RadioBerryPCMDecoder(frame => this.accept(frame));
      this.blocks = this.missing = this.recoveries = this.maxSources = this.maxBufferMs = 0;
      this.recoveryDetails = [];
      this.startedAt = this.lastFrameAt = performance.now(); this.elapsed = 0;
      this.watchdog = setInterval(() => {
        if (performance.now() - this.lastFrameAt > 3000) this.stop('No audio received. Click Listen to reconnect.');
        else this.update();
      }, 100);
      const response = await fetch('/api/audio-' + this.mode, {cache: 'no-store', signal: abort.signal});
      if (epoch !== this.epoch) { response.body?.cancel().catch(() => {}); return; }
      if (!response.ok) throw new Error(response.status === 409 ? 'The audio or IQ stream is already in use.' : `Board returned ${response.status}`);
      if (response.headers.get('content-type') !== (this.mode === 'tone' ? 'application/x-rbpcm' : 'application/x-rbpcm-' + this.mode) || !response.body)
        throw new Error('This audio test is not available on the board.');
      this.starting = false; this.running = true; this.gain.gain.value = this.volume;
      this.nextTime = this.context.currentTime; this.message = this.mode === 'tone' ? 'Test tone from the board · 600 Hz' : `Waiting for ${this.mode.toUpperCase()} reception…`; this.update();
      const reader = response.body.getReader(); this.reader = reader;
      for (;;) {
        const {value, done} = await reader.read();
        if (epoch !== this.epoch) return;
        if (done) { decoder.finish(); throw new Error('The board connection was closed. Click Listen.'); }
        decoder.feed(value);
      }
    } catch (error) {
      if (epoch === this.epoch) this.stop(error.message || 'Audio connection failed');
    }
  }
  stop(message = 'Stopped — click Listen to restart') {
    ++this.epoch; if (this.running || this.starting) this.elapsed = (performance.now() - this.startedAt) / 1000;
    this.running = this.starting = false; clearInterval(this.watchdog); this.watchdog = null;
    this.abort?.abort(); this.abort = null; this.reader?.cancel().catch(() => {}); this.reader = null;
    this.clearSources(); if (this.context?.state === 'running') this.context.suspend().catch(() => {});
    this.message = message; this.update();
  }
  setVolume(value) {
    if (!Number.isFinite(value)) return;
    this.volume = Math.max(0, Math.min(1, value));
    if (this.gain && this.context) this.gain.gain.setTargetAtTime(this.volume, this.context.currentTime, .01);
    this.update();
  }
  setConfig(config) {
    const old = this.expectedConfig; this.expectedConfig = config;
    if (this.mode !== 'tone' && this.running && old && (old.config_id !== config.config_id || old.stream_id !== config.stream_id)) {
      this.clearSources(); this.nextTime = this.context.currentTime;
      this.message = 'New radio setting — waiting for audio…'; this.update();
    }
  }
}
window.radioBerryPCMProbe = new RadioBerryPCMProbe();
document.getElementById('listen').addEventListener('click', () => window.radioBerryPCMProbe.start());
document.getElementById('stop').addEventListener('click', () => window.radioBerryPCMProbe.stop());
document.getElementById('volume').addEventListener('input', event => window.radioBerryPCMProbe.setVolume(Number(event.target.value) / 100));
document.getElementById('audio-mode')?.addEventListener('change', () => {
  window.radioBerryPCMProbe.stop('Mode changed — click Listen.');
});
document.addEventListener('visibilitychange', () => {
  if (document.hidden) window.radioBerryPCMProbe.stop('Paused because the page is hidden. Click Listen.');
});
window.addEventListener('pagehide', () => window.radioBerryPCMProbe.stop());
window.radioBerryPCMProbe.update();
