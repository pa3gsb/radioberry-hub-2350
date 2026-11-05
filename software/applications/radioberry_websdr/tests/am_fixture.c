/* Known AM RF source; audio runs through production tagged-IQ RX and DSP. */
#include "websdr_receiver.h"
#include <math.h>
static rb_am_queue queue;
static rb_am_rx rx;
static uint64_t sample;
static uint32_t configuration;
static uint32_t fixture_frequency;
static void block(void *context,const int16_t *pcm){(void)context;rb_am_queue_publish(&queue,pcm,fixture_frequency,configuration);}
static void gap(void *context){(void)context;rb_am_queue_gap(&queue);}
void rb_test_am_fixture_init(void){rb_am_queue_init(&queue);rb_am_rx_init(&rx);sample=0;configuration=0;}
bool rb_test_am_fixture_open(uint32_t id){rx.mode=RB_RX_AM;rb_am_rx_reset(&rx);sample=0;configuration=0;return rb_am_queue_open(&queue,id);}
bool rb_test_rx_fixture_open(uint32_t id,uint8_t mode){rx.mode=mode;rb_am_rx_reset(&rx);sample=0;configuration=0;return rb_am_queue_open_mode(&queue,id,mode);}
bool rb_test_rx_fixture_agc(uint32_t mode,int32_t threshold){return rb_dsp_receiver_agc(&rx.receiver,mode,threshold);}
void rb_test_am_fixture_close(void){rb_am_queue_close(&queue);}
void rb_test_am_fixture_gap(void){rb_am_queue_gap(&queue);rb_am_rx_reset(&rx);}
bool rb_test_am_fixture_read(uint8_t *frame){return rb_am_queue_read(&queue,frame);}
static uint32_t word(uint64_t index,bool q)
{
    if(rx.mode!=RB_RX_AM) {
        const double a=6.283185307179586*(index%48000u)/48.,b=6.283185307179586*1900*(index%48000u)/48000.;
        const double value=q?-.03*(sin(a)-sin(b)):.03*(cos(a)+cos(b));
        return (q?0x01000000u:0u)|((uint32_t)(int32_t)lround(value*8388608.)&0xffffffu);
    }
    const double phase=6.283185307179586*(index%48000u)/40.;
    const double amplitude=.08*(1+.5*cos(6.283185307179586*(index%48000u)/48.));
    const double value=q?-amplitude*sin(phase):amplitude*cos(phase);
    return (q?0x01000000u:0u)|((uint32_t)(int32_t)lround(value*8388608.)&0xffffffu);
}
void rb_test_am_fixture_produce(uint32_t frequency,uint32_t config)
{
    if(configuration&&configuration!=config)rb_test_am_fixture_gap();
    configuration=config;fixture_frequency=frequency;
    uint32_t words[1024];
    for(unsigned n=0;n<512u;++n,++sample) {
        words[n*2u]=word(sample,true);
        words[n*2u+1u]=word(sample+1u,false);
    }
    rb_am_rx_feed(&rx,words,1024,block,gap,NULL);
}
