#ifndef RADIOBERRY_DSP_AM_H
#define RADIOBERRY_DSP_AM_H
#include <stdbool.h>
#include <stdint.h>
#include "radioberry/agc.h"
#define RB_AM_FIR_TAPS 97u
/* Fixed 48 kHz complex input -> 12 kHz mono. Caller owns state; no hardware,
 * allocation or network dependencies. Input is normalized to ADC full scale. */
typedef struct {
    float coefficients[3][5], state[3][2][2];
    float fir[RB_AM_FIR_TAPS], history[RB_AM_FIR_TAPS];
    float dc, highpass;
    rb_dsp_agc agc;
    unsigned head, decimation;
    uint32_t samples;
} rb_dsp_am;
void rb_dsp_am_init(rb_dsp_am *am);
void rb_dsp_am_reset(rb_dsp_am *am);
/* True every fourth input pair; output bounded to +/-0.9. */
bool rb_dsp_am_push(rb_dsp_am *am, float i, float q, float *audio);
#endif
