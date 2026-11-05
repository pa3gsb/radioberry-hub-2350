// From madpsy/ka9q_ubersdr, static/v2/src/lib/spectrumTrace.js
// Commit: a18b58aa8865adafa95fa2fe4110d503fe102f7b
// Repository license: GNU GPL v3; see ../../../README.md and ../../../LICENSE-ubersdr.txt.
// Extracted 2026-10-10. Selected functions retained verbatim; unrelated code omitted.

export function binsToPixels(bins, width, out) {
    const n = bins.length;
    if (!n) return out;
    const ratio = n / width;
    for (let x = 0; x < width; x++) {
        const lo = Math.floor(x * ratio);
        const hi = Math.max(lo + 1, Math.floor((x + 1) * ratio));
        let m = -Infinity;
        for (let i = lo; i < hi && i < n; i++) {
            const v = bins[i];
            if (v > m) m = v;
        }
        out[x] = m === -Infinity ? bins[Math.min(n - 1, lo)] : m;
    }
    return out;
}

export function frequencyTicks(cfg, cssW) {
    // A zero or missing span divides through to a zero step, and a loop stepping
    // by zero never ends — it hangs the tab rather than drawing a bad ruler.
    // Both callers guard already; a shared function should not need them to.
    if (!cfg || !(cfg.span > 0) || !(cssW > 0)) return { ticks: [], step: 0 };
    const lo = cfg.centerFreq - cfg.span / 2;
    const hi = cfg.centerFreq + cfg.span / 2;
    const targetTicks = Math.max(2, Math.floor(cssW / 110));
    const rough = cfg.span / targetTicks;
    const pow = Math.pow(10, Math.floor(Math.log10(rough)));
    const mult = [1, 2, 2.5, 5, 10].find((m) => pow * m >= rough) || 10;
    const step = pow * mult;

    const minor = step / 5;
    const out = [];
    for (let i = Math.ceil(lo / minor); i <= Math.floor(hi / minor); i++) {
        const f = i * minor;
        out.push({ hz: f, frac: (f - lo) / cfg.span, major: i % 5 === 0 });
    }
    return { ticks: out, step };
}
