#include "radioberry/ssb.h"
#include "dsp_internal.h"
#include <math.h>
#include <string.h>
#define PI 3.14159265358979323846f
static float lowpass(int x,unsigned n,unsigned taps,float cut,float rate)
{
    const float sinc=x?sinf(2.f*PI*cut/rate*x)/(PI*x):2.f*cut/rate;
    return sinc*(.54f-.46f*cosf(2.f*PI*n/(taps-1u)));
}
void rb_dsp_ssb_reset(rb_dsp_ssb *s)
{
    memset(s->dec_i,0,sizeof(s->dec_i));memset(s->dec_q,0,sizeof(s->dec_q));
    memset(s->history_i,0,sizeof(s->history_i));memset(s->history_q,0,sizeof(s->history_q));
    s->previous=s->highpass=0;rb_dsp_agc_reset(&s->agc);s->dec_head=s->channel_head=s->decimation=0;s->samples=0;
}
void rb_dsp_ssb_init(rb_dsp_ssb *s)
{
    float sum=0;
    for(unsigned n=0;n<RB_SSB_DEC_TAPS;++n){
        s->dec_coeff[n]=lowpass((int)n-(int)(RB_SSB_DEC_TAPS/2u),n,RB_SSB_DEC_TAPS,4000,48000);
        sum+=s->dec_coeff[n];
    }
    for(unsigned n=0;n<RB_SSB_DEC_TAPS;++n)s->dec_coeff[n]/=sum;
    sum=0;
    for(unsigned n=0;n<RB_SSB_CHANNEL_TAPS;++n){
        const int x=(int)n-(int)(RB_SSB_CHANNEL_TAPS/2u);
        const float h=lowpass(x,n,RB_SSB_CHANNEL_TAPS,1200,12000);
        /* History runs oldest -> newest. Reverse the impulse-response phase
         * so I*real - Q*imag selects positive-frequency (USB) input. */
        const float phase=-2.f*PI*1500.f/12000.f*x;
        s->channel_i[n]=h*cosf(phase);s->channel_q[n]=h*sinf(phase);sum+=h;
    }
    for(unsigned n=0;n<RB_SSB_CHANNEL_TAPS;++n){s->channel_i[n]/=sum;s->channel_q[n]/=sum;}
    rb_dsp_agc_init(&s->agc,12000,.25f);rb_dsp_ssb_reset(s);
}
RB_DSP_HOT bool rb_dsp_ssb_push(rb_dsp_ssb *s,float i,float q,bool lsb,float *audio)
{
    if(!isfinite(i)||!isfinite(q)){rb_dsp_ssb_reset(s);return false;}
    if(lsb)q=-q; /* Conjugate once per input pair; one positive-band filter. */
    const unsigned head=s->dec_head;
    s->dec_i[head]=s->dec_i[head+RB_SSB_DEC_TAPS]=i;
    s->dec_q[head]=s->dec_q[head+RB_SSB_DEC_TAPS]=q;
    s->dec_head=head+1u==RB_SSB_DEC_TAPS?0u:head+1u;
    if(s->samples<5760u)++s->samples;
    if(++s->decimation<4u)return false;
    s->decimation=0;i=q=0;
    const float *di=s->dec_i+s->dec_head,*dq=s->dec_q+s->dec_head;
    /* The real decimator is symmetric: fold pairs before multiplying.
     * Keep the same 97-tap response and the same 4:1 sample timing. */
    for(unsigned n=0;n<RB_SSB_DEC_TAPS/2u;++n){
        const unsigned other=RB_SSB_DEC_TAPS-1u-n;
        i+=s->dec_coeff[n]*(di[n]+di[other]);
        q+=s->dec_coeff[n]*(dq[n]+dq[other]);
    }
    i+=s->dec_coeff[RB_SSB_DEC_TAPS/2u]*di[RB_SSB_DEC_TAPS/2u];
    q+=s->dec_coeff[RB_SSB_DEC_TAPS/2u]*dq[RB_SSB_DEC_TAPS/2u];
    const unsigned ch=s->channel_head;
    s->history_i[ch]=s->history_i[ch+RB_SSB_CHANNEL_TAPS]=i;
    s->history_q[ch]=s->history_q[ch+RB_SSB_CHANNEL_TAPS]=q;
    s->channel_head=ch+1u==RB_SSB_CHANNEL_TAPS?0u:ch+1u;
    float filtered=0;
    const float *hi=s->history_i+s->channel_head,*hq=s->history_q+s->channel_head;
    /* Real coefficients are symmetric; imaginary coefficients antisymmetric.
     * Only the real output is needed, so halve both sets of products. */
    for(unsigned n=0;n<RB_SSB_CHANNEL_TAPS/2u;++n){
        const unsigned other=RB_SSB_CHANNEL_TAPS-1u-n;
        filtered+=s->channel_i[n]*(hi[n]+hi[other])-s->channel_q[n]*(hq[n]-hq[other]);
    }
    filtered+=s->channel_i[RB_SSB_CHANNEL_TAPS/2u]*hi[RB_SSB_CHANNEL_TAPS/2u];
    s->highpass=filtered-s->previous+.989582665f*s->highpass; /* 20 Hz DC rejection. */
    s->previous=filtered;filtered=s->highpass;
    rb_dsp_agc_detect(&s->agc,fabsf(filtered));
    const float gain=s->agc.gain;
    const float fade=s->samples<=4800u?0.f:s->samples>=5760u?1.f:(s->samples-4800u)*(1.f/960.f);
    *audio=fmaxf(-.9f,fminf(.9f,filtered*gain))*fade;return true;
}
