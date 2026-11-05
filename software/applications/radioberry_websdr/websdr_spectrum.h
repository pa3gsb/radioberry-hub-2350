#ifndef RB_WEBSDR_SPECTRUM_H
#define RB_WEBSDR_SPECTRUM_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <radioberry/spectrum.h>
#define RB_SPECTRUM_BINS RB_DSP_SPECTRUM_BINS
/* ADC/DDC counts before windowing or FFT; independent of the display scale. */
typedef struct {
    int32_t i_min, i_max, q_min, q_max;
    uint32_t i_rms, q_rms, word_count, words[8];
} rb_spectrum_input;
void rb_spectrum_measure(const uint32_t *words, size_t count, rb_spectrum_input *input);
typedef struct {
    rb_dsp_spectrum dsp;
    float pending_i;
    bool have_i, invert_q;
    uint8_t flags;
    uint32_t last_publish, tag_errors;
} rb_spectrum;
typedef struct {
    uint8_t bins[RB_SPECTRUM_BINS], flags;
    uint16_t averages;
    uint32_t capture_ms;
} rb_spectrum_result;
void rb_spectrum_init(rb_spectrum *s, bool invert_q, uint32_t now);
/* Discard partial windows AND partial averages at a discontinuity. */
void rb_spectrum_reset(rb_spectrum *s, bool gap, uint32_t now);
/* PIO metadata 0 = I, 1 = Q; low 24 bits are signed samples.
 * Feed at most 1024 words per call; at most one result at nominal 10 Hz. */
bool rb_spectrum_feed(rb_spectrum *s, const uint32_t *words, size_t count,
                      uint32_t capture_ms, rb_spectrum_result *result);
#endif
