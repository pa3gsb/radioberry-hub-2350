#ifndef RADIOBERRY_DSP_SSB_H
#define RADIOBERRY_DSP_SSB_H
#include <stdbool.h>
#include <stdint.h>
#include "radioberry/agc.h"
#define RB_SSB_DEC_TAPS 97u
#define RB_SSB_CHANNEL_TAPS 257u
/* Streaming 48 kHz complex -> 12 kHz mono; USB/LSB pass 300..2700 Hz.
 * Input Q orientation matches positive FFT frequency. No hardware/heap.
 * Reset on a sample discontinuity or sideband change. */
typedef struct {
    float dec_coeff[RB_SSB_DEC_TAPS];
    float dec_i[2u*RB_SSB_DEC_TAPS],dec_q[2u*RB_SSB_DEC_TAPS];
    float channel_i[RB_SSB_CHANNEL_TAPS],channel_q[RB_SSB_CHANNEL_TAPS];
    float history_i[2u*RB_SSB_CHANNEL_TAPS],history_q[2u*RB_SSB_CHANNEL_TAPS];
    float previous,highpass;
    rb_dsp_agc agc;
    unsigned dec_head,channel_head,decimation;
    uint32_t samples;
} rb_dsp_ssb;
void rb_dsp_ssb_init(rb_dsp_ssb *ssb);
void rb_dsp_ssb_reset(rb_dsp_ssb *ssb);
bool rb_dsp_ssb_push(rb_dsp_ssb *ssb,float i,float q,bool lsb,float *audio);
#endif
