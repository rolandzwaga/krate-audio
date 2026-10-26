// SC-013(c) first clause: HarmonicFold at amount 100 fed the SC-013(b) panned core output at max excitation.
// Full rendered design (render(): split, 4x halfband, notches, DC) per channel; SHARED detector env = max(peak L, peak R)
// of the excitation output (FR-033). Reports inter-channel h1 phase at the Fold input and output, the Fold's added
// difference, and the mono-sum h1 power clause, at input and output. Also the asymmetry-0 case.
const M=require('./lib.js');
for(const fs of [48000])for(const note of ['E1','C2'])for(const nm of ['mid','saw'])for(const asym of [0,1]){
 const f0=M.NOTES[note],st=M.STATES[nm],base=M.coreAmps(st,f0,fs).map(v=>v*Math.SQRT2);
 const pan=(n)=>n===1?0:(n%2===0?0.5:-0.5);const AL=base.map((a,i)=>a*Math.cos((pan(i+1)+1)*Math.PI/4)),AR=base.map((a,i)=>a*Math.sin((pan(i+1)+1)*Math.PI/4));
 const P=Math.round(fs/f0*16),secs=2.0;const ex=M.excParams(1,asym,st.edge);
 const eL=M.render({amps:AL,f0,fs,F:4,seconds:secs,exc:ex}),eR=M.render({amps:AR,f0,fs,F:4,seconds:secs,exc:ex});
 const tail=(y)=>y.subarray(y.length-P);let pk=0;for(const v of tail(eL))pk=Math.max(pk,Math.abs(v));for(const v of tail(eR))pk=Math.max(pk,Math.abs(v));
 const fp=M.foldParamsFor(100,[M.P_CAL]);fp.env=pk;
 const fL=M.render({amps:AL,f0,fs,F:4,seconds:secs,fold:fp,xin:eL}),fR=M.render({amps:AR,f0,fs,F:4,seconds:secs,fold:fp,xin:eR});
 const meas=(l,r)=>{const a=M.goertzel(tail(l),fs,f0),b=M.goertzel(tail(r),fs,f0);const s=tail(l).map((v,k)=>0.5*(v+tail(r)[k]));const c=M.goertzel(s,fs,f0);
  const pa=a[0]**2+a[1]**2,pb=b[0]**2+b[1]**2,pc=c[0]**2+c[1]**2;let d=(Math.atan2(a[1],a[0])-Math.atan2(b[1],b[0]))*180/Math.PI;d=((d+540)%360)-180;
  return {d,mono:10*Math.log10(pc/(0.5*(pa+pb)))};};
 const i=meas(eL,eR),o=meas(fL,fR);
 console.log(`${fs} ${note} ${nm} asym ${asym}: Fold input  L-R h1 ${i.d.toFixed(2)} deg, mono-sum ${i.mono.toFixed(2)} dB | Fold output L-R h1 ${o.d.toFixed(2)} deg, mono-sum ${o.mono.toFixed(2)} dB | added by Fold ${(o.d-i.d).toFixed(2)} deg`);}
