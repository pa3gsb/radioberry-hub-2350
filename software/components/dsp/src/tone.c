#include "radioberry/tone.h"
#include <math.h>

#define TWO_PI 6.2831853071795864769f
bool rb_dsp_tone_init(rb_dsp_tone *t, uint32_t rate, float frequency, float amplitude)
{
    if (!t || !rate || !isfinite(frequency) || frequency <= 0.f ||
        frequency >= rate * .5f || !isfinite(amplitude) || amplitude < 0.f || amplitude > 1.f)
        return false;
    t->phase = 0.f; t->step = TWO_PI * frequency / rate; t->amplitude = amplitude;
    return true;
}
void rb_dsp_tone_pcm16(rb_dsp_tone *t, int16_t *output, size_t count)
{
    for (size_t n = 0; n < count; ++n) {
        output[n] = (int16_t)lrintf(sinf(t->phase) * t->amplitude * 32767.f);
        t->phase += t->step;
        if (t->phase >= TWO_PI) t->phase -= TWO_PI;
    }
}
void rb_dsp_tone_skip(rb_dsp_tone *t, uint64_t samples)
{
    t->phase = (float)fmod((double)t->phase + (double)t->step * samples, (double)TWO_PI);
}
