'use strict';
// Fixed-size PCM frames from the board; accepts arbitrary fetch/TCP splits.
class RadioBerryPCMDecoder {
  constructor(onFrame) { this.onFrame = onFrame; this.bytes = new Uint8Array(520); this.used = 0; this.needed = 8; this.last = null; }
  feed(input) {
    for (let offset = 0; offset < input.length;) {
      const n = Math.min(this.needed - this.used, input.length - offset);
      this.bytes.set(input.subarray(offset, offset + n), this.used); this.used += n; offset += n;
      if (this.used !== this.needed) continue;
      const v = new DataView(this.bytes.buffer);
      const header = v.getUint16(6, true), tone = v.getUint8(4) === 1;
      if (this.needed === 8) {
        const magic = String.fromCharCode(...this.bytes.subarray(0, 4));
        if ((tone ? magic !== 'RBA1' || header !== 32 || !(v.getUint8(5) & 2) || (v.getUint8(5) & ~3) :
              magic !== 'RBA2' || v.getUint8(4) !== 2 || header !== 40 || (v.getUint8(5) & 2)) ||
            (v.getUint8(5) & ~15) || (v.getUint8(5) & 12) === 12) throw new Error('Unknown audio format');
        this.needed = header + 480; continue;
      }
      if (v.getUint32(24, true) !== 12000 || v.getUint16(28, true) !== 240 ||
          v.getUint8(30) !== 16 || v.getUint8(31) !== 1) throw new Error('Unknown audio format');
      const sequence = v.getUint32(8, true), stream = v.getUint32(12, true);
      const firstSample = v.getBigUint64(16, true);
      const mode = tone ? 'tone' : v.getUint8(5) & 4 ? 'usb' : v.getUint8(5) & 8 ? 'lsb' : 'am';
      let missing = 0;
      if (this.last) {
        const advance = (sequence - this.last.sequence) >>> 0;
        if (stream !== this.last.stream || mode !== this.last.mode || !advance || advance >= 0x80000000 ||
            firstSample !== this.last.firstSample + BigInt(advance) * 240n)
          throw new Error('Invalid audio sequence');
        missing = advance - 1;
      }
      const samples = new Float32Array(240);
      for (let i = 0; i < 240; ++i) samples[i] = v.getInt16(header + i * 2, true) / 32768;
      const frequency = tone ? null : v.getUint32(32, true), config = tone ? null : v.getUint32(36, true);
      if (!tone && (frequency < 100000 || frequency > 30000000 || !config)) throw new Error('Invalid radio settings in audio');
      const frame = {sequence, stream, firstSample, samples, missing, tone, mode, frequency, config,
        gap: !!(v.getUint8(5) & 1) || !!(this.last && this.last.config !== config)};
      this.last = frame; this.used = 0; this.needed = 8; this.onFrame(frame);
    }
  }
  finish() { if (this.used) throw new Error('Incomplete audio block'); }
}
window.RadioBerryPCMDecoder = RadioBerryPCMDecoder;
