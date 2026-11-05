// Observe the board in actual Chrome; never tune or change gain.
(async()=>{
  const p=window.radioBerryIQProbe,$=id=>document.getElementById(id);
  const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  const status=await(await fetch('/api/status')).json();
  const diagStart=await(await fetch('/api/diagnostics')).json();
  $('start').click();
  const deadline=performance.now()+10000;
  while(p.state.frames<10) {
    if(performance.now()>deadline)throw new Error(`No hardware IQ: ${$('status').textContent}`);
    await wait(50);
  }
  const before={samples:p.state.samples,frames:p.state.frames,gaps:p.state.gaps,missing:p.state.missing,
    reconnections:p.state.reconnections};
  const start=performance.now();await wait(window.__iqCheckSeconds*1000);
  const elapsed=(performance.now()-start)/1000;
  const result={seconds:elapsed,frames:p.state.frames-before.frames,
    receivedPairsPerSecond:(p.state.samples-before.samples)/elapsed,
    gaps:p.state.gaps-before.gaps,missingSamples:(p.state.missing-before.missing).toString(),
    reconnects:p.state.reconnections-before.reconnections,synthetic:p.state.last.synthetic,
    frequency:p.state.last.frequency,iqTransport:status.iq_transport};
  const diagEnd=await(await fetch('/api/diagnostics')).json();
  for(const key of ['rx_stalls','tag_errors','invalid_fft'])result[key]=(diagEnd[key]-diagStart[key])>>>0;
  $('stop').click();await wait(100);
  result.passed=!result.synthetic&&!result.gaps&&result.missingSamples==='0'&&!result.reconnects&&
    !result.rx_stalls&&!result.tag_errors&&!result.invalid_fft&&result.receivedPairsPerSecond>46500;
  return result;
})()
