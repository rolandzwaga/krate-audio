const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={coreAmps,excParams,STATES,NOTES,sig,P_CAL};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
const Msz=8192;
function h1(amps,g){let re=0,im=0;for(let k=0;k<Msz;k++){const t=2*Math.PI*k/Msz;let x=0;for(let n=0;n<amps.length;n++)if(amps[n])x+=amps[n]*Math.sin((n+1)*t);const y=g(x);re+=y*Math.sin(t);im+=y*Math.cos(t);}return Math.atan2(im,re)*180/Math.PI;}
for(const nm of ['mid','saw']){const st=M.STATES[nm];const base=M.coreAmps(st,M.NOTES.C2,48000).map(v=>v*Math.SQRT2); // undo centre gain
 const pan=(n)=>n===1?0:(n%2===0?0.5:-0.5);const L=base.map((a,i)=>a*Math.cos((pan(i+1)+1)*Math.PI/4)),R=base.map((a,i)=>a*Math.sin((pan(i+1)+1)*Math.PI/4));
 for(const asym of [0,0.5,1]){const p=M.excParams(1,asym,st.edge);const fb=M.sig(p.b);const g=(x)=>p.M*(M.sig(p.D*x/M.P_CAL+p.b)-fb);
  const a=h1(L,g),b=h1(R,g);console.log(`${nm} asym ${asym}: h1 phase L ${a.toFixed(2)} R ${b.toFixed(2)} diff ${(a-b).toFixed(2)} deg`);}}
