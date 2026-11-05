#ifndef RADIOBERRY_WEBSDR_AUDIO_H
#define RADIOBERRY_WEBSDR_AUDIO_H
#include "radioberry/tone.h"
#define RB_AUDIO_MIME "application/x-rbpcm"
#define RB_AUDIO_SAMPLES 240u
#define RB_AUDIO_FRAME_BYTES (32u + RB_AUDIO_SAMPLES * 2u)
typedef struct {
    rb_dsp_tone tone;
    uint32_t stream_id, sequence, next_ms;
    uint64_t first_sample;
} rb_web_audio;
void rb_web_audio_open(rb_web_audio *audio, uint32_t stream_id, uint32_t now);
bool rb_web_audio_read(rb_web_audio *audio, uint32_t now, uint8_t *frame);
#endif
