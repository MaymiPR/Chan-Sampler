"""A one-pole low-pass with resonance feedback, for fitting FL's Fast LP filter."""
import numpy as np
def onepole_res(x, f, r):
    y=np.zeros_like(x); y1=0.0; y2=0.0
    for i in range(len(x)):
        v=y1+f*(x[i]-y1)+r*(y1-y2); y2=y1; y1=v; y[i]=v
    return y
