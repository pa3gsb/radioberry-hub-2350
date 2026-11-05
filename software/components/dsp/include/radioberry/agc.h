#ifndef RADIOBERRY_DSP_AGC_H
#define RADIOBERRY_DSP_AGC_H
#include <stdbool.h>
#include <stdint.h>
#define RB_AGC_OFF 0u
#define RB_AGC_FAST 1u
#define RB_AGC_NORMAL 2u
#define RB_AGC_SLOW 3u
#define RB_AGC_THRESHOLD_MIN_DBFS (-100)
#define RB_AGC_THRESHOLD_MAX_DBFS (-20)
#define RB_AGC_DEFAULT_THRESHOLD_DBFS (-100)
typedef struct {
    float level,gain,target,threshold,attack,release,ramp;
    uint32_t rate,mode;
    int32_t threshold_dbfs;
} rb_dsp_agc;
bool rb_dsp_agc_init(rb_dsp_agc *agc,uint32_t rate,float target);
bool rb_dsp_agc_set(rb_dsp_agc *agc,uint32_t mode,int32_t threshold_dbfs);
void rb_dsp_agc_reset(rb_dsp_agc *agc);
/* Envelope is normalized amplitude. Threshold bounds gain below the knee,
 * not a squelch. OFF smoothly returns to unity gain; detector keeps tracking. */
void rb_dsp_agc_detect(rb_dsp_agc *agc,float envelope);
#endif
