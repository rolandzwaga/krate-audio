// SC-013(c) step clause: HarmonicFold at amount 47.5, cal sine at E1, R 6 dB below L, L steps -24 -> -6 dBFS RMS
// (1 s holds), 4-period frames (round(4 fs/f0)) hopped by one period (round(fs/f0)). Time-domain S4.4 detector
// (8-segment windowed peak, attack, release 4/f0 with setFundamentalHz(E1)), G = P/max(env, floor) (beta = 1 at 47.5),
// static map at the base rate (S4.3; the split structure is linear in c and the Fold at 47.5 aliases at -168 dBFS, S9.4).
// Variants: ATTACK_MS (default 2), INSTANT=1 (env := raw when raw > env), PERCH=1 (per-channel detectors).
const M=require('./lib.js');const fs=Number(process.env.FS||48000),f0=M.NOTES.E1,P=M.P_CAL,amount=47.5,F=M.foldMap(amount);
const attackMs=Number(process.env.ATTACK_MS||2),instant=process.env.INSTANT==='1',perch=process.env.PERCH==='1';
const hold=fs,n=2*hold,aL=(k)=>Math.SQRT2*Math.pow(10,(k<hold?-24:-6)/20);
class Det{constructor(){this.seg=Math.ceil(Math.round(0.125*fs)/8);this.ring=new Float64Array(8);this.pos=0;this.cnt=0;this.mx=0;this.env=0;
 this.aa=1-Math.exp(-1/(attackMs*1e-3*fs));this.ar=1-Math.exp(-f0/(4*fs));}
 step(d){this.mx=Math.max(this.mx,d);if(++this.cnt===this.seg){this.ring[this.pos]=this.mx;this.pos=(this.pos+1)&7;this.mx=0;this.cnt=0;}
  let raw=this.mx;for(const v of this.ring)raw=Math.max(raw,v);
  if(instant&&raw>this.env)this.env=raw;else this.env+=(raw>this.env?this.aa:this.ar)*(raw-this.env);return P/Math.max(this.env,1e-6);}}
const dS=new Det(),dL=new Det(),dR=new Det();const yL=new Float64Array(n),yR=new Float64Array(n);
for(let k=0;k<n;k++){const s=Math.sin(2*Math.PI*f0*k/fs),xL=aL(k)*s,xR=0.5*xL;let gL,gR;
 if(perch){gL=dL.step(Math.abs(xL));gR=dR.step(Math.abs(xR));}else{gL=gR=dS.step(Math.max(Math.abs(xL),Math.abs(xR)));}
 yL[k]=F(gL*xL)/gL;yR[k]=F(gR*xR)/gR;}
const fr=Math.round(4*fs/f0),hop=Math.round(fs/f0),ratio=(a)=>{let l=0,r=0;for(let k=a;k<a+fr;k++){l+=yL[k]*yL[k];r+=yR[k]*yR[k];}return 10*Math.log10(l/r);};
const pre=ratio(hold-fr-hop),post=ratio(n-fr);const lo=Math.min(pre,post)-0.1,hi=Math.max(pre,post)+0.1;
let worst=0,wAt=0,minR=1e9,maxR=-1e9;for(let a=Math.round(0.5*fs);a+fr<=n;a+=hop){const r=ratio(a);minR=Math.min(minR,r);maxR=Math.max(maxR,r);const e=r<lo?lo-r:r>hi?r-hi:0;if(e>worst){worst=e;wAt=a;}}
console.log(`fs ${fs} attack ${instant?'instant':attackMs+' ms'} ${perch?'per-channel':'shared'}: steady pre ${pre.toFixed(3)} post ${post.toFixed(3)} dB; frame ratio range ${minR.toFixed(3)}..${maxR.toFixed(3)}; worst excursion outside band ${worst.toFixed(3)} dB (bar 0) at frame start ${((wAt-hold)/fs*1000).toFixed(1)} ms re step`);
