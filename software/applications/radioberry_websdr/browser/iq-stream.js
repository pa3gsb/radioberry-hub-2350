'use strict';

// Streaming HTTP is dechunked by fetch. RBI1 boundaries are independent of TCP
// and ReadableStream boundaries. Retain at most one 3120-byte application frame.
class RadioBerryIQProtocolError extends Error {}
class RadioBerryIQDecoder {
  constructor(onFrame) {
    this.onFrame = onFrame;
    this.buffer = new Uint8Array(3120);
    this.used = 0;
    this.header = null;
    this.previous = null;
  }
  parseHeader() {
    const b = this.buffer, v = new DataView(b.buffer);
    if (String.fromCharCode(...b.subarray(0, 4)) !== 'RBI1' || b[4] !== 1 ||
        (b[5] & ~7) || v.getUint16(6, true) !== 48 || v.getUint32(8, true) !== 3120 ||
        !v.getUint32(12, true) || v.getUint32(40, true) !== 48000 ||
        v.getUint16(44, true) !== 512 || b[46] !== 24 || b[47] !== 0)
      throw new RadioBerryIQProtocolError('Unknown or corrupted IQ format');
    const frequency = v.getUint32(36, true);
    if (frequency < 100000 || frequency > 30000000) throw new RadioBerryIQProtocolError('Invalid IQ frequency');
    return {flags:b[5], streamId:v.getUint32(12, true), configId:v.getUint32(16, true),
      sequence:v.getUint32(20, true), firstSample:v.getBigUint64(24, true),
      captureMs:v.getUint32(32, true), frequency, sampleRate:48000, pairs:512};
  }
  decodeFrame() {
    const h = this.header, old = this.previous;
    const streamChanged = Boolean(old && h.streamId !== old.streamId);
    const configurationChanged = Boolean(old && !streamChanged && h.configId !== old.configId);
    let missingSamples = 0n, missingBlocks = 0;
    if (old && !streamChanged) {
      const delta = (h.sequence - old.sequence) >>> 0;
      if (!delta || delta >= 0x80000000) throw new RadioBerryIQProtocolError('Duplicate or out-of-order IQ block');
      if (h.firstSample < old.firstSample + 512n) throw new RadioBerryIQProtocolError('IQ sample counter moved backwards');
      if (!configurationChanged && h.frequency !== old.frequency)
        throw new RadioBerryIQProtocolError('IQ frequency changed without a configuration change');
      missingBlocks = delta - 1;
      missingSamples = h.firstSample - old.firstSample - 512n;
    }
    const samples = new Float32Array(1024);
    for (let i = 0; i < 1024; ++i) {
      const at = 48 + i * 3;
      let n = this.buffer[at] | this.buffer[at + 1] << 8 | this.buffer[at + 2] << 16;
      if (n & 0x800000) n -= 0x1000000;
      samples[i] = n / 8388608 * ((i & 1) && (h.flags & 4) ? -1 : 1);
    }
    const gap = Boolean(old && ((h.flags & 1) || missingSamples || missingBlocks || streamChanged || configurationChanged));
    this.previous = h;
    this.onFrame({...h, samples, missingSamples, missingBlocks, streamChanged, configurationChanged,
      gap, synthetic:Boolean(h.flags & 2), unknownGap:Boolean(h.flags & 1)});
  }
  push(bytes) {
    if (!(bytes instanceof Uint8Array)) throw new Error('IQ data must be bytes');
    let offset = 0;
    while (offset < bytes.length) {
      const target = this.header ? 3120 : 48;
      const count = Math.min(target - this.used, bytes.length - offset);
      this.buffer.set(bytes.subarray(offset, offset + count), this.used);
      this.used += count; offset += count;
      if (this.used === 48 && !this.header) this.header = this.parseHeader();
      if (this.used === 3120) {
        this.decodeFrame(); this.used = 0; this.header = null;
      }
    }
  }
  finish() { if (this.used) throw new Error('IQ connection ended within a block'); }
}
window.RadioBerryIQDecoder = RadioBerryIQDecoder;
window.RadioBerryIQProtocolError = RadioBerryIQProtocolError;
