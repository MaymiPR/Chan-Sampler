import numpy as np
def biquad(x,b,a):
    y=np.zeros_like(x); x1=x2=y1=y2=0.0
    for i in range(len(x)):
        v=b[0]*x[i]+b[1]*x1+b[2]*x2-a[1]*y1-a[2]*y2; x2=x1; x1=x[i]; y2=y1; y1=v; y[i]=v
    return y
def rbj_lp(f0,Q,sr=44100):
    w=2*np.pi*f0/sr; al=np.sin(w)/(2*Q); c=np.cos(w); a0=1+al
    return np.array([(1-c)/2,1-c,(1-c)/2])/a0, np.array([1,-2*c,1-al])/a0
def svf(x,f,q):
    lp=bp=0.0; y=np.zeros_like(x)
    for i in range(len(x)):
        lp=lp+f*bp; hp=x[i]-lp-q*bp; bp=f*hp+bp; y[i]=lp
    return y
