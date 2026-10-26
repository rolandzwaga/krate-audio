const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={foldMap,periodAnalysis,desc,P_CAL};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
const P=M.P_CAL; console.log('kCalibrationPeak =', P.toPrecision(10));
for(const a of [15,47.5,82.5]){const F=M.foldMap(a);
 for(const lvl of [-24,-12,-6]){const k=Math.pow(10,(lvl+12)/20);const pL=P*k,pR=pL/2;   // L at level, R 6 dB below
  const envShared=pL;const beta=a<=30?0:a>=40?1:0;  // beta 1 above 40 (smoothstep ramp 30..40)
  const Gs=(1-beta)+beta*P/envShared;
  const outL=M.desc(M.periodAnalysis([pL],(x)=>F(Gs*x)/Gs,4096).ys,4096,40).rms, outRs=M.desc(M.periodAnalysis([pR],(x)=>F(Gs*x)/Gs,4096).ys,4096,40).rms;
  const GR=(1-beta)+beta*P/pR; const outRp=M.desc(M.periodAnalysis([pR],(x)=>F(GR*x)/GR,4096).ys,4096,40).rms;
  console.log(`amount ${a} L at ${lvl} dBFS RMS: shared-detector L/R ${(20*Math.log10(outL/outRs)).toFixed(2)} dB, per-channel ${(20*Math.log10(outL/outRp)).toFixed(2)} dB (input 6.02)`);}}
