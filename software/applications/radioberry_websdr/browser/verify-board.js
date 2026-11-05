// Runs against tests/host_check.py --serve, backed by the actual firmware C code.
(async () => {
  const $ = id => document.getElementById(id);
  const checks = [];
  const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
  function assert(ok, description) { if (!ok) throw new Error(description); checks.push(description); }
  async function waitFor(predicate, description, timeout = 8000) {
    const deadline = performance.now() + timeout;
    while (!(await predicate())) {
      if (performance.now() > deadline) throw new Error(`Timeout: ${description}`);
      await sleep(50);
    }
  }
  const control = async values => (await fetch('/__test/control', { method: 'POST',
    headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(values) })).json();
  const stats = async () => (await fetch('/__test/state')).json();
  function submit(text) { $('frequency').value = text; $('tune-form').requestSubmit(); }
  function setGain(value) { $('rx-gain').value = String(value); $('rx-gain').dispatchEvent(new Event('input', { bubbles: true })); }
  const rows = () => window.__probe.rows.length;
  const connected = () => $('run-state').textContent === 'Board connected';
  assert($('source-badge').textContent === 'ETHERNET · DEMO', 'HTTP page uses board source and labels it synthetic');
  assert($('span').disabled && $('span').value === '48000', 'Board offers only the confirmed 48 kHz profile');
  assert(connected() && rows() > 0, 'Real C service supplies spectrum through HTTP');
  assert(performance.getEntriesByType('resource').every(e => new URL(e.name).origin === location.origin),
    'Page and spectrum require only the local origin');
  await control({ hold: true }); await sleep(350);
  const duplicates = rows(); await sleep(500);
  assert(rows() === duplicates, 'Duplicate frame sequence adds no waterfall rows');
  await control({ hold: false, delay_ms: 300 });
  await waitFor(async () => (await stats()).active > 0, 'delayed spectrum request');
  const beforeTune = rows();
  submit('7105,000'); submit('7106,000');
  assert($('tuned-value').textContent.replace(/\s/g, '').startsWith('7,100,000'),
    'Displayed confirmed frequency waits for the board acknowledgement');
  await sleep(150);
  assert(rows() === beforeTune, 'In-flight old spectrum is discarded after a tune request');
  await waitFor(() => connected() && $('frequency').value === '7106.000', 'latest tune acknowledgement');
  await waitFor(() => rows() > beforeTune, 'spectrum from new configuration');
  let s = await stats();
  assert(s.frequency === 7106000 && s.tunes === 1, 'Rapid tune requests coalesce to the latest absolute frequency');
  assert(s.max_active === 1, 'Browser serializes API requests even with network delay');
  await control({ delay_ms: 0 });
  const statusCount = () => performance.getEntriesByType('resource').filter(e => new URL(e.name).pathname === '/api/status').length;
  function editFrequency(text) {
    $('frequency').value = text;
    $('frequency').dispatchEvent(new Event('input', { bubbles: true }));
  }
  $('frequency').focus();editFrequency('3650,');$('frequency').setSelectionRange(2,4);
  let beforeStatus=statusCount();
  await waitFor(() => statusCount()>beforeStatus, 'periodic status while editing frequency');
  assert($('frequency').value==='3650,' && document.activeElement===$('frequency') &&
    $('frequency').selectionStart===2 && $('frequency').selectionEnd===4,
    'Periodic board status preserves unfinished frequency and selection');
  $('step').focus();beforeStatus=statusCount();
  await control({ frequency: 14200000 });
  await waitFor(() => $('tuned-value').textContent.replace(/\s/g,'')==='14,200,000Hz', 'external tuning while draft is blurred');
  assert($('frequency').value==='3650,', 'Blurred draft survives external tuning while confirmed readout updates');
  $('frequency').dispatchEvent(new KeyboardEvent('keydown',{key:'Escape',bubbles:true}));
  assert($('frequency').value==='14200.000', 'Escape discards the draft and restores the latest board frequency');
  submit('99');beforeStatus=statusCount();
  await waitFor(() => statusCount()>beforeStatus, 'periodic status after invalid frequency');
  assert($('frequency').value==='99' && $('frequency').getAttribute('aria-invalid')==='true' && !$('frequency-error').hidden,
    'Status polling preserves rejected input and its validation message');
  await control({ delay_ms: 300 });
  submit('7102,125');editFrequency('3650,');
  await waitFor(async () => connected() && (await stats()).frequency===7102125, 'tune acknowledgement while typing a new draft');
  assert($('frequency').value==='3650,' && $('tuned-value').textContent.replace(/\s/g,'')==='7,102,125Hz',
    'A late tune acknowledgement preserves a newer frequency edit');
  await control({ delay_ms: 0 });submit('7106,000');
  await waitFor(() => connected() && $('frequency').value==='7106.000', 'submitted draft confirmation');
  assert($('frequency-error').hidden && !$('frequency').hasAttribute('aria-invalid'),
    'Successful submission restores the confirmed field and clears validation');
  await control({ delay_ms: 0, malformed: true });
  await waitFor(() => $('run-state').textContent === 'Disconnected', 'invalid frame indication');
  assert($('peak-value').textContent.includes('—') && $('tuned-value').textContent.includes('—'),
    'Invalid data clears stale measurements');
  await control({ malformed: false });
  await waitFor(() => connected() && !$('peak-value').textContent.includes('—'), 'recover after malformed frame');
  assert(true, 'Connection resynchronizes status after an invalid frame');
  await control({ frequency: 14200000 });
  await waitFor(() => $('frequency').value === '14200.000', 'configuration from another client');
  assert(true, 'External configuration change resynchronizes confirmed frequency');
  await control({ offline: true });
  await waitFor(() => $('run-state').textContent === 'Disconnected', 'connection loss');
  assert($('rx-gain').disabled && $('gain-value').value === '— dB', 'Disconnected board disables gain and clears its confirmed readout');
  await control({ offline: false, reboot: true });
  await waitFor(() => connected() && $('frequency').value === '7100.000' && !$('peak-value').textContent.includes('—'),
    'reboot and changed stream identifier');
  assert(true, 'Connection loss and board reboot recover without refreshing the page');
  assert(!$('rx-gain').disabled && $('gain-value').value === '+14 dB', 'Reconnect restores board-confirmed VGA gain');
  $('pause').click(); await sleep(200);
  const paused = rows(), beforePause = await stats(); await sleep(650);
  const afterPause = await stats();
  assert(rows() === paused && afterPause.spectra === beforePause.spectra, 'Pause stops spectrum polling and drawing');
  $('pause').click();
  await waitFor(() => rows() > paused, 'resume');
  assert(Number($('history-status').textContent.split(' ')[0].replace(',', '.')) >= .7,
    'Waterfall uses source time, retaining blank time gaps after pause');
  $('frequency').value = '99'; $('tune-form').requestSubmit();
  assert($('frequency').getAttribute('aria-invalid') === 'true', 'Invalid user frequency rejected before network write');
  $('frequency').dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
  assert($('frequency').value === '7100.000', 'Escape restores board-confirmed frequency');
  await control({ real: true, hold: true });
  await waitFor(() => $('source-badge').textContent === 'ETHERNET · LIVE' &&
    $('run-state').textContent === 'Waiting for reception…', 'real source without initial FFT');
  assert($('peak-value').textContent.includes('—'), 'Empty real RX clears stale synthetic measurements');
  assert((await fetch('/api/spectrum')).status === 204, 'No fabricated frame before real RX data');
  await control({ hold: false });
  await waitFor(() => connected() && !$('peak-value').textContent.includes('—'), 'real FFT rendered');
  const raw = await (await fetch('/api/spectrum')).arrayBuffer();
  const header = new DataView(raw);
  assert(!(header.getUint8(5) & 1) && header.getUint16(38, true) === 512 &&
    header.getUint16(44, true) > 0 && header.getUint8(46) === 1, 'Browser accepts real Hann FFT metadata');
  assert(Math.abs(Number($('peak-value').textContent.replace(',', '.').split(' ')[0]) + 40) <= .5,
    'Production FFT tone appears at its relative power level');
  assert($('rx-gain').min === '-12' && $('rx-gain').max === '48' && $('rx-gain').step === '1',
    'Hardware RX gain covers -12 to +48 dB in 1 dB steps');
  await control({ delay_ms: 300 });
  await waitFor(async () => (await stats()).active > 0, 'delayed request before slider drag');
  const beforeGain = (await stats()).gains || 0;
  setGain(48); setGain(35); setGain(36);
  assert($('gain-value').value === '+36 dB' && $('gain-status').textContent === 'Instellen…',
    'Dragging gain displays the latest requested value while awaiting hardware');
  await waitFor(async () => connected() && (await stats()).rx_gain_db === 36 &&
    $('gain-status').textContent === 'AD9866 VGA', 'latest VGA gain acknowledgement');
  s = await stats();
  assert(s.gains === beforeGain + 1 && s.max_active === 1,
    'Rapid gain inputs coalesce and share the serialized spectrum transport');
  await control({ delay_ms: 0 });
  for (const gain of [-12, 0, 48]) {
    setGain(gain);
    await waitFor(async () => connected() && (await stats()).rx_gain_db === gain, `gain ${gain} dB`);
    assert($('gain-value').value === `${gain > 0 ? '+' : ''}${gain} dB`, `Acknowledged gain ${gain} dB is rendered`);
  }
  await control({ gain: 20 });
  await waitFor(() => $('rx-gain').value === '20', 'gain from another browser');
  assert($('gain-value').value === '+20 dB', 'External VGA setting resynchronizes the slider');
  submit('7106,000'); setGain(30);
  await waitFor(async () => connected() && $('frequency').value === '7106.000' &&
    (await stats()).rx_gain_db === 30, 'frequency and gain queued together');
  assert((await stats()).max_active === 1, 'Frequency and gain changes are retained together without concurrent API requests');
  submit('7100,000'); setGain(14);
  await waitFor(async () => connected() && $('frequency').value === '7100.000' &&
    (await stats()).rx_gain_db === 14, 'restore hardware settings');
  submit('7105,000');
  await waitFor(() => connected() && $('frequency').value === '7105.000', 'real profile tuning');
  assert(true, 'Real profile keeps hardware acknowledgement and spectrum configuration together');
  submit('7100,000');
  await waitFor(() => connected() && $('frequency').value === '7100.000', 'restore frequency');
  await control({ hold: true }); await sleep(900);
  assert($('run-state').textContent === 'Waiting for reception…' && $('peak-value').textContent.includes('—'),
    'Expired real RX frame clears measurements while the board stays reachable');
  await control({ hold: false });
  await waitFor(() => connected() && !$('peak-value').textContent.includes('—'), 'RX recovery');
  assert(true, 'Fresh real RX resumes without page reload');
  s = await stats();
  return { passed: checks.length, descriptions: checks, fixture: s };
})();
