// SC-002(c) mix-blend residual, as written (mix 0 / 0.5 / 1) and the C-9 re-rule (mix 0.1 / 0.55 / 1).
// kSineAnchor, drive 0.5, asymmetry 0, recipe edge 0 and 1. Full S5.4 stage (split, 4x halfband, notches, ADAA1, DC 2 Hz).
const M=require('./lib.js');
for(const fs of [44100,48000,96000])for(const note of ['E1','C2'])for(const edge of [0,1]){
 const f0=M.NOTES[note],st={...M.STATES.sine,edge},amps=M.coreAmps(st,f0,fs),P=Math.round(fs/f0*16),n=Math.round(1.2*fs)+P;
 const x=M.synth(amps,f0,fs,n);const p=M.excParams(0.5,0,edge);
 const run=(m)=>{const s=new M.ExcStage(fs);s.seed(p.b);const y=new Float64Array(n);for(let k=0;k<n;k++)y[k]=s.proc(x[k],m,p.D,p.b,p.M,p.c);return y.subarray(n-P);};
 const res=(ya,yb,ymid)=>{let e=0,o=0;for(let k=0;k<P;k++){const d=ymid[k]-0.5*(ya[k]+yb[k]);e+=d*d;o+=ymid[k]*ymid[k];}return 10*Math.log10(e/o);};
 const y0=run(0),y1=run(1),yh=run(0.5),y01=run(0.1),y55=run(0.55);
 console.log(`${fs} ${note} edge ${edge}: as-written y(0.5) vs (y(0)+y(1))/2 ${res(y0,y1,yh).toFixed(1)} dB | re-rule y(0.55) vs (y(0.1)+y(1))/2 ${res(y01,y1,y55).toFixed(1)} dB`);}
