#include "websdr_receiver.h"
#include <string.h>
static void le16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void le32(uint8_t *p,uint32_t v){le16(p,v);le16(p+2,v>>16);}
void rb_am_rx_init(rb_am_rx *r){memset(r,0,sizeof(*r));rb_dsp_receiver_init(&r->receiver);}
void rb_am_rx_reset(rb_am_rx *r){rb_dsp_receiver_reset(&r->receiver);r->used=0;r->have_i=false;}
static float signed24(uint32_t word) {
    int32_t value=(int32_t)(word&0xffffffu);
    if(value&0x800000)value-=0x1000000;
    return value/8388608.f;
}
int rb_am_rx_word(rb_am_rx *r,uint32_t word)
{
    const unsigned tag=word>>24; /* Only RX0 I/Q are configured. */
    if(tag==0u&&!r->have_i){r->i_word=word;r->have_i=true;return 0;}
    if(tag!=1u||!r->have_i){
        rb_am_rx_reset(r);
        if(tag==0u){r->i_word=word;r->have_i=true;}
        return -1;
    }
    r->have_i=false;
    /* Q inversion belongs here once, matching the existing spectrum path. */
    const float i=signed24(r->i_word),q=-signed24(word);
    if(!rb_dsp_receiver_pcm16(&r->receiver,(rb_dsp_rx_mode)r->mode,i,q,&r->pcm[r->used]))return 0;
    ++r->used;
    if(r->used<RB_AUDIO_SAMPLES)return 0;
    r->used=0;return 1;
}
void rb_am_rx_feed(rb_am_rx *r,const uint32_t *words,size_t count,
                   rb_am_block_fn block,rb_am_gap_fn gap,void *context)
{
    for(size_t n=0;n<count;++n) {
        const int ready=rb_am_rx_word(r,words[n]);
        if(ready<0)gap(context);
        else if(ready)block(context,r->pcm);
    }
}
void rb_am_queue_init(rb_am_queue *q){memset(q,0,sizeof(*q));}
bool rb_am_queue_open(rb_am_queue *q,uint32_t id) {
    return rb_am_queue_open_mode(q,id,RB_RX_AM);
}
bool rb_am_queue_open_mode(rb_am_queue *q,uint32_t id,uint8_t mode) {
    if(q->active||mode>RB_RX_LSB)return false;
    q->mode=mode;
    ++q->generation;q->active=true;q->gap=false;q->stream_id=id;
    q->head=q->count=0;q->samples=0;q->sequence=q->dropped=q->produced=0;return true;
}
void rb_am_queue_close(rb_am_queue *q){q->active=false;q->count=0;++q->generation;}
void rb_am_queue_gap(rb_am_queue *q) {
    q->head=q->count=0;++q->generation;
    if(q->active){q->gap=true;++q->sequence;q->samples+=RB_AUDIO_SAMPLES;}
}
void rb_am_queue_publish(rb_am_queue *q,const int16_t *pcm,uint32_t frequency,uint32_t config)
{
    if(!q->active)return;
    if(q->count==RB_AM_QUEUE_BLOCKS) {
        q->head=(q->head+1u)%RB_AM_QUEUE_BLOCKS;--q->count;++q->dropped;
        q->frames[q->head][5]|=1u;
    }
    uint8_t *p=q->frames[(q->head+q->count)%RB_AM_QUEUE_BLOCKS];
    memset(p,0,40);memcpy(p,"RBA2",4);p[4]=2;p[5]=(q->gap?1u:0u)|(q->mode==RB_RX_USB?4u:q->mode==RB_RX_LSB?8u:0u);le16(p+6,40);
    le32(p+8,q->sequence++);le32(p+12,q->stream_id);
    le32(p+16,(uint32_t)q->samples);le32(p+20,(uint32_t)(q->samples>>32));
    le32(p+24,12000);le16(p+28,RB_AUDIO_SAMPLES);p[30]=16;p[31]=1;
    le32(p+32,frequency);le32(p+36,config);
    for(unsigned n=0;n<RB_AUDIO_SAMPLES;++n)le16(p+40+n*2u,(uint16_t)pcm[n]);
    q->samples+=RB_AUDIO_SAMPLES;++q->count;++q->produced;q->gap=false;
}
bool rb_am_queue_read(rb_am_queue *q,uint8_t *frame) {
    if(!q->active||!q->count)return false;
    memcpy(frame,q->frames[q->head],RB_AM_FRAME_BYTES);
    q->head=(q->head+1u)%RB_AM_QUEUE_BLOCKS;--q->count;return true;
}
