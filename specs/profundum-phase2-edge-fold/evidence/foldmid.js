// SC-006 centroid clause and SC-008(a) static continuity on the core's mid grid at C2 (review round 2, issue 7).
// Steady state of the Fold on the mid-grid core output: static map with the steady detector gain
// G = (1 - beta) + beta * P / peak (S4.3/S4.4; the windowed peak equals the waveform peak in steady state).
// One period, 16384 points, FFT; harmonics n with n*f0 < fs/2 (C = sum log2(n) w_n / sum w_n, spec line 240).
const M=require('./lib.js');
for(const fs of [44100,48000]){const f0=M.NOTES.C2,amps=M.coreAmps(M.STATES.mid,f0,fs),N=16384,Nh=Math.floor((fs/2-1e-9)/f0);
 const xs=new Float64Array(N);for(let k=0;k<N;k++){let s=0;for(let j=0;j<amps.length;j++)if(amps[j])s+=amps[j]*Math.sin((j+1)*2*Math.PI*k/N);xs[k]=s;}
 let pk=0;for(const v of xs)pk=Math.max(pk,Math.abs(v));
 const descr=(y)=>{const re=Float64Array.from(y),im=new Float64Array(N);M.fft(re,im);let p1=0,rest=0,num=0,den=0,ms=0,mean=re[0]/N,ypk=0;
  for(let n=1;n<=Nh;n++){const w=(re[n]**2+im[n]**2)*2/N/N;if(n===1)p1=w;else rest+=w;num+=Math.log2(n)*w;den+=w;}
  for(const v of y){ms+=(v-mean)**2;ypk=Math.max(ypk,Math.abs(v-mean));}ms/=N;return {L:10*Math.log10(ms),CF:20*Math.log10(ypk/Math.sqrt(ms)),C:num/den,thd:10*Math.log10(rest/p1)};};
 const rows=[];for(let i=0;i<=400;i++){const a=i*0.25,p=M.foldParams(a),G=(1-p.beta)+p.beta*M.P_CAL/pk,F=M.foldMap(a);rows.push(descr(xs.map(x=>F(G*x)/G)));}
 let wL=[0,0],wCF=[0,0],wC=[0,0];const dthd=[];
 for(let i=1;i<=400;i++){const a=rows[i],b=rows[i-1];const l=Math.abs(a.L-b.L),c=Math.abs(a.CF-b.CF),o=Math.abs(a.C-b.C);if(l>wL[0])wL=[l,i/4];if(c>wCF[0])wCF=[c,i/4];if(o>wC[0])wC=[o,i/4];
  if(a.thd>=-60&&b.thd>=-60)dthd.push([Math.abs(a.thd-b.thd),i/4]);}
 const med=dthd.map(d=>d[0]).sort((x,y)=>x-y)[Math.floor(dthd.length/2)],thr=Math.max(3*med,1),wT=dthd.reduce((m,d)=>d[0]>m[0]?d:m,[0,0]);
 console.log(`${fs} C2 mid grid, THETA_MAX ${M.FOLD.thetaMax}: C(0) ${rows[0].C.toFixed(3)} oct, C(100) ${rows[400].C.toFixed(3)} oct, rise ${(rows[400].C-rows[0].C).toFixed(3)} oct (bar 0.5); L(100)-L(0) ${(rows[400].L-rows[0].L).toFixed(2)} dB`);
 console.log(`   0.25 grid: worst |dL| ${wL[0].toFixed(3)} dB @${wL[1]}, |dCF| ${wCF[0].toFixed(3)} dB @${wCF[1]}, |dC| ${wC[0].toFixed(4)} oct @${wC[1]} (bars 0.25/0.25/0.05); |dTHD| worst ${wT[0].toFixed(3)} @${wT[1]} vs max(3 x median ${med.toFixed(3)}, 1) = ${thr.toFixed(3)} dB over ${dthd.length} pairs`);
 let mono=true;for(let i=2;i<=400;i++)if(!(rows[i].thd>rows[i-1].thd))mono=false;console.log(`   THD strictly increasing over (0.25..100]: ${mono}`);}
