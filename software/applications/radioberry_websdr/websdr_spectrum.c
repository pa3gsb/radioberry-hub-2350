#include "websdr_spectrum.h"
#include <math.h>
#include <string.h>

void rb_spectrum_reset(rb_spectrum *s, bool gap, uint32_t now)
{
    rb_dsp_spectrum_reset(&s->dsp);
    s->have_i = false;
    s->flags = gap ? 2u : 0u;
    s->last_publish = now;
}

void rb_spectrum_init(rb_spectrum *s, bool invert_q, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->invert_q = invert_q;
    rb_dsp_spectrum_init(&s->dsp);
    rb_spectrum_reset(s, false, now);
}

static int32_t signed24(uint32_t word)
{
    int32_t value = (int32_t)(word & 0xffffffu);
    if (value & 0x800000) value -= 0x1000000;
    return value;
}

static float sample24(uint32_t word)
{
    return signed24(word) * (1.f / 8388608.f);
}

void rb_spectrum_measure(const uint32_t *words, size_t count, rb_spectrum_input *input)
{
    memset(input, 0, sizeof(*input));
    input->word_count = (uint32_t)count;
    float power[2] = {0};
    unsigned samples[2] = {0};
    for (size_t n = 0; n < count; ++n) {
        if (n < 8u) input->words[n] = words[n];
        const unsigned tag = words[n] >> 24;
        if (tag > 1u) continue;
        const int32_t v = signed24(words[n]);
        int32_t *lo = tag ? &input->q_min : &input->i_min;
        int32_t *hi = tag ? &input->q_max : &input->i_max;
        if (!samples[tag] || v < *lo) *lo = v;
        if (!samples[tag] || v > *hi) *hi = v;
        power[tag] += (float)v * (float)v;
        ++samples[tag];
    }
    if (samples[0]) input->i_rms = (uint32_t)(sqrtf(power[0] / samples[0]) + .5f);
    if (samples[1]) input->q_rms = (uint32_t)(sqrtf(power[1] / samples[1]) + .5f);
}

/* Display cadence, clipping flags and byte encoding belong to this app.
 * Windowing, FFT, power averaging and logarithmic levels live in rb_dsp. */
static bool publish(rb_spectrum *s, uint32_t now, rb_spectrum_result *r)
{
    if ((uint32_t)(now - s->last_publish) < 100u) return false;
    r->flags = s->flags; r->capture_ms = now; r->averages = s->dsp.averages;
    for (unsigned i = 0; i < RB_SPECTRUM_BINS; ++i) {
        const float db = rb_dsp_spectrum_level(&s->dsp,i);
        if (db < -127.5f) r->flags |= 4u;
        if (db > 0.f) r->flags |= 8u;
        int q = (int)floorf((db + 127.5f) * 2.f + .5f);
        r->bins[i] = q < 0 ? 0 : q > 255 ? 255 : q;
    }
    const uint32_t cadence = s->last_publish + 100u;
    rb_spectrum_reset(s, false, now);
    /* Keep the 100 ms cadence across 10.667 ms FFT blocks (9/10 averages).
     * After a long input silence, start again without a catch-up frame queue. */
    if ((uint32_t)(now - cadence) < 100u) s->last_publish = cadence;
    return true;
}

bool rb_spectrum_feed(rb_spectrum *s, const uint32_t *words, size_t count,
                      uint32_t now, rb_spectrum_result *r)
{
    bool ready = false;
    for (size_t n = 0; n < count; ++n) {
        const uint32_t word = words[n];
        const unsigned tag = word >> 24;
        if (tag > 1u || (tag == 0u && s->have_i) || (tag == 1u && !s->have_i)) {
            ++s->tag_errors; rb_spectrum_reset(s, true, now);
            if (tag != 0u) continue; /* A new I word is also a resync point. */
        }
        if (tag == 0u) { s->pending_i = sample24(word); s->have_i = true; }
        else {
            const float q=sample24(word)*(s->invert_q?-1.f:1.f);
            s->have_i = false;
            const int window=rb_dsp_spectrum_push(&s->dsp,s->pending_i,q);
            if(window<0)rb_spectrum_reset(s,true,now);
            else if(window>0)ready|=publish(s,now,r);
        }
    }
    return ready;
}
