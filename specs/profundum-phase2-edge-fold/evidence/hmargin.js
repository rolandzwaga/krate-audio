const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,'module.exports={foldMap,periodAnalysis,harmonics,P_CAL};');
const Module=require('module');const m=new Module(path);m.filename=path;m._compile(src,path);const M=m.exports;
for(const a of [30,35,47.5,65,82.5,100]){const pa=M.periodAnalysis([M.P_CAL],M.foldMap(a),8192);const h=M.harmonics(pa.ys,pa.M,25);
 console.log(`amount ${a}: `+[3,5,7,9,11,13,15].map(n=>`h${n} ${(10*Math.log10(h[n].p/h[1].p)).toFixed(1)}`).join(' '));}
