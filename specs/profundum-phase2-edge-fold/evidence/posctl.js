const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={render,aliased,fftLen,coreAmps,excParams,STATES,NOTES};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
const fs=48000,f0=M.NOTES.E1,L=M.fftLen(fs,f0);
for(const nm of ['saw','sc009','growl','mid']){const st=M.STATES[nm];const amps=M.coreAmps(st,f0,fs);
 for(const asym of [0,1]){const y=M.render({amps,f0,fs,F:2,seconds:(L+0.7*fs)/fs,exc:M.excParams(1,asym,1),naiveExc:true});
 console.log(`naive base-rate, E1 48k, ${nm}, drive 1, edge 1 (Deff 3.98), asym ${asym}: ${M.aliased(y.subarray(y.length-L),L,fs,f0).toFixed(2)} dBFS`);}}
