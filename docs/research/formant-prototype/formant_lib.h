#pragma once
// Shared synthetic-vowel generator, envelope-error metric and LPC used by the formant prototypes.
// Prototype: does LPC whitening -> existing PeriodShifter -> LPC resynthesis preserve formants?
#include "dsp/PitchEngine.h"
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>
using namespace std;
const double sr=48000;
// Known envelope: source tilt * 4 formant resonances (vowel /a/-like, female)
double envRaw(double f){
  const double F[]={850,1220,2810,3800}, B[]={90,110,160,200};
  complex<double> z=polar(1.0,-2*M_PI*f/sr), h=1;
  for(int i=0;i<4;++i){ double r=exp(-M_PI*B[i]/sr), th=2*M_PI*F[i]/sr; h/= (1.0-2*r*cos(th)*z+r*r*z*z); }
  return abs(h)/ (1+f/300.0); // ~-6 dB/oct source tilt
}
double env(double f){ const double floor=1e-3*envRaw(850); return sqrt(envRaw(f)*envRaw(f)+floor*floor); }
vector<float> synth(double f0,int n){ vector<float> x(n); int K=int(14000/f0);
  vector<double> amp(K+1),ph(K+1); for(int k=1;k<=K;++k){amp[k]=env(k*f0);ph[k]=k*k*.7;}
  double norm=0; for(int k=1;k<=K;++k) norm+=amp[k];
  for(int i=0;i<n;++i){double s=0;for(int k=1;k<=K;++k)s+=amp[k]*sin(2*M_PI*k*f0*i/sr+ph[k]);x[i]=float(.5*s/norm);} return x; }
// harmonic amplitude error vs ideal envelope, 200-5000 Hz, offset-normalized RMS dB
double envError(const vector<float>& y,double f0){
  int n=int(sr*.25), start=int(y.size())-n-10; vector<double> d;
  for(int k=1;k*f0<4500;++k){ double f=k*f0; if(f<200) continue; complex<double> acc=0;
    for(int i=0;i<n;++i){double w=.5-.5*cos(2*M_PI*i/(n-1)); acc+=w*double(y[start+i])*polar(1.0,-2*M_PI*f*i/sr);}
    d.push_back(20*log10(abs(acc)+1e-12)-20*log10(env(f))); }
  double m=0; for(double v:d)m+=v; m/=d.size(); double e=0; for(double v:d)e+=(v-m)*(v-m); return sqrt(e/d.size()); }
struct Lpc { int p; double lagBw; vector<double> a; // a[0]=1
  Lpc(int order,double bw):p(order),lagBw(bw),a(order+1,0){a[0]=1;}
  void analyse(const vector<float>& x,int end,double f0){ int N=1024; vector<double> w(N), r(p+1,0);
    for(int i=0;i<N;++i){int j=end-N+i; w[i]=(j>=0?x[j]:0)*(.5-.5*cos(2*M_PI*(i+.5)/N));}
    for(int k=0;k<=p;++k){for(int i=k;i<N;++i)r[k]+=w[i]*w[i-k];
      double bw=lagBw*f0; r[k]*=exp(-.5*pow(2*M_PI*bw*k/sr,2));}
    r[0]=r[0]*1.0001+1e-9; vector<double> A(p+1,0),tmp; A[0]=1; double E=r[0];
    for(int i=1;i<=p;++i){double acc=r[i];for(int j=1;j<i;++j)acc+=A[j]*r[i-j]; double k=-acc/E; tmp=A; for(int j=1;j<i;++j)A[j]=tmp[j]+k*tmp[i-j]; A[i]=k; E*=1-k*k;}
    double g=1; for(int i=1;i<=p;++i){g*=.995;A[i]*=g;} a=A; } };
