// Offline-only Blackman-windowed sinc decimator. SPDX-License-Identifier: MIT
#include <math.h>
#include <stdlib.h>
// Centered, unity-DC FIR, 64 host frames support, cutoff 0.45 host Fs.
int decimate(const float *in,int n,int factor,float *out) {
    int half=32*factor,len=2*half+1;
    double *h=malloc((size_t)len*sizeof(double));if(!h)return -1;
    double sum=0,fc=.45/factor;
    for(int i=-half;i<=half;++i) {
        double w=.42+.5*cos(3.141592653589793*i/half)+.08*cos(2*3.141592653589793*i/half);
        h[i+half]=w*(i?sin(2*3.141592653589793*fc*i)/(3.141592653589793*i):2*fc);sum+=h[i+half];
    }
    for(int j=0;j<n/factor;++j) {
        double y=0;int center=j*factor;
        for(int i=-half;i<=half;++i) if(center+i>=0 && center+i<n)y+=in[center+i]*h[i+half];
        out[j]=(float)(y/sum);
    }
    free(h);return n/factor;
}
