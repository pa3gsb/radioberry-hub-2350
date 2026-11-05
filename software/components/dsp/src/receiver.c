#include "radioberry/receiver.h"
#include "dsp_internal.h"
#include <math.h>

void rb_dsp_receiver_init(rb_dsp_receiver *r)
{
    rb_dsp_am_init(&r->am);
    rb_dsp_ssb_init(&r->ssb);
}

void rb_dsp_receiver_reset(rb_dsp_receiver *r)
{
    rb_dsp_am_reset(&r->am);
    rb_dsp_ssb_reset(&r->ssb);
}

bool rb_dsp_receiver_agc(rb_dsp_receiver *r,uint32_t mode,int32_t threshold)
{
    return rb_dsp_agc_set(&r->am.agc,mode,threshold)&&
        rb_dsp_agc_set(&r->ssb.agc,mode,threshold);
}

RB_DSP_HOT bool rb_dsp_receiver_pcm16(rb_dsp_receiver *r,rb_dsp_rx_mode mode,
                                     float i,float q,int16_t *sample)
{
    float audio;
    bool ready;
    switch(mode) {
    case RB_DSP_RX_AM: ready=rb_dsp_am_push(&r->am,i,q,&audio);break;
    case RB_DSP_RX_USB: ready=rb_dsp_ssb_push(&r->ssb,i,q,false,&audio);break;
    case RB_DSP_RX_LSB: ready=rb_dsp_ssb_push(&r->ssb,i,q,true,&audio);break;
    default: return false;
    }
    if(!ready)return false;
    *sample=(int16_t)lrintf(audio*32767.f);
    return true;
}
