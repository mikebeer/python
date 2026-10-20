#!/usr/bin/env python3
import numpy as np
from scipy import optimize, integrate
def v6(a): return str((np.round(np.asarray(a, dtype=float) * 1e6) / 1e6).tolist())
def sc(a): return str(round(float(a), 6) + 0.0)
o = []
say = lambda *a: o.append(' '.join(a))
cube = lambda x: x**3 - 2*x - 5
say("brentq", sc(optimize.brentq(cube, 2, 3, xtol=2e-12)))
say("newton_d", sc(optimize.newton(lambda x: np.cos(x) - x, 1.0, fprime=lambda x: -np.sin(x) - 1, tol=1e-10)))
say("secant", sc(optimize.newton(cube, 2.0, tol=1e-8)))
say("quad_gauss", sc(integrate.quad(lambda x: np.exp(-x*x), -2, 3, epsabs=1e-8, epsrel=1e-8)[0]))
say("quad_osc", sc(integrate.quad(lambda x: np.sin(10*x)*x + 1, 0, 4, epsabs=1e-8, epsrel=1e-8)[0]))
ross = lambda x: (1-x[0])**2 + 100*(x[1]-x[0]**2)**2
q = lambda x: (x[0]-3)**2 + 2*(x[1]+1)**2 + x[0]*x[1]
r = optimize.minimize(ross, [-1.2, 1.0], method='Nelder-Mead', options=dict(xatol=1e-8, fatol=1e-8, maxiter=10000, maxfev=10000))
say("nm_rosen", v6(r.x), sc(r.fun))
r = optimize.minimize(q, [0.0, 0.0], method='Nelder-Mead', options=dict(xatol=1e-7, fatol=1e-7))
say("nm_quad", v6(r.x), sc(r.fun))
r = optimize.minimize(ross, [-1.2, 1.0], method='BFGS', jac=lambda x: np.array([-2*(1-x[0])-400*x[0]*(x[1]-x[0]**2), 200*(x[1]-x[0]**2)]), options=dict(gtol=1e-8))
say("bfgs_rosen", v6(r.x), sc(r.fun))
r = optimize.minimize(q, [0.0, 0.0], method='BFGS', options=dict(gtol=1e-8))
say("bfgs_quad", v6(r.x), sc(r.fun))
rhs = lambda t, y: [-0.5*y[0], y[0]-y[1]]
te = [0.0, 0.5, 1.0, 2.5, 4.0]
s = integrate.solve_ivp(rhs, (0, 4), [1.0, 0.0], t_eval=te, rtol=1e-10, atol=1e-12)
say("ivp", v6(s.y))
s = integrate.solve_ivp(rhs, (0, 4), [1.0, 0.0], method='RK45')
say("ivp_default_steps", str(len(s.t)), v6(s.y[:, :3]), v6(s.t[:3]))
xd = np.array([0, .5, 1, 1.5, 2, 2.5, 3, 3.5, 4]); yd = np.array([5.2, 3.9, 3.0, 2.5, 2.1, 1.9, 1.6, 1.55, 1.4])
popt, pcov = optimize.curve_fit(lambda x, a, b, c: a*np.exp(-b*x)+c, xd, yd, p0=[4, 1, 1])
say("curve_fit", v6(popt), v6(np.sqrt(np.diag(pcov))))
import os
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 't_opt.expected'), 'w').write('\n'.join(o) + '\n')
