// Loads edge-fold-model.js internals for the review-round evidence scripts (plan Review notes, round 2).
const path=require('path').join(__dirname,'..','edge-fold-model.js');
const src=require('fs').readFileSync(path,'utf8').replace(/const arg = process\.argv\[2\][\s\S]*$/,
 'module.exports={render,aliased,fftLen,coreAmps,excParams,excStatic,foldParamsFor,foldParams,STATES,NOTES,foldMap,P_CAL,sig,adaa1,driveLaw,edgeGain,smooth,OSHB,DcBlock,Cascade,lpCoeffs,preStages,periodAnalysis,harmonics,desc,fft,EXC,FOLD};');
const Module=require('module');const m=new Module(path);m.filename=path;m.paths=Module._nodeModulePaths(require('path').dirname(path));m._compile(src,path);
const M=m.exports;
// One channel of the S5.4 excitation stage with per-sample controls (mix m, drive D, bias b, makeup Mk, h1 c).
M.ExcStage=class{constructor(fs,F=4){this.fs=fs;this.F=F;this.os=new M.OSHB(F);this.lb=new M.Cascade([M.lpCoeffs(500,fs,Math.SQRT1_2)]);
  this.lo=new M.Cascade([M.lpCoeffs(500,F*fs,Math.SQRT1_2)]);const pre=new M.Cascade(M.preStages(2*fs,1,fs,4));this.hook=(v)=>pre.proc(v);
  this.dc=new M.DcBlock(2,fs);this.buf=new Float64Array(F);this.v1=0;this.s1=1;this.idle=true;}
 seed(b){this.v1=b;this.s1=Math.sqrt(1+b*b);}
 proc(x,m,D,b,Mk,c){if(m===0&&this.idle)return x;this.idle=false;
  const cm=1-m+m*c,w=M.smooth(m/0.1),sb=Math.sqrt(1+b*b),fb=(b+b)/(sb+sb);
  const lo=this.lb.proc(x),base=cm*(lo+(1-w)*(x-lo));this.os.up(x,this.buf);
  for(let i=0;i<this.F;i++){const u=this.buf[i],v=D*u/M.P_CAL+b,s=Math.sqrt(1+v*v),y=(v+this.v1)/(s+this.s1);this.v1=v;this.s1=s;
   const ul=this.lo.proc(u);this.buf[i]=m*Mk*(y-fb)+(1-m)*u-cm*(ul+(1-w)*(u-ul));}
  return base+this.dc.proc(this.os.down(this.buf,this.hook));}};
M.synth=(amps,f0,fs,n,gain=1)=>{const x=new Float64Array(n),w=2*Math.PI*f0/fs;for(let k=0;k<n;k++){let s=0;for(let j=0;j<amps.length;j++)if(amps[j])s+=amps[j]*Math.sin((j+1)*w*k);x[k]=gain*s;}return x;};
M.goertzel=(x,fs,hz)=>{let re=0,im=0;const N=x.length;for(let k=0;k<N;k++){const w=0.5-0.5*Math.cos(2*Math.PI*k/N);re+=x[k]*w*Math.cos(2*Math.PI*hz*k/fs);im-=x[k]*w*Math.sin(2*Math.PI*hz*k/fs);}return [re,im];};
module.exports=M;
