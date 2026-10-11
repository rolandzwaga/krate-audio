// Worst case over the step's phase within the period (16 step offsets, each on the 32-sample latch grid) for ctlsteps.js,
// plus the transient level bump (max |y| in the 20 ms after the step re the steady max |y| at the endpoints).
const M=require('./lib.js');const fs=48000;
const TM=[],TC=[];for(let j=0;j<9;j++)for(let i=0;i<49;i++){const s=M.excStatic(Math.pow(2,-4+6*i/48),0.5*j/8);TM.push(s.makeup);TC.push(s.c);}
const look=(T,l2,b)=>{const pi=Math.min(Math.max((l2+4)*8,0),48),pj=Math.min(Math.max(b*16,0),8);const i=Math.min(Math.floor(pi),47),j=Math.min(Math.floor(pj),7),ti=pi-i,tj=pj-j;
 const a=T[j*49+i]+(T[j*49+i+1]-T[j*49+i])*ti,c=T[(j+1)*49+i]+(T[(j+1)*49+i+1]-T[(j+1)*49+i])*ti;return a+(c-a)*tj;};
const coef=Math.exp(-5000/(5*fs));
class Sm{constructor(v){this.c=v;this.t=v;}set(t){this.t=t;}p(){if(Math.abs(this.c-this.t)<1e-4)this.c=this.t;else this.c=this.t+coef*(this.c-this.t);return this.c;}}
function run(x,ctl,design,n){const st=new M.ExcStage(fs);let c0=ctl(0);st.seed(c0.b);const y=new Float64Array(n);
 const s0=M.excStatic(c0.D,c0.b);const sm={m:new Sm(1),D:new Sm(c0.D),l2:new Sm(Math.log2(c0.D)),b:new Sm(c0.b),M:new Sm(s0.makeup),c:new Sm(s0.c)};
 for(let k=0;k<n;k++){if(k%32===0){const c=ctl(k);if(design==='A'){const s=M.excStatic(c.D,c.b);sm.D.set(c.D);sm.b.set(c.b);sm.M.set(s.makeup);sm.c.set(s.c);}else{sm.l2.set(Math.log2(c.D));sm.b.set(c.b);}}
  const m=sm.m.p();let D,b,Mk,cc;if(design==='A'){D=sm.D.p();b=sm.b.p();Mk=sm.M.p();cc=sm.c.p();}else{const l2=sm.l2.p();b=sm.b.p();D=Math.pow(2,l2);Mk=look(TM,l2,b);cc=look(TC,l2,b);}
  y[k]=st.proc(x[k],m,D,b,Mk,cc);}return y;}
const maxd=(y,a,b)=>{let d=0;for(let k=Math.max(a,1);k<b;k++)d=Math.max(d,Math.abs(y[k]-y[k-1]));return d;};
const maxa=(y,a,b)=>{let d=0;for(let k=a;k<b;k++)d=Math.max(d,Math.abs(y[k]));return d;};
for(const note of ['E1','C2'])for(const nm of ['mid','saw'])for(const ctlName of ['drive','edge']){const asym=1;
 const st=M.STATES[nm],f0=M.NOTES[note],amps=M.coreAmps(st,f0,fs),pre=Math.round(0.6*fs),post=Math.round(0.06*fs),Pd=fs/f0;
 const Dof=(v)=>ctlName==='drive'?M.driveLaw(v)*M.edgeGain(st.edge):M.driveLaw(0.5)*M.edgeGain(v);const b=0.5*asym;
 const nS=pre+Math.ceil(Pd)+post,x=M.synth(amps,f0,fs,nS);
 let steady=0,steadyPk=0;const P=Math.ceil(Pd)*2;
 for(let g=0;g<=32;g++){const D=Dof(g/32);const y=run(x.subarray(0,pre+P),()=>({D,b}),'B',pre+P);steady=Math.max(steady,maxd(y,pre,pre+P));if(g===0||g===32)steadyPk=Math.max(steadyPk,maxa(y,pre,pre+P));}
 let line=`${note} ${nm} asym1 ${ctlName} 0->1 worst over 16 step phases:`;
 for(const design of ['A','B']){let wr=0,wp=0;for(let ph=0;ph<16;ph++){const at=32*Math.ceil((pre+ph*Pd/16)/32);
   const y=run(x,(k)=>({D:Dof(k>=at?1:0),b}),design,nS);wr=Math.max(wr,maxd(y,at,at+post-1)/steady);wp=Math.max(wp,maxa(y,at,at+Math.round(0.02*fs))/steadyPk);}
  line+=` | ${design==='A'?'A outputs':'B coords'}: delta ratio ${wr.toFixed(3)} (bar 1.1), level bump ${(20*Math.log10(wp)).toFixed(2)} dB`;}
 console.log(line);}
