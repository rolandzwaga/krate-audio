// foldpan.js variants: detector shared vs per-channel; Fold input = excitation max output or the raw panned core (mix 0).
// Also the h1 magnitude ratio |h1L|/|h1R| at the Fold output (the mono-sum clause penalises imbalance as well as phase).
const M=require('./lib.js');const fs=48000;
for(const note of ['E1','C2'])for(const nm of ['mid','saw'])for(const src of ['exc-max asym0','exc-max asym1','core (mix 0)'])for(const det of (process.env.DET?[process.env.DET]:['shared','per-ch'])){
 const f0=M.NOTES[note],st=M.STATES[nm],base=M.coreAmps(st,f0,fs).map(v=>v*Math.SQRT2);
 const pan=(n)=>n===1?0:(n%2===0?0.5:-0.5);const AL=base.map((a,i)=>a*Math.cos((pan(i+1)+1)*Math.PI/4)),AR=base.map((a,i)=>a*Math.sin((pan(i+1)+1)*Math.PI/4));
 const P=Math.round(fs/f0*16),secs=2.0;const tail=(y)=>y.subarray(y.length-P);
 let eL,eR;if(src.startsWith('exc')){const ex=M.excParams(1,src.endsWith('1')?1:0,st.edge);eL=M.render({amps:AL,f0,fs,F:4,seconds:secs,exc:ex});eR=M.render({amps:AR,f0,fs,F:4,seconds:secs,exc:ex});}
 else{eL=M.synth(AL,f0,fs,Math.round(secs*fs));eR=M.synth(AR,f0,fs,Math.round(secs*fs));}
 const pkOf=(y)=>{let p=0;for(const v of tail(y))p=Math.max(p,Math.abs(v));return p;};const pL=pkOf(eL),pR=pkOf(eR);
 const mk=(env)=>{const fp=M.foldParamsFor(100,[M.P_CAL]);fp.env=env;return fp;};
 const fL=M.render({amps:AL,f0,fs,F:4,seconds:secs,fold:mk(det==='shared'?Math.max(pL,pR):pL),xin:eL}),fR=M.render({amps:AR,f0,fs,F:4,seconds:secs,fold:mk(det==='shared'?Math.max(pL,pR):pR),xin:eR});
 const meas=(l,r)=>{const a=M.goertzel(tail(l),fs,f0),b=M.goertzel(tail(r),fs,f0);const s=tail(l).map((v,k)=>0.5*(v+tail(r)[k]));const c=M.goertzel(s,fs,f0);
  const pa=a[0]**2+a[1]**2,pb=b[0]**2+b[1]**2,pc=c[0]**2+c[1]**2;let d=(Math.atan2(a[1],a[0])-Math.atan2(b[1],b[0]))*180/Math.PI;d=((d+540)%360)-180;
  return {d,mono:10*Math.log10(pc/(0.5*(pa+pb))),ratio:10*Math.log10(pa/pb),coh:10*Math.log10(pc/(0.25*(Math.sqrt(pa)+Math.sqrt(pb))**2))};};
 const i=meas(eL,eR),o=meas(fL,fR);
 console.log(`${note} ${nm.padEnd(3)} ${src.padEnd(13)} ${det.padEnd(6)}: in ${i.d.toFixed(2)} deg/${i.mono.toFixed(2)} dB | out ${o.d.toFixed(2)} deg/${o.mono.toFixed(2)} dB, h1 L/R ${o.ratio.toFixed(2)} dB, re coherent sum ${o.coh.toFixed(3)} dB | added ${(o.d-i.d).toFixed(2)} deg`);}
