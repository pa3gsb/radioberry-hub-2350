#ifndef RADIOBERRY_DSP_TONE_H
#define RADIOBERRY_DSP_TONE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Network-independent PCM source. Phase persists across arbitrary blocks. */
typedef struct { float phase, step, amplitude; } rb_dsp_tone;
bool rb_dsp_tone_init(rb_dsp_tone *tone, uint32_t rate, float frequency, float amplitude);
void rb_dsp_tone_pcm16(rb_dsp_tone *tone, int16_t *output, size_t samples);
void rb_dsp_tone_skip(rb_dsp_tone *tone, uint64_t samples);
#endif
