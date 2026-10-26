// Centroid of the Fold on the mid grid at C2/48 kHz vs amount, for the THETA_MAX / G_COMP in the environment.
const M=require('./lib.js');const fs=48000,f0=M.NOTES.C2,amps=M.coreAmps(M.STATES.mid,f0,fs),N=16384,Nh=Math.floor(fs/2/f0);
const xs=new Float64Array(N);for(let k=0;k<N;k++){let s=0;for(let j=0;j<amps.length;j++)if(amps[j])s+=amps[j]*Math.sin((j+1)*2*Math.PI*k/N);xs[k]=s;}
let pk=0;for(const v of xs)pk=Math.max(pk,Math.abs(v));
const C=(y)=>{const re=Float64Array.from(y),im=new Float64Array(N);M.fft(re,im);let num=0,den=0;for(let n=1;n<=Nh;n++){const w=re[n]**2+im[n]**2;num+=Math.log2(n)*w;den+=w;}return num/den;};
let line=`theta ${M.FOLD.thetaMax}: input peak/P ${(pk/M.P_CAL).toFixed(3)}; C(a):`;for(const a of [0,30,65,82.5,90,100]){const p=M.foldParams(a),G=(1-p.beta)+p.beta*M.P_CAL/pk,F=M.foldMap(a);line+=` ${a}:${C(xs.map(x=>F(G*x)/G)).toFixed(3)}`;}
console.log(line);
