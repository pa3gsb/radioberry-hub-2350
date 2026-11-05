#include "radioberry/am.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f

void rb_dsp_am_reset(rb_dsp_am *a)
{
    memset(a->state,0,sizeof(a->state)); memset(a->history,0,sizeof(a->history));
    a->dc=a->highpass=0.f;rb_dsp_agc_reset(&a->agc);a->head=a->decimation=0u; a->samples=0u;
}
void rb_dsp_am_init(rb_dsp_am *a)
{
    /* Sixth-order Butterworth, 4 kHz complex low-pass. Each section uses the
     * normalized LPF equations in https://www.w3.org/TR/audio-eq-cookbook/ . */
    const float quality[3]={.5176380902f,.7071067812f,1.931851653f};
    const float omega=2.f*PI*4000.f/48000.f, c=cosf(omega), s=sinf(omega);
    for(unsigned n=0;n<3u;++n) {
        float alpha=s/(2.f*quality[n]), inv=1.f/(1.f+alpha);
        float *b=a->coefficients[n];
        b[0]=(1.f-c)*.5f*inv; b[1]=(1.f-c)*inv; b[2]=b[0];
        b[3]=-2.f*c*inv; b[4]=(1.f-alpha)*inv;
    }
    /* Hamming-windowed sinc: audio anti-alias filter at the input rate.
     * Filter the envelope BEFORE dropping samples, not after aliasing. */
    float sum=0.f;
    for(unsigned n=0;n<RB_AM_FIR_TAPS;++n) {
        int x=(int)n-(int)(RB_AM_FIR_TAPS/2u);
        float sinc=x?sinf(2.f*PI*3300.f/48000.f*x)/(PI*x):2.f*3300.f/48000.f;
        a->fir[n]=sinc*(.54f-.46f*cosf(2.f*PI*n/(RB_AM_FIR_TAPS-1u))); sum+=a->fir[n];
    }
    for(unsigned n=0;n<RB_AM_FIR_TAPS;++n)a->fir[n]/=sum;
    rb_dsp_agc_init(&a->agc,48000,.35f);rb_dsp_am_reset(a);
}
bool rb_dsp_am_push(rb_dsp_am *a,float i,float q,float *audio)
{
    if(!isfinite(i)||!isfinite(q)) { rb_dsp_am_reset(a); return false; }
    for(unsigned n=0;n<3u;++n) {
        const float *b=a->coefficients[n];
        float yi=b[0]*i+a->state[n][0][0], yq=b[0]*q+a->state[n][1][0];
        a->state[n][0][0]=b[1]*i-b[3]*yi+a->state[n][0][1];
        a->state[n][0][1]=b[2]*i-b[4]*yi;
        a->state[n][1][0]=b[1]*q-b[3]*yq+a->state[n][1][1];
        a->state[n][1][1]=b[2]*q-b[4]*yq; i=yi; q=yq;
    }
    const float envelope=sqrtf(i*i+q*q);
    a->highpass=envelope-a->dc+.999345716f*a->highpass; /* 5 Hz DC rejection. */
    a->dc=envelope;
    rb_dsp_agc_detect(&a->agc,envelope);
    a->history[a->head]=a->highpass;
    a->head=(a->head+1u)%RB_AM_FIR_TAPS;
    if(a->samples<5760u)++a->samples;
    if(++a->decimation<4u)return false;
    a->decimation=0u; float filtered=0.f; unsigned index=a->head;
    for(unsigned n=0;n<RB_AM_FIR_TAPS;++n) {
        index=index?index-1u:RB_AM_FIR_TAPS-1u;
        filtered+=a->fir[n]*a->history[index];
    }
    const float gain=a->agc.gain;
    /* Silence the initial carrier step; fade in over the following 20 ms. */
    const float fade=a->samples<=4800u?0.f:fminf(1.f,(a->samples-4800u)/960.f);
    *audio=fmaxf(-.9f,fminf(.9f,filtered*gain))*fade;
    return true;
}
