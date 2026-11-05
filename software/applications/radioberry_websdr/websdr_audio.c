#include "websdr_audio.h"
#include <string.h>
static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }
void rb_web_audio_open(rb_web_audio *a, uint32_t id, uint32_t now)
{
    memset(a, 0, sizeof(*a)); a->stream_id = id; a->next_ms = now + 20u;
    rb_dsp_tone_init(&a->tone, 12000u, 600.f, .2f);
}
bool rb_web_audio_read(rb_web_audio *a, uint32_t now, uint8_t *p)
{
    if ((int32_t)(now - a->next_ms) < 0) return false;
    /* Never synthesize a backlog after a slow client. Sample/sequence counters
     * expose skipped time and the browser resynchronizes its bounded buffer. */
    const uint32_t skipped = (uint32_t)(now - a->next_ms) / 20u;
    if (skipped) {
        const uint64_t samples = (uint64_t)skipped * RB_AUDIO_SAMPLES;
        a->first_sample += samples; a->sequence += skipped;
        rb_dsp_tone_skip(&a->tone, samples);
    }
    memset(p, 0, 32); memcpy(p, "RBA1", 4); p[4] = 1; p[5] = 2u | (skipped ? 1u : 0u);
    le16(p + 6, 32); le32(p + 8, a->sequence); le32(p + 12, a->stream_id);
    le32(p + 16, (uint32_t)a->first_sample); le32(p + 20, (uint32_t)(a->first_sample >> 32));
    le32(p + 24, 12000); le16(p + 28, RB_AUDIO_SAMPLES); p[30] = 16; p[31] = 1;
    int16_t pcm[RB_AUDIO_SAMPLES]; rb_dsp_tone_pcm16(&a->tone, pcm, RB_AUDIO_SAMPLES);
    for (unsigned n = 0; n < RB_AUDIO_SAMPLES; ++n) le16(p + 32 + n * 2u, (uint16_t)pcm[n]);
    ++a->sequence; a->first_sample += RB_AUDIO_SAMPLES;
    a->next_ms += (skipped + 1u) * 20u;
    return true;
}
