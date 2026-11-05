#include "radioberry/spectrum.h"
#include <math.h>
#include <string.h>

void rb_dsp_spectrum_reset(rb_dsp_spectrum *s)
{
    s->used=s->averages=0;
    memset(s->power,0,sizeof(s->power));
}

void rb_dsp_spectrum_init(rb_dsp_spectrum *s)
{
    memset(s,0,sizeof(*s));
    for(unsigned n=0;n<RB_DSP_SPECTRUM_BINS;++n)
        s->window[n]=.5f-.5f*cosf(6.283185307179586f*n/RB_DSP_SPECTRUM_BINS);
}

int rb_dsp_spectrum_push(rb_dsp_spectrum *s,float i,float q)
{
    s->iq[s->used++]=(rb_iq_f32_t){i,q};
    ++s->pairs;
    if(s->used<RB_DSP_SPECTRUM_BINS)return 0;
    for(unsigned n=0;n<RB_DSP_SPECTRUM_BINS;++n){
        s->iq[n].i*=s->window[n];s->iq[n].q*=s->window[n];
    }
    rb_fft_forward(s->iq,RB_DSP_SPECTRUM_BINS);
    s->used=0;
    ++s->windows;
    for(unsigned n=0;n<RB_DSP_SPECTRUM_BINS;++n){
        const rb_iq_f32_t v=s->iq[(n+RB_DSP_SPECTRUM_BINS/2u)%RB_DSP_SPECTRUM_BINS];
        const float power=(v.i*v.i+v.q*v.q)/65536.f; /* Hann sum = 256. */
        if(!isfinite(power)||!isfinite(s->power[n]+power)){
            ++s->invalid_fft;rb_dsp_spectrum_reset(s);return -1;
        }
        s->power[n]+=power;
    }
    ++s->averages;
    return 1;
}

float rb_dsp_spectrum_level(const rb_dsp_spectrum *s,unsigned bin)
{
    if(bin>=RB_DSP_SPECTRUM_BINS||!s->averages)return -200.f;
    const float power=s->power[bin]/s->averages;
    return power>0.f?10.f*log10f(power):-200.f;
}
