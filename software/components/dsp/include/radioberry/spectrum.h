#ifndef RADIOBERRY_DSP_SPECTRUM_H
#define RADIOBERRY_DSP_SPECTRUM_H
#include <stdint.h>
#include "radioberry/dsp.h"

#define RB_DSP_SPECTRUM_BINS 512u

/* Non-overlapping Hann FFT windows, with power averaging. Input is normalized
 * complex IQ, already in the desired Q orientation. No framing or timestamps;
 * the caller chooses when to read levels and reset the averaging interval. */
typedef struct {
    rb_iq_f32_t iq[RB_DSP_SPECTRUM_BINS];
    float window[RB_DSP_SPECTRUM_BINS],power[RB_DSP_SPECTRUM_BINS];
    unsigned used,averages;
    uint32_t pairs,windows,invalid_fft;
} rb_dsp_spectrum;

void rb_dsp_spectrum_init(rb_dsp_spectrum *spectrum);
/* Discard partial windows and averages, retaining coefficients and counters. */
void rb_dsp_spectrum_reset(rb_dsp_spectrum *spectrum);
/* 1: a window was accumulated; 0: partial window; -1: invalid FFT, reset. */
int rb_dsp_spectrum_push(rb_dsp_spectrum *spectrum,float i,float q);
/* Averaged relative power in dB, FFT-shifted low -> high. -200 if no power.
 * The caller maps bin frequencies using its own IQ sample rate. */
float rb_dsp_spectrum_level(const rb_dsp_spectrum *spectrum,unsigned bin);
#endif
