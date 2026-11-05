#include "websdr_audio.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
int rb_test_audio(void)
{
    rb_dsp_tone a, b; int16_t whole[480], split[480];
    CHECK(rb_dsp_tone_init(&a,12000,600,.2f)); CHECK(rb_dsp_tone_init(&b,12000,600,.2f));
    rb_dsp_tone_pcm16(&a,whole,480); rb_dsp_tone_pcm16(&b,split,137); rb_dsp_tone_pcm16(&b,split+137,343);
    CHECK(!memcmp(whole,split,sizeof(whole)));
    double power=0; unsigned crossings=0;
    for(unsigned n=0;n<480;++n) { power+=(double)whole[n]*whole[n]; CHECK(abs(whole[n])<=6554);
        if(n && whole[n-1]<0 && whole[n]>=0)++crossings; }
    CHECK(crossings==23 || crossings==24); CHECK(fabs(sqrt(power/480)/32768-.2/sqrt(2))<.0001);
    CHECK(!rb_dsp_tone_init(&a,0,600,.2f)); CHECK(!rb_dsp_tone_init(&a,12000,6000,.2f));
    CHECK(!rb_dsp_tone_init(&a,12000,NAN,.2f)); CHECK(!rb_dsp_tone_init(&a,12000,600,1.1f));
    rb_web_audio audio; uint8_t p[RB_AUDIO_FRAME_BYTES]; rb_web_audio_open(&audio,123,1000);
    CHECK(!rb_web_audio_read(&audio,1019,p)); CHECK(rb_web_audio_read(&audio,1020,p));
    CHECK(!memcmp(p,"RBA1",4) && p[5]==2 && u32(p+12)==123 && u32(p+24)==12000);
    CHECK(u32(p+8)==0 && u32(p+16)==0); CHECK(!rb_web_audio_read(&audio,1020,p));
    CHECK(rb_web_audio_read(&audio,1100,p)); CHECK(p[5]==3 && u32(p+8)==4 && u32(p+16)==960);
    CHECK(!rb_web_audio_read(&audio,1101,p)); CHECK(rb_web_audio_read(&audio,1120,p));
    CHECK(p[5]==2 && u32(p+8)==5 && u32(p+16)==1200);
    rb_web_audio_open(&audio,9,0xfffffff0u); CHECK(!rb_web_audio_read(&audio,3,p));
    CHECK(rb_web_audio_read(&audio,4,p) && u32(p+8)==0);
    puts("PCM source: phase continuity, 600 Hz frequency/amplitude, validation, pacing, gaps and millisecond wrap passed.");
    return 0;
}
