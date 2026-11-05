(async()=>{
  const p=window.radioBerryIQProbe,checks=[], $=id=>document.getElementById(id);
  const assert=(ok,label)=>{if(!ok)throw new Error(label);checks.push(label);};
  const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  assert($('badge').textContent==='BROWSER DEMO · IQ','Preview explicitly identifies synthetic IQ');
  $('start').click();await wait(400);
  const f=p.state.last,s=f.samples;
  assert(f.synthetic&&f.sampleRate===48000&&f.pairs===512,'Preview uses the same RBI1 decoder');
  const phase=Math.atan2(s[0]*s[3]-s[1]*s[2],s[0]*s[2]+s[1]*s[3]);
  assert(Math.abs(phase*48000/(2*Math.PI)-1000)<.5,'Preview IQ tone has correct orientation');
  const samples=p.state.samples,started=performance.now(),gaps=p.state.gaps;
  await wait(window.__iqCheckSeconds*1000);
  const rate=(p.state.samples-samples)*1000/(performance.now()-started);
  assert(rate>46500&&rate<49500&&p.state.gaps===gaps,'Preview keeps a continuous real-time IQ cadence');
  $('stop').click();const frames=p.state.frames;await wait(150);
  assert(!p.state.running&&p.state.frames===frames,'Preview stops without generating extra frames');
  return{checks,seconds:window.__iqCheckSeconds,measuredPairsPerSecond:rate};
})()
