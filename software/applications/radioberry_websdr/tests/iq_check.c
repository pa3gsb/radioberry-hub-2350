#include "websdr_iq.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"IQ check failed: line %u\n",__LINE__); return __LINE__; } } while (0)
static uint32_t u32(const uint8_t *p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static rb_iq_queue queue;
static uint8_t frame[RB_IQ_FRAME_BYTES];
static uint32_t words[1024];
static void block(unsigned config) { rb_iq_feed(&queue,words,1024,7100000,config,1234,RB_IQ_INVERT_Q); }
int rb_test_iq(void)
{
    unsigned checks=0;
    for(unsigned n=0;n<512;++n) { words[n*2]=n; words[n*2+1]=0x01ffffffu-n; }
    words[0]=0x00800000u; words[1]=0x017fffffu;
    rb_iq_init(&queue); block(1);
    CHECK(queue.samples==512 && !queue.count && !rb_iq_read(&queue,frame));
    CHECK(!rb_iq_open(&queue,0)); CHECK(rb_iq_open(&queue,123)); CHECK(!rb_iq_open(&queue,456));
    block(1); CHECK(rb_iq_read(&queue,frame));
    CHECK(!memcmp(frame,"RBI1",4) && frame[4]==1 && frame[5]==4 && frame[6]==48);
    CHECK(u32(frame+8)==3120 && u32(frame+12)==123 && u32(frame+16)==1 && u32(frame+20)==1);
    CHECK(u32(frame+24)==512 && !u32(frame+28) && u32(frame+32)==1234 && u32(frame+36)==7100000);
    CHECK(u32(frame+40)==48000 && frame[44]==0 && frame[45]==2 && frame[46]==24 && !frame[47]);
    CHECK(frame[48]==0 && frame[49]==0 && frame[50]==0x80 && frame[51]==0xff && frame[52]==0xff && frame[53]==0x7f);
    CHECK(frame[54]==1 && frame[57]==0xfe && frame[58]==0xff && frame[59]==0xff);
    CHECK(!rb_iq_read(&queue,frame));
    /* Every single raw word may arrive on a separate DMA call. */
    for(unsigned n=0;n<1024;++n) rb_iq_feed(&queue,words+n,1,7100000,1,2000,4);
    CHECK(queue.count==1 && queue.used==0 && !queue.have_i);
    CHECK(rb_iq_read(&queue,frame) && u32(frame+24)==1024 && u32(frame+20)==2);
    for(unsigned n=0;n<18;++n) block(1);
    CHECK(queue.count==16 && queue.dropped==2 && queue.high_water==16);
    CHECK(rb_iq_read(&queue,frame) && (frame[5]&1) && u32(frame+20)==5);
    for(unsigned n=0;n<15;++n) CHECK(rb_iq_read(&queue,frame));
    CHECK(!rb_iq_read(&queue,frame));
    block(1); rb_iq_gap(&queue,6144); CHECK(!queue.count && queue.gap);
    const uint64_t before=queue.samples; block(2);
    CHECK(rb_iq_read(&queue,frame) && (frame[5]&1) && u32(frame+16)==2 && u32(frame+24)==(uint32_t)before);
    /* Drop a partial packet when the raw stream loses I/Q alignment. */
    rb_iq_feed(&queue,words,4,7100000,2,0,4);
    const uint32_t bad=0x02000000u; rb_iq_feed(&queue,&bad,1,7100000,2,0,4);
    CHECK(queue.invalid_words==1 && !queue.used && !queue.have_i && queue.gap);
    const uint32_t orphan_q=0x01000000u; rb_iq_feed(&queue,&orphan_q,1,7100000,2,0,4);
    CHECK(queue.invalid_words==2);
    block(2); CHECK(rb_iq_read(&queue,frame) && (frame[5]&1));
    rb_iq_feed(&queue,words,1,7100000,2,0,4); block(2);
    CHECK(queue.invalid_words==3 && rb_iq_read(&queue,frame) && (frame[5]&1));
    rb_iq_close(&queue); CHECK(!queue.active && !queue.count && !rb_iq_read(&queue,frame));
    CHECK(rb_iq_open(&queue,987));
    queue.samples=0xffffffffu-255u; queue.sequence=0xffffffffu;
    block(3); CHECK(rb_iq_read(&queue,frame) && u32(frame+20)==0 && u32(frame+24)==0xffffff00u && !u32(frame+28));
    block(3); CHECK(rb_iq_read(&queue,frame) && u32(frame+24)==256 && u32(frame+28)==1 && u32(frame+12)==987);
    printf("Production IQ packing/queue: %u checks passed.\n",checks);
    return 0;
}
