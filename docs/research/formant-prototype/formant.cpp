#include "formant_lib.h"
int main(){
  printf("f0in  shift | uncorrected  LPC(p,lagBw): p24/0 p40/0 p24/.5 p40/.5 p40/.8  | unshifted-ref\n");
  for(double f0:{220.0,330.0,440.0,660.0}) for(int st:{-12,-7,7,12}){
    const double ratio=exp2(st/12.0); int n=int(sr*1.0); auto x=synth(f0,n);
    auto shiftOnly=[&](const vector<float>& in){ ape::PeriodShifter s; s.prepare(sr); s.setRange(12); vector<float> y(n);
      for(int i=0;i<n;++i) y[i]=s.process(in[i],ratio,sr/f0); return y; };
    printf("%4.0f %+4d | %8.2f dB ",f0,st,envError(shiftOnly(x),f0*ratio));
    for(auto cfg: vector<pair<int,double>>{{24,0},{40,0},{24,.5},{40,.5},{40,.8}}){
      Lpc lpc(cfg.first,cfg.second); vector<float> e(n),y(n); vector<double> hx(cfg.first+1,0), hy(cfg.first+1,0);
      // whiten (coeffs per 64-sample hop, past-only window)
      vector<vector<double>> frames;
      for(int i=0;i<n;++i){ if(i%64==0){lpc.analyse(x,i,f0); frames.push_back(lpc.a);} 
        double v=x[i]; for(int k=1;k<=cfg.first;++k) v+=lpc.a[k]*(i-k>=0?x[i-k]:0); e[i]=float(v);}
      ape::PeriodShifter s; s.prepare(sr); s.setRange(12); vector<double> ybuf(n,0);
      for(int i=0;i<n;++i){ double r=s.process(e[i],ratio,sr/f0); int age=int(s.currentDelayMs()*sr/1000);
        const auto& A=frames[max(0,(i-age))/64]; double v=r; for(int k=1;k<=cfg.first;++k) v-=A[k]*(i-k>=0?ybuf[i-k]:0); ybuf[i]=v; y[i]=float(v);}
      printf("%6.2f ",envError(y,f0*ratio)); }
    printf("| %.2f\n",envError(x,f0));
  }
}
