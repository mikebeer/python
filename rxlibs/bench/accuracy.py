#!/usr/bin/env python3
"""Accuracy of the special functions and distributions against scipy (relative error 1e-12 allowed).
Builds bench/accuracy_driver.c against core/ and compares ~1100 points.  usage: python3 bench/accuracy.py"""
import os, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); C = os.path.join(HERE, '..', 'core')
BIN = os.path.join(HERE, '..', 'build', 'accuracy_driver')
os.makedirs(os.path.dirname(BIN), exist_ok=True)
subprocess.check_call(['gcc', '-O2', '-I' + C, os.path.join(HERE, 'accuracy_driver.c')] + [os.path.join(C, f) for f in
    ('rxsp.c', 'rxsp_dist.c', 'rxnp.c', 'rxnp_la.c', 'rxnp_lapack.c', 'rxnp_fft.c', 'rxnp_rng.c', 'rxnp_pcg.c', 'rxnp_idx.c')] + ['-lm', '-o', BIN])
import numpy as np, sys
from scipy import special as sp, stats as st
cases=[]  # (op,name,x,a,b,loc,scale,expected)
def add(op,nm,x,a=0,b=0,l=0,s=1,e=None): cases.append((op,nm,x,a,b,l,s,e))
for x in [0.1,0.5,1,1.5,2.5,3.7,5,9.9,10,17.3,50,100.5,150]: add('gamma','',x,e=sp.gamma(x)); add('lgamma','',x,e=sp.gammaln(x))
for x in [-0.5,-1.5,-3.2]: add('gamma','',x,e=sp.gamma(x)); add('lgamma','',x,e=sp.gammaln(x))
for x in [1e-8,0.01,0.3,1,1.7,2.4,2.9,3.1,4.5,6,10]: add('erf','',x,e=sp.erf(x)); add('erfc','',x,e=sp.erfc(x)); add('erfc','',-x,e=sp.erfc(-x))
for y in [1e-10,0.001,0.1,0.5,0.8,0.9,0.99,0.999999,1-1e-12,-0.7]: add('erfinv','',y,e=sp.erfinv(y))
for a,b in [(0.5,0.5),(2,3),(10.5,0.25),(100,200),(1,1)]: add('beta','',a,b,e=sp.beta(a,b))
for a,b,x in [(2,3,.4),(0.5,0.5,0.01),(10,20,.3),(50,30,.7),(1,1,.5),(200,300,.39),(0.3,5,.9),(5,0.3,.9),(3,4,1e-6)]: add('betainc','',a,b,x,e=sp.betainc(a,b,x))
for a,x in [(0.5,0.1),(1,1),(3,2),(3,10),(10,5),(10,20),(100,90),(100,120),(0.1,3),(500,520)]: add('gammainc','',a,x,e=sp.gammainc(a,x)); add('gammaincc','',a,x,e=sp.gammaincc(a,x))
D={'norm':[(),(1,2)],'t':[(2.5,),(10,),(30,),(1.0,)],'chi2':[(1,),(4,),(30,)],'f':[(3,7),(10,2.5),(1,1)],'gamma':[(0.5,),(2,),(9.5,)],'beta':[(0.5,0.5),(2,5),(10,3)],
   'expon':[()],'uniform':[()],'lognorm':[(0.5,),(1.2,)],'binom':[(10,0.3),(50,0.9),(1,0.5)],'poisson':[(0.7,),(4,),(30,)],'geom':[(0.2,),(0.7,)]}
xs={'norm':[-3,-.5,0,1.3,4],'t':[-4,-1,0,.5,2,8],'chi2':[.01,1,3,12,50],'f':[.1,1,2.5,9],'gamma':[.1,1,5,20],'beta':[.01,.3,.8,.99],'expon':[0,.2,3],'uniform':[-1,.3,1.5],'lognorm':[.2,1,5],
   'binom':[0,1,3,7,10,25,49],'poisson':[0,1,4,10,40],'geom':[1,2,5,12]}
qs=[1e-6,0.01,.25,.5,.9,.999,1-1e-9]
for nm,pl in D.items():
  for p in pl:
    for sc in ([(0,1)] if nm in('binom','poisson','geom') else [(0,1),(0.5,2.5)]):
      if nm=='norm': p2=(); loc,scale=(0,1) if sc==(0,1) else (1,2); 
      else: p2=p; loc,scale=sc
      if nm=='norm' and sc==(0.5,2.5): continue
      a=p2[0] if len(p2)>0 else 0; b=p2[1] if len(p2)>1 else 0
      d=getattr(st,nm)(*p2,loc=loc,scale=scale) if nm not in('binom','poisson','geom') else getattr(st,nm)(*p2,loc=loc)
      for x in xs[nm]:
        xx=x*scale+loc if nm not in('binom','poisson','geom') else x+loc
        add('pdf' ,nm,xx,a,b,loc,scale,d.pdf(xx) if nm not in('binom','poisson','geom') else d.pmf(xx))
        add('cdf',nm,xx,a,b,loc,scale,d.cdf(xx)); add('sf',nm,xx,a,b,loc,scale,d.sf(xx))
      for q in qs: add('ppf',nm,q,a,b,loc,scale,d.ppf(q))
inp=''.join(f"{o} {n or 'x'} {x!r} {a!r} {b!r} {l!r} {s!r}\n" for o,n,x,a,b,l,s,e in cases)
out=subprocess.run([BIN],input=inp,capture_output=True,text=True).stdout.split()
bad=0;worst=0
for c,v in zip(cases,out):
  v=float(v); e=c[7]
  if np.isnan(e) and np.isnan(v): continue
  if np.isinf(e) and v==e: continue
  err=abs(v-e)/max(abs(e),1e-300) if abs(e)>1e-300 else abs(v-e)
  if err>1e-12 and abs(v-e)>1e-15:
    bad+=1
    if bad<40: print('BAD',c[:7],'got',v,'exp',e,'rel %.2e'%err)
  else: worst=max(worst,err)
print(len(cases),'cases, bad',bad,'worst ok rel %.2e'%worst)
