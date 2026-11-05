// Read-only check on a physical board. No tuning, resets or test-fixture routes.
(async () => {
  const $ = id => document.getElementById(id);
  const checks = [];
  function assert(ok, label) { if (!ok) throw new Error(label); checks.push(label); }
  const before = window.__probe.rows.length;
  await new Promise(resolve => setTimeout(resolve, 1200));
  const status = await (await fetch('/api/status')).json();
  assert($('source-badge').textContent === (status.synthetic ? 'ETHERNET · DEMO' : 'ETHERNET · LIVE'),
    'Physical board page labels the confirmed spectrum source');
  assert($('run-state').textContent === 'Board connected', 'Browser connects to the physical board API');
  const added = window.__probe.rows.length - before;
  assert(added >= 3, 'Live board spectrum adds waterfall rows during the observation');
  assert(!$('peak-value').textContent.includes('—'), 'Live spectrum renders received levels');
  assert($('span').disabled && $('span').value === '48000', 'Physical board confirms the initial spectrum profile');
  assert(performance.getEntriesByType('resource').every(e => new URL(e.name).origin === location.origin),
    'All application resources and API requests stay on the physical board');
  return { passed: checks.length, descriptions: checks, rowsIn1200ms: added,
    confirmedFrequency: $('tuned-value').textContent.trim(), peak: $('peak-value').textContent.trim() };
})();
