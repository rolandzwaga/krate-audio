const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={foldMap,periodAnalysis,desc,P_CAL};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
const ref=M.desc(M.periodAnalysis([M.P_CAL],(x)=>x,4096).ys,4096,60);
let prev=null,worstL=[0,0],worstCF=[0,0],minDthd=[1e9,0],t1min=[1e9,0];
for(let i=0;i<=400;i++){const a=i*0.25;const d=M.desc(M.periodAnalysis([M.P_CAL],M.foldMap(a),4096).ys,4096,60);
 const L=20*Math.log10(d.rms/ref.rms),t1=20*Math.log10(Math.abs(d.h1.re)/M.P_CAL);if(t1<t1min[0])t1min=[t1,a];
 if(prev){const dl=Math.abs(L-prev.L),dc=Math.abs(d.cf-prev.cf),dt=d.thd-prev.thd;if(dl>worstL[0])worstL=[dl,a];if(dc>worstCF[0])worstCF=[dc,a];if(a>0.25&&dt<minDthd[0])minDthd=[dt,a];}
 prev={L,cf:d.cf,thd:d.thd};}
console.log(`G_CONT ${process.env.G_CONT||0} THETA ${process.env.THETA_MAX||3}: max|dL| ${worstL[0].toFixed(3)} @${worstL[1]}  max|dCF| ${worstCF[0].toFixed(3)} @${worstCF[1]}  min dTHD ${minDthd[0].toFixed(4)} dB @${minDthd[1]}  min|T1| ${t1min[0].toFixed(2)} @${t1min[1]}`);
