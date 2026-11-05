#include "websdr_receiver.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
#define PI 3.14159265358979323846
static rb_dsp_ssb dsp;
/* Independent, unfolded convolution oracle. Random I/Q exercises every tap
 * and ring position, including the antisymmetric Q terms and both sidebands. */
static bool matches_full_convolution(bool lsb)
{
    rb_dsp_ssb_init(&dsp);rb_dsp_agc_set(&dsp.agc,RB_AGC_OFF,-100);
    float di[RB_SSB_DEC_TAPS]={0},dq[RB_SSB_DEC_TAPS]={0};
    float hi[RB_SSB_CHANNEL_TAPS]={0},hq[RB_SSB_CHANNEL_TAPS]={0};
    float previous=0,highpass=0;uint32_t random=12345;unsigned output_count=0;
    for(unsigned n=0;n<12000u;++n){
        random=random*1664525u+1013904223u;float i=((int32_t)(random>>8)-8388608)*(1.f/33554432.f);
        random=random*1664525u+1013904223u;float q=((int32_t)(random>>8)-8388608)*(1.f/33554432.f),audio;
        memmove(di,di+1,(RB_SSB_DEC_TAPS-1u)*sizeof(float));di[RB_SSB_DEC_TAPS-1u]=i;
        memmove(dq,dq+1,(RB_SSB_DEC_TAPS-1u)*sizeof(float));dq[RB_SSB_DEC_TAPS-1u]=lsb?-q:q;
        const bool ready=rb_dsp_ssb_push(&dsp,i,q,lsb,&audio);
        if(ready!=((n+1u)%4u==0u))return false;
        if(!ready)continue;
        float fi=0,fq=0;
        for(unsigned k=0;k<RB_SSB_DEC_TAPS;++k){fi+=dsp.dec_coeff[k]*di[k];fq+=dsp.dec_coeff[k]*dq[k];}
        memmove(hi,hi+1,(RB_SSB_CHANNEL_TAPS-1u)*sizeof(float));hi[RB_SSB_CHANNEL_TAPS-1u]=fi;
        memmove(hq,hq+1,(RB_SSB_CHANNEL_TAPS-1u)*sizeof(float));hq[RB_SSB_CHANNEL_TAPS-1u]=fq;
        float filtered=0;
        for(unsigned k=0;k<RB_SSB_CHANNEL_TAPS;++k)filtered+=dsp.channel_i[k]*hi[k]-dsp.channel_q[k]*hq[k];
        highpass=filtered-previous+.989582665f*highpass;previous=filtered;
        float fade=fmaxf(0,fminf(1,((float)n+1-4800)/960));
        float expected=fmaxf(-.9f,fminf(.9f,highpass))*fade;
        if(fabsf(audio-expected)>2e-6f)return false;
        ++output_count;
    }
    return output_count==3000u;
}
static double measure(bool lsb,float level,float opposite,double *rejected)
{
    rb_dsp_ssb_init(&dsp);double real=0,imag=0,other_real=0,other_imag=0;unsigned count=0;
    for(unsigned n=0;n<96000u;++n) {
        double a=2*PI*1000*n/48000.,b=2*PI*1900*n/48000.;
        float i=(float)(level*cos(a)+opposite*cos(b));
        float q=(float)((lsb?-1:1)*(level*sin(a)-opposite*sin(b))),audio;
        if(rb_dsp_ssb_push(&dsp,i,q,lsb,&audio)&&n>=48000u) {
            if(!isfinite(audio)||fabsf(audio)>.90001f)return -1;
            real+=audio*cos(a);imag+=audio*sin(a);other_real+=audio*cos(b);other_imag+=audio*sin(b);++count;
        }
    }
    *rejected=2*hypot(other_real,other_imag)/count;
    return 2*hypot(real,imag)/count;
}
int rb_test_ssb(void)
{
    CHECK(matches_full_convolution(false));CHECK(matches_full_convolution(true));
    double unwanted,usb=measure(false,.03f,.03f,&unwanted),usb_bad=unwanted;
    CHECK(usb>.15&&usb<.4);CHECK(usb_bad<usb*.0031623); /* At least 50 dB with equal tones. */
    double lsb=measure(true,.03f,.03f,&unwanted),lsb_bad=unwanted;
    CHECK(fabs(lsb/usb-1)<.01&&lsb_bad<lsb*.0031623);
    double weak=measure(false,.0003f,.0003f,&unwanted);CHECK(fabs(weak/usb-1)<.03);
    rb_dsp_ssb_init(&dsp);float audio;unsigned outputs=0;
    for(unsigned n=0;n<4800;++n)if(rb_dsp_ssb_push(&dsp,0,0,false,&audio)){CHECK(audio==0);++outputs;}
    CHECK(outputs==1200);CHECK(!rb_dsp_ssb_push(&dsp,NAN,0,false,&audio));
    rb_dsp_ssb_reset(&dsp);
    for(unsigned n=0;n<96000;++n)if(rb_dsp_ssb_push(&dsp,.1f,0,false,&audio)&&n>=48000u)CHECK(fabsf(audio)<.00001f);
    rb_dsp_ssb_reset(&dsp);CHECK(!rb_dsp_ssb_push(&dsp,.1f,0,false,&audio));
    rb_am_queue q;rb_am_queue_init(&q);CHECK(!rb_am_queue_open_mode(&q,1,3));
    uint8_t frame[RB_AM_FRAME_BYTES];int16_t pcm[240]={0};
    CHECK(rb_am_queue_open_mode(&q,1,RB_RX_USB));rb_am_queue_publish(&q,pcm,7100000,1);
    CHECK(rb_am_queue_read(&q,frame)&&frame[5]==4);rb_am_queue_close(&q);
    CHECK(rb_am_queue_open_mode(&q,1,RB_RX_LSB));rb_am_queue_gap(&q);rb_am_queue_publish(&q,pcm,7100000,1);
    CHECK(rb_am_queue_read(&q,frame)&&frame[5]==9);
    printf("SSB: USB %.5f / LSB %.5f; equal opposite tone rejection %.1f / %.1f dB; weak-signal gain, pacing, silence/reset and mode framing passed.\n",usb,lsb,20*log10(usb/usb_bad),20*log10(lsb/lsb_bad));
    return 0;
}
