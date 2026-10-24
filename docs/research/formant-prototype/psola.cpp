#include "formant_lib.h"
// Idealised TD-PSOLA with exact pitch marks: shows the best case for grain-based formant preservation.
int main(){
  printf("f0in shift | PSOLA(2P Hann)  PSOLA(win=2*min(P,P'))\n");
  for(double f0:{220.0,330.0,440.0,660.0}) for(int st:{-12,-7,7,12}){
    const double ratio=exp2(st/12.0); int n=int(sr); auto x=synth(f0,n); double P=sr/f0, Q=P/ratio;
    printf("%4.0f %+4d |",f0,st);
    for(int mode=0;mode<2;++mode){ vector<float> y(n,0); double half= mode==0?P:min(P,Q);
      for(double t=2*P;t<n-2*P;t+=Q){ double src=std::round(t/P)*P; // nearest input mark
        for(int i=-int(half);i<=int(half);++i){ int o=int(t)+i, s=int(src)+i; if(o<0||o>=n||s<0||s>=n) continue;
          double w=.5+.5*cos(M_PI*i/half); y[o]+=float(w*x[s]); } }
      printf("  %8.2f dB",envError(y,f0*ratio)); }
    printf("\n"); }
}
