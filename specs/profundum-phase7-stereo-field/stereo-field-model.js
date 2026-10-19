// Profundum Phase 7 sizing model (plan S12). Run: node specs/profundum-phase7-stereo-field/stereo-field-model.js
// Sizing figures only, not compliance evidence. Analytic LR4 magnitudes, pan-staircase residual, law bounds.
const db=x=>20*Math.log10(x);
// LR4 magnitudes, r = f/fs_stage
const hp=r=>r**4/(1+r**4), lp=r=>1/(1+r**4);
for (const r of [0.5,0.475,0.25,2.5,1,0.75]) console.log('r',r,'HP dB',db(hp(r)).toFixed(2),'LP dB',db(lp(r)).toFixed(2));
// gravity-min effect at 2.5 fs for g_floor
for (const g of [0.5,0.25,0.18]) console.log('g',g,'dW at 2.5fs',(db(hp(2.5)+g*lp(2.5))-db(hp(2.5))).toFixed(3));
// sideband from pan staircase, zero-mean sawtooth of step dp
for (const dp of [0.003,0.0039,0.0044]) console.log('dp',dp,'residual dB',(10*Math.log10((Math.PI/4)**2*dp*dp/12)).toFixed(2));
// cycle slope per interval at 1Hz kappa .5
for (const fs of [44100,48000,96000,192000]) console.log('fs',fs,'cycle step/interval',(0.5*2*Math.PI*32/fs).toExponential(3));
// crossover smoother settle: 380 Hz handoff within 32 samples at 0.1ms
for (const fs of [44100,48000,96000,192000]) console.log('fs',fs,'residual Hz',(380*Math.exp(-5000/(0.1*fs)*32)).toExponential(3));
// law c at fc, Gravity 0, gamma 1, knee 0.25 oct below, upper 2 oct above
const c=(oct,G,g)=>{const lo=-0.25*(1-G),hi=2; if(oct<=lo)return 0; if(oct>=hi)return 1; const x=(oct-lo)/(hi-lo); return 0.5-0.5*Math.cos(Math.PI*x**g);};
for (const g of [1,2,4]) console.log('gamma',g,'c(fc) G0',c(0,0,g).toFixed(4),'rho floor',Math.cos(Math.PI*c(0,0,g)/2).toFixed(5),'c(2fc)',c(1,0,g).toFixed(3),'c(2fc)G1',c(1,1,g).toFixed(3));
// group delay of LR4 allpass at fs_stage
for (const f of [120,240,500]) console.log('fs_stage',f,'gd ms',(2*Math.SQRT2/(2*Math.PI*f)*1000).toFixed(3));
// Q for pan p
for (const p of [0.03,0.09,1]) console.log('p',p,'Q dB',(10*Math.log10((1+Math.cos(Math.PI*p/2))/2)).toFixed(4));
