const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={coreAmps,excParams,STATES,NOTES,sig,P_CAL,EXC};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
const Msz=8192;
function h1(amps,g){let re=0,im=0,r0=0,i0=0;for(let k=0;k<Msz;k++){const t=2*Math.PI*k/Msz;let x=0;for(let n=0;n<amps.length;n++)if(amps[n])x+=amps[n]*Math.sin((n+1)*t);const y=g(x);re+=y*Math.sin(t);im+=y*Math.cos(t);r0+=x*Math.sin(t);i0+=x*Math.cos(t);}
 return {db:20*Math.log10(Math.hypot(re,im)/Math.hypot(r0,i0)),deg:(Math.atan2(im,re)-Math.atan2(i0,r0))*180/Math.PI};}
for(const nm of ['sine','mid','saw','sc009']){const st=M.STATES[nm];const amps=M.coreAmps(st,M.NOTES.C2,48000);let line=nm.padEnd(6);
 for(const [d,a] of [[0.5,0],[0.5,0.5],[0.5,1],[1,0],[1,0.5],[1,1]]){const p=M.excParams(d,a,st.edge);const fb=M.sig(p.b);const r=h1(amps,(x)=>p.M*(M.sig(p.D*x/M.P_CAL+p.b)-fb));
  line+=` | d${d} a${a}: ${r.db.toFixed(2)}dB ${r.deg.toFixed(2)}deg`;}
 console.log(line);}
