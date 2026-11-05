// Runs through CDP against the actual page. Only the timer is made deterministic.
(() => {
  const $ = id => document.getElementById(id);
  const checks = [];
  function assert(condition, description) {
    if (!condition) throw new Error(description);
    checks.push(description);
  }
  const tick = () => window.__probe.tick();
  const rows = window.__probe.rows;
  const latest = () => rows[rows.length - 1];
  const peakIndex = row => {
    let max = -1, peak = 0;
    for (let i = 0; i < row.length / 4; i++) {
      const brightness = row[i * 4] + row[i * 4 + 1] + row[i * 4 + 2];
      if (brightness > max) { max = brightness; peak = i; }
    }
    return peak;
  };
  const hzAt = (i, centre, span) => centre - span / 2 + (i + 0.5) * span / 512;
  function submit(text) { $('frequency').value = text; $('tune-form').requestSubmit(); }
  function select(id, value) { $(id).value = value; $(id).dispatchEvent(new Event('change', { bubbles: true })); }

  assert(rows.length === 1, 'Initial frame paints exactly one waterfall row');
  assert(Math.abs(hzAt(peakIndex(latest()), 7100000, 48000) - 7104200) < 94,
    'Known simulated carrier appears at 7104200 Hz within one bin');
  const started = performance.now();
  for (let i = 0; i < 220; i++) tick();
  const elapsed = performance.now() - started;
  assert(rows.length === 221, 'Each generated frame writes exactly one row, including wrap');
  assert($('history-status').textContent === '20.0 s history', 'Waterfall history stays bounded at 200 rows');

  // Compare peaks in actual visible pixels against previously captured rows.
  // The screen is wider than 512 bins; nearest-neighbour blitting preserves them.
  const canvas = $('waterfall');
  const rect = canvas.getBoundingClientRect();
  const scale = canvas.width / rect.width;
  const left = rect.width < 550 ? 48 : 56;
  const plotWidth = rect.width - left - 18;
  const plotHeight = rect.height - 12;
  for (const age of [0, 80, 180]) {
    const y = Math.floor((age + 0.5) / 200 * plotHeight * scale);
    const line = canvas.getContext('2d').getImageData(Math.round(left * scale), y, Math.round(plotWidth * scale), 1).data;
    const actual = peakIndex(line) / (line.length / 4);
    const expected = (peakIndex(rows[rows.length - 1 - age]) + 0.5) / 512;
    assert(Math.abs(actual - expected) < 0.007, `Visible waterfall preserves row order at age ${age} after ring wrap`);
  }

  $('pause').click();
  const pausedRows = rows.length;
  const pausedImage = $('waterfall').toDataURL();
  for (let i = 0; i < 5; i++) tick();
  assert(rows.length === pausedRows && $('waterfall').toDataURL() === pausedImage, 'Pause freezes both incoming rows and displayed pixels');
  const peakBefore = hzAt(peakIndex(latest()), 7100000, 48000);
  submit('7101,250');
  assert($('tuned-value').textContent.trim() === '7,101,250 Hz', 'Decimal comma is accepted as kHz');
  const peakAfter = hzAt(peakIndex(latest()), 7101250, 48000);
  assert(Math.abs(peakBefore - peakAfter) < 94, 'Retuning moves the view while the RF carrier stays at the same frequency');
  assert($('history-status').textContent === '0.1 s history', 'Retuning clears history from the previous frequency axis');
  submit('7102.125');
  assert($('tuned-value').textContent.trim() === '7,102,125 Hz', 'Decimal dot is accepted as kHz');
  for (const bad of ['', '7,1M', '7.100.000', 'NaN', '1e4', '99', '30001', '7100,1234']) {
    submit(bad);
    assert($('frequency').getAttribute('aria-invalid') === 'true' && $('tuned-value').textContent.trim() === '7,102,125 Hz', `Invalid input is rejected without retuning: ${JSON.stringify(bad)}`);
  }
  $('frequency').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
  assert($('frequency').value === '7102.125' && $('frequency-error').hidden, 'Escape restores the confirmed frequency');
  $('step-up').click();
  assert($('frequency').value === '7103.125', 'Step button adds selected 1 kHz');
  $('spectrum').dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowLeft', bubbles: true }));
  assert($('frequency').value === '7102.125', 'Spectrum keyboard navigation tunes one step');
  const bounds = $('spectrum').getBoundingClientRect();
  const plotLeft = bounds.width < 550 ? 48 : 56;
  $('spectrum').dispatchEvent(new MouseEvent('click', { clientX: bounds.left + plotLeft + (bounds.width - plotLeft - 18) * .75, clientY: bounds.top + 60, bubbles: true }));
  const clickedHz = Number($('frequency').value.replace(',', '.')) * 1000;
  assert(Math.abs(clickedHz - 7114125) <= 48000 / (bounds.width - plotLeft - 18),
    `Click at 75% tunes +12 kHz within one CSS pixel (actual ${clickedHz} Hz)`);
  document.querySelector('[data-preset="14200000"]').click();
  assert($('frequency').value === '14200.000', 'Band preset selects its own synthetic scene');
  select('span', '12000');
  assert(Math.abs(hzAt(peakIndex(latest()), 14200000, 12000) - 14204200) < 24, '12 kHz span preserves frequency calibration within one bin');
  select('palette', 'turbo');
  assert($('history-status').textContent === '0.1 s history', 'Palette change starts history in one consistent palette');
  $('floor').value = '-120'; $('floor').dispatchEvent(new Event('input', { bubbles: true }));
  assert($('floor-value').value === '-120 dB', 'Display level control updates the scale');
  assert(performance.getEntriesByType('resource').filter(r => /^https?:/.test(r.name)).length === 0,
    'Standalone page loads without external resources');
  $('reset').click();
  assert($('frequency').value === '7100.000' && $('pause').getAttribute('aria-pressed') === 'false'
    && $('span').value === '48000' && $('palette').value === 'ice', 'Reset restores the starting scene and running state');
  return { passed: checks.length, descriptions: checks, accelerated220FramesMs: Math.round(elapsed) };
})()
