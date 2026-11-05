// From madpsy/ka9q_ubersdr, static/v2/src/lib/waterfallRing.js
// Commit: a18b58aa8865adafa95fa2fe4110d503fe102f7b
// Repository license: GNU GPL v3; see ../../../README.md and ../../../LICENSE-ubersdr.txt.
// Extracted 2026-10-10. Selected functions retained verbatim; unrelated code omitted.

export function ringSlices(head, height, rows) {
    const h = Math.max(0, Math.floor(height));
    const want = Math.min(h, Math.max(0, Math.floor(rows)));
    if (!want) return [];
    const start = ((Math.floor(head) % h) + h) % h;

    // From the head to the end of the buffer, then — if that was not enough —
    // from the start of it. The second run can never overrun the head: it is
    // capped at `want - firstH`, and `want` is at most the whole ring.
    const firstH = Math.min(h - start, want);
    const runs = [{ sy: start, sh: firstH, dy: 0 }];
    if (firstH < want) runs.push({ sy: 0, sh: want - firstH, dy: firstH });
    return runs;
}
