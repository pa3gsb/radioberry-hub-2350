#include "radioberry/agc.h"
#include "dsp_internal.h"
#include <math.h>
#include <string.h>
bool rb_dsp_agc_set(rb_dsp_agc *a,uint32_t mode,int32_t threshold)
{
    if(!a||!a->rate||mode>RB_AGC_SLOW||threshold<RB_AGC_THRESHOLD_MIN_DBFS||threshold>RB_AGC_THRESHOLD_MAX_DBFS)return false;
    const float seconds=mode==RB_AGC_FAST?.05f:mode==RB_AGC_SLOW?1.5f:.5f;
    a->mode=mode;a->threshold_dbfs=threshold;a->threshold=powf(10.f,threshold/20.f);
    a->attack=-expm1f(-1.f/(a->rate*.010f));a->release=-expm1f(-1.f/(a->rate*seconds));
    a->ramp=-expm1f(-1.f/(a->rate*.005f));return true;
}
bool rb_dsp_agc_init(rb_dsp_agc *a,uint32_t rate,float target)
{
    if(!a||!rate||!isfinite(target)||target<=0.f||target>1.f)return false;
    memset(a,0,sizeof(*a));a->rate=rate;a->target=target;a->gain=1.f;
    return rb_dsp_agc_set(a,RB_AGC_NORMAL,RB_AGC_DEFAULT_THRESHOLD_DBFS);
}
void rb_dsp_agc_reset(rb_dsp_agc *a){a->level=0.f;a->gain=1.f;}
RB_DSP_HOT void rb_dsp_agc_detect(rb_dsp_agc *a,float envelope)
{
    if(!isfinite(envelope)||envelope<0.f){rb_dsp_agc_reset(a);return;}
    a->level+=(envelope>a->level?a->attack:a->release)*(envelope-a->level);
    const float requested=a->mode==RB_AGC_OFF?1.f:fminf(10000.f,a->target/fmaxf(a->level,a->threshold));
    a->gain+=a->ramp*(requested-a->gain);
}
