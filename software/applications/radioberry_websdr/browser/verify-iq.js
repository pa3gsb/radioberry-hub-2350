(async () => {
  const checks=[], $=id=>document.getElementById(id), p=window.radioBerryIQProbe;
  const assert=(ok,label)=>{if(!ok)throw new Error(label);checks.push(label);};
  const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
  async function until(condition,label,timeout=6000) {
    const end=performance.now()+timeout;
    while(!condition()) {if(performance.now()>end)throw new Error(label);await wait(25);}
  }
  const control=async values=>{
    const r=await fetch('/__test/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(values)});
    if(!r.ok)throw new Error('Fixture control failed');return r.json();
  };
  function packet(seq=1,first=0n,flags=4,config=1) {
    const b=new Uint8Array(3120),v=new DataView(b.buffer);
    b.set([82,66,73,49,1,flags]);v.setUint16(6,48,true);v.setUint32(8,3120,true);v.setUint32(12,123,true);
    v.setUint32(16,config,true);v.setUint32(20,seq,true);v.setBigUint64(24,first,true);
    v.setUint32(36,7100000,true);v.setUint32(40,48000,true);v.setUint16(44,512,true);b[46]=24;
    b.set([0,0,128,0,0,128,255,255,127,255,255,255],48);return b;
  }
  let frame=null;
  new RadioBerryIQDecoder(f=>frame=f).push(packet());
  assert(frame.samples[0]===-1&&frame.samples[1]===1&&frame.samples[2]===8388607/8388608&&frame.samples[3]===1/8388608,
    'Signed24 boundaries and exactly one Q inversion');
  const input=packet();
  for(let split=1;split<3120;++split) {
    let count=0;const d=new RadioBerryIQDecoder(()=>++count);
    d.push(input.subarray(0,split));if(count)throw new Error('Premature partial frame');
    d.push(input.subarray(split));if(count!==1)throw new Error(`Split ${split} lost frame`);d.finish();
  }
  assert(true,'All 3119 possible network split points decode exactly once');
  const paired=new Uint8Array(6240);paired.set(packet());paired.set(packet(2,512n),3120);
  const decoded=[];const d=new RadioBerryIQDecoder(f=>decoded.push(f));d.push(paired);
  assert(decoded.length===2&&!decoded[1].gap&&d.buffer.length===3120,'Coalesced frames and decoder memory remain bounded');
  d.push(packet(4,1536n,5));
  assert(decoded[2].gap&&decoded[2].missingSamples===512n&&decoded[2].missingBlocks===1,'Missing samples and skipped blocks detected');
  let rejected=false;try{d.push(packet(4,1536n));}catch{rejected=true;}
  assert(rejected,'Duplicate blocks rejected');
  const wrap=[];const w=new RadioBerryIQDecoder(f=>wrap.push(f));
  w.push(packet(0xffffffff,0x100000123n));w.push(packet(0,0x100000323n));
  assert(wrap[1].firstSample===0x100000323n&&!wrap[1].gap,'64-bit sample counter and 32-bit sequence wrap preserved');
  w.push(packet(1,0x100000523n,4,2));assert(wrap[2].configurationChanged&&wrap[2].gap,'Configuration changes reset stream continuity');
  for(const [at,value] of [[0,0],[4,2],[5,8],[6,0],[8,0],[12,0],[40,0],[44,1],[46,16],[47,1]]) {
    const b=packet();b[at]=value;let bad=false;try{new RadioBerryIQDecoder(()=>{}).push(b);}catch{bad=true;}
    if(!bad)throw new Error(`Malformed header accepted at ${at}`);
  }
  assert(true,'Malformed IQ format, sizes, flags, identifiers and rate rejected');
  const partial=new RadioBerryIQDecoder(()=>{});partial.push(input.subarray(0,300));
  rejected=false;try{partial.finish();}catch{rejected=true;}assert(rejected,'Truncated application frame rejected at EOF');

  await control({real:true});
  assert(!p.state.running&&!p.state.frames,'Stream waits for Start');
  $('start').click();await until(()=>p.state.frames>=20,'No production IQ reached browser');
  const initial=p.state.last;
  assert(initial.synthetic&&initial.pairs===512&&initial.sampleRate===48000,'Production C IQ fixture decoded with explicit synthetic flag');
  const s=initial.samples;
  const phase=Math.atan2(s[0]*s[3]-s[1]*s[2],s[0]*s[2]+s[1]*s[3]);
  assert(Math.abs(phase*48000/(2*Math.PI)-1000)<.5,'Known complex 1000 Hz tone has correct frequency and orientation');
  let power=0;for(const x of s)power+=x*x;
  assert(Math.abs(10*Math.log10(power/512)+26.0206)<.01,'Complex RMS matches known IQ amplitude');
  const startSamples=p.state.samples,startTime=performance.now(),startGaps=p.state.gaps;
  await wait(window.__iqCheckSeconds*1000);
  const measuredRate=(p.state.samples-startSamples)*1000/(performance.now()-startTime);
  assert(measuredRate>46500&&measuredRate<49500,'Continuous IQ delivery runs near 48000 pairs/s');
  assert(p.state.gaps===startGaps,'No sample discontinuities during ordinary streaming');
  assert(!$('stop').disabled&&$('start').disabled,'Streaming controls have correct state');
  const busy=await fetch('/api/iq-stream');assert(busy.status===409,'A second IQ client is rejected');
  const status=await fetch('/api/status');assert(status.ok,'Radio status remains available while IQ streams');
  const spectrum=await fetch('/api/spectrum');assert(spectrum.ok||spectrum.status===204,'Spectrum remains available while IQ streams');
  const previousConfig=p.state.last.configId;
  await control({frequency:7103000});
  await until(()=>p.state.last.configId!==previousConfig,'IQ did not follow tuning');
  assert(p.state.last.frequency===7103000&&p.state.gaps>startGaps,'IQ metadata follows tuning and marks the settling gap');
  const gainConfig=p.state.last.configId;
  await control({gain:30});await until(()=>p.state.last.configId!==gainConfig,'IQ did not follow gain');
  assert(p.state.last.frequency===7103000,'Gain configuration changes preserve the tuned frequency');
  const missing=p.state.missing;
  await control({iq_skip:1024});await until(()=>p.state.missing>missing,'Skipped samples not detected');
  assert(p.state.missing>=missing+1024n,'Known input loss advances the sample counter');
  const reconnects=p.state.reconnections;
  await control({iq_disconnect:true});await until(()=>p.state.reconnections>reconnects,'Disconnect did not trigger reconnect');
  const oldFrames=p.state.frames;await until(()=>p.state.frames>oldFrames+10,'Stream did not recover');
  assert(p.state.running,'Stream automatically recovers after network disconnect');
  $('stop').click();const stopped=p.state.frames;await wait(150);
  assert(!p.state.running&&p.state.frames===stopped&&!$('start').disabled,'Stop aborts streaming without late frames');
  $('start').click();$('stop').click();$('start').click();
  await until(()=>p.state.frames>10,'Rapid restart was canceled by an old request');
  assert(p.state.running,'Old request cleanup cannot cancel a newly started stream');
  $('stop').click();await wait(100);
  return {checks,seconds:window.__iqCheckSeconds,measuredPairsPerSecond:measuredRate,normalStreamingGaps:0};
})()
