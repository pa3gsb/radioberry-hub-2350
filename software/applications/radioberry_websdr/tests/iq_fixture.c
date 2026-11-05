/* Synthetic RF source for actual production IQ packing in browser tests. */
#include "websdr_iq.h"
#include <math.h>
static rb_iq_queue queue;
static uint32_t last_config;
void rb_test_iq_fixture_init(void) { rb_iq_init(&queue);last_config=0; }
bool rb_test_iq_fixture_open(uint32_t stream_id) { last_config=0;return rb_iq_open(&queue,stream_id); }
void rb_test_iq_fixture_close(void) { rb_iq_close(&queue); }
void rb_test_iq_fixture_gap(uint32_t skip) { rb_iq_gap(&queue,skip); }
bool rb_test_iq_fixture_read(uint8_t *frame) { return rb_iq_read(&queue,frame); }
void rb_test_iq_fixture_produce(uint32_t frequency,uint32_t config,uint32_t now)
{
    if(last_config && last_config!=config) rb_iq_gap(&queue,6144);
    last_config=config;
    uint32_t words[1024];
    for(unsigned n=0;n<512;++n) {
        const double phase=6.283185307179586*(double)((queue.samples+n)%48000u)/48.0;
        const int32_t i=(int32_t)lround(.05*8388608*cos(phase));
        const int32_t q=(int32_t)lround(-.05*8388608*sin(phase));
        words[n*2]=(uint32_t)i&0xffffffu;words[n*2+1]=0x01000000u|((uint32_t)q&0xffffffu);
    }
    rb_iq_feed(&queue,words,1024,frequency,config,now,RB_IQ_SYNTHETIC|RB_IQ_INVERT_Q);
}
