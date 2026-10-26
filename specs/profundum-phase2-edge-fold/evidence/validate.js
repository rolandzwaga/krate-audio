// Candidate-design validation: level law, peaks, h1 phase, harmonic fidelity (comb check) with the split bypass.
const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={render,aliased,fftLen,coreAmps,excParams,foldParamsFor,STATES,NOTES,foldMap,P_CAL,sig};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
function goertzel(x,fs,hz){let re=0,im=0;const N=x.length;for(let k=0;k<N;k++){const w=0.5-0.5*Math.cos(2*Math.PI*k/N);re+=x[k]*w*Math.cos(2*Math.PI*hz*k/fs);im-=x[k]*w*Math.sin(2*Math.PI*hz*k/fs);}return [re,im];}
const rms=(x)=>Math.sqrt(x.reduce((a,v)=>a+v*v,0)/x.length);
const fs=Number(process.argv[2]||48000);
for(const note of ['E1','E2','C2']){const f0=M.NOTES[note];const P=Math.round(fs/f0*8);
 for(const nm of ['sine','mid','saw','sc009']){const st=M.STATES[nm];const amps=M.coreAmps(st,f0,fs);const secs=0.9+P/fs;
  const dry=M.render({amps,f0,fs,F:4,seconds:secs});const d=dry.subarray(dry.length-P);
  let line=`${note} ${nm.padEnd(5)}`;
  for(const [lab,cfg,F] of [['exc d0',{exc:M.excParams(0,0,st.edge)},4],['exc max',{exc:M.excParams(1,1,st.edge)},4],['fold65',{fold:M.foldParamsFor(65,amps)},4],['fold100',{fold:M.foldParamsFor(100,amps)},4]]){
   const y=M.render({amps,f0,fs,F,seconds:secs,...cfg});const o=y.subarray(y.length-P);
   const [a,b]=goertzel(d,fs,f0),[c,e]=goertzel(o,fs,f0);const ang=(Math.atan2(e,c)-Math.atan2(b,a))*180/Math.PI;const mag=20*Math.log10(Math.hypot(c,e)/Math.hypot(a,b));
   let pk=0;for(const v of o)pk=Math.max(pk,Math.abs(v));
   line+=` | ${lab}: L ${(20*Math.log10(rms(o)/rms(d))).toFixed(2)} T1 ${mag.toFixed(2)}dB/${(((ang+540)%360)-180).toFixed(2)}deg pk ${pk.toFixed(2)}`;}
  console.log(line);}}
