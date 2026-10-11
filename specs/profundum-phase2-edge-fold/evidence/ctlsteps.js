// SC-009 drive / edge steps through the excitation with the two smoothing designs (review round 2, issue 2):
//  A "outputs":     five independent OnePoleSmoothers on m, D, b, M, c (targets M, c latched from the table at the target D).
//  B "coordinates": smooth log2(D) and b per sample; M and c by bilinear lookup in the 49 x 9 table EVERY sample (plan S5.3 rev).
// Metric (SC-009): max |y[k]-y[k-1]| during the transition / max steady |dy| over the 33-point grid of the stepped control.
// 48 kHz, 5 ms smoothers (smoother.h coefficient exp(-5000/(ms*fs))), latch on the 32-sample grid, mix 1, asymmetry 0 and 1.
const M=require('./lib.js');const fs=48000;
// bilinear table, log2 D in [-4,2] (49 pts) x b in [0,0.5] (9 pts), clamped indices (plan S5.3)
const TM=[],TC=[];for(let j=0;j<9;j++)for(let i=0;i<49;i++){const s=M.excStatic(Math.pow(2,-4+6*i/48),0.5*j/8);TM.push(s.makeup);TC.push(s.c);}
const look=(T,l2,b)=>{const pi=Math.min(Math.max((l2+4)*8,0),48),pj=Math.min(Math.max(b*16,0),8);const i=Math.min(Math.floor(pi),47),j=Math.min(Math.floor(pj),7),ti=pi-i,tj=pj-j;
 const a=T[j*49+i]+(T[j*49+i+1]-T[j*49+i])*ti,c=T[(j+1)*49+i]+(T[(j+1)*49+i+1]-T[(j+1)*49+i])*ti;return a+(c-a)*tj;};
// table accuracy vs exact
{let worst=0;for(let i=0;i<=480;i++)for(const b of [0,0.03125,0.25,0.4375,0.5]){const D=Math.pow(2,-4+6*i/480),s=M.excStatic(D,b);worst=Math.max(worst,Math.abs(20*Math.log10(look(TM,Math.log2(D),b)/s.makeup)));}
 console.log(`table makeup interpolation error, worst over 481 x 5 off-grid points: ${worst.toFixed(4)} dB`);}
const coef=Math.exp(-5000/(5*fs));
class Sm{constructor(v){this.c=v;this.t=v;}set(t){this.t=t;}p(){if(Math.abs(this.c-this.t)<1e-4)this.c=this.t;else this.c=this.t+coef*(this.c-this.t);return this.c;}}
function run(x,ctl,design,n,stepAt){ // ctl(k) -> {D,b}; returns y
 const st=new M.ExcStage(fs);let c0=ctl(0);st.seed(c0.b);const y=new Float64Array(n);
 const s0=M.excStatic(c0.D,c0.b);const sm={m:new Sm(1),D:new Sm(c0.D),l2:new Sm(Math.log2(c0.D)),b:new Sm(c0.b),M:new Sm(s0.makeup),c:new Sm(s0.c)};
 for(let k=0;k<n;k++){if(k%32===0){const c=ctl(k);if(design==='A'){const s=M.excStatic(c.D,c.b);sm.D.set(c.D);sm.b.set(c.b);sm.M.set(s.makeup);sm.c.set(s.c);}else{sm.l2.set(Math.log2(c.D));sm.b.set(c.b);}}
  const m=sm.m.p();let D,b,Mk,cc;if(design==='A'){D=sm.D.p();b=sm.b.p();Mk=sm.M.p();cc=sm.c.p();}else{const l2=sm.l2.p();b=sm.b.p();D=Math.pow(2,l2);Mk=look(TM,l2,b);cc=look(TC,l2,b);}
  y[k]=st.proc(x[k],m,D,b,Mk,cc);}
 return y;}
const maxd=(y,a,b)=>{let d=0;for(let k=Math.max(a,1);k<b;k++)d=Math.max(d,Math.abs(y[k]-y[k-1]));return d;};
for(const note of ['E1','C2'])for(const nm of ['mid','saw'])for(const asym of [0,1])for(const ctlName of ['drive','edge']){
 const st=M.STATES[nm],f0=M.NOTES[note],amps=M.coreAmps(st,f0,fs),hold=Math.round(0.5*fs),n=3*hold,x=M.synth(amps,f0,fs,n);
 const Dof=(v)=>ctlName==='drive'?M.driveLaw(v)*M.edgeGain(st.edge):M.driveLaw(0.5)*M.edgeGain(v);const b=0.5*asym;
 // steady grid
 let steady=0;const P=Math.round(fs/f0*2),ns=Math.round(0.7*fs)+P;const xs=x.subarray(0,ns);
 for(let g=0;g<=32;g++){const D=Dof(g/32);const y=run(xs,()=>({D,b}),'B',ns,0);steady=Math.max(steady,maxd(y,ns-P,ns));}
 let line=`${note} ${nm} asym ${asym} ${ctlName} 0->1->0: steady max|dy| ${steady.toFixed(4)}`;
 for(const design of ['A','B']){const y=run(x,(k)=>({D:Dof(k>=hold&&k<2*hold?1:0),b}),design,n,hold);
  const up=maxd(y,hold,hold+Math.round(0.05*fs))/steady,dn=maxd(y,2*hold,2*hold+Math.round(0.05*fs))/steady;
  line+=` | ${design==='A'?'A outputs':'B coords'}: up ${up.toFixed(3)} down ${dn.toFixed(3)} (bar 1.1)`;}
 console.log(line);}
