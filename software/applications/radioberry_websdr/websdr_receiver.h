#ifndef RADIOBERRY_WEBSDR_RECEIVER_H
#define RADIOBERRY_WEBSDR_RECEIVER_H
#include "websdr_audio.h"
#include "radioberry/receiver.h"
#define RB_RX_AM RB_DSP_RX_AM
#define RB_RX_USB RB_DSP_RX_USB
#define RB_RX_LSB RB_DSP_RX_LSB
#define RB_AM_FRAME_BYTES (40u+RB_AUDIO_SAMPLES*2u)
#define RB_AM_QUEUE_BLOCKS 8u
#define RB_AM_MIME "application/x-rbpcm-am"
#define RB_USB_MIME "application/x-rbpcm-usb"
#define RB_LSB_MIME "application/x-rbpcm-lsb"
/* Only queue operations need a lock. RX parsing and DSP belong to core 1. */
typedef struct {
    uint8_t frames[RB_AM_QUEUE_BLOCKS][RB_AM_FRAME_BYTES];
    uint32_t generation, stream_id, sequence, dropped, produced;
    uint64_t samples;
    unsigned head,count;
    bool active,gap;
    uint8_t mode;
} rb_am_queue;
typedef struct {
    rb_dsp_receiver receiver;
    int16_t pcm[RB_AUDIO_SAMPLES];
    uint32_t i_word;
    unsigned used;
    bool have_i;
    uint8_t mode;
} rb_am_rx;
void rb_am_rx_init(rb_am_rx *rx);
void rb_am_rx_reset(rb_am_rx *rx);
/* -1 malformed input, 0 incomplete, 1 completed rx->pcm block. */
int rb_am_rx_word(rb_am_rx *rx,uint32_t word);
typedef void (*rb_am_block_fn)(void *context,const int16_t *pcm);
typedef void (*rb_am_gap_fn)(void *context);
/* Keep searching for a new I word after a bad/orphan tag. DMA may start
 * halfway through a pair; never discard every Q-first block indefinitely. */
void rb_am_rx_feed(rb_am_rx *rx,const uint32_t *words,size_t count,
                   rb_am_block_fn block,rb_am_gap_fn gap,void *context);
void rb_am_queue_init(rb_am_queue *queue);
bool rb_am_queue_open(rb_am_queue *queue,uint32_t id);
bool rb_am_queue_open_mode(rb_am_queue *queue,uint32_t id,uint8_t mode);
void rb_am_queue_close(rb_am_queue *queue);
void rb_am_queue_gap(rb_am_queue *queue);
void rb_am_queue_publish(rb_am_queue *queue,const int16_t *pcm,uint32_t frequency,uint32_t config);
bool rb_am_queue_read(rb_am_queue *queue,uint8_t *frame);
bool rb_web_am_open(uint32_t stream_id);
bool rb_web_rx_open(uint32_t stream_id,uint8_t mode);
void rb_web_am_close(void);
bool rb_web_am_read(uint8_t *frame);
#endif
