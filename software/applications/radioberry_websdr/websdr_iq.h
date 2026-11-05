#ifndef RB_WEBSDR_IQ_H
#define RB_WEBSDR_IQ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RB_IQ_PAIRS 512u
#define RB_IQ_HEADER_BYTES 48u
#define RB_IQ_FRAME_BYTES (RB_IQ_HEADER_BYTES + RB_IQ_PAIRS * 6u)
#define RB_IQ_QUEUE_BLOCKS 16u
#define RB_IQ_GAP 1u
#define RB_IQ_SYNTHETIC 2u
#define RB_IQ_INVERT_Q 4u
#define RB_IQ_MIME "application/x-rbiq"

/* Caller owns synchronization. Input words keep the gateware tag and signed24.
 * Samples are counted when accepted; unknown physical losses use the GAP flag. */
typedef struct {
    uint8_t frames[RB_IQ_QUEUE_BLOCKS][RB_IQ_FRAME_BYTES];
    uint8_t assembling[RB_IQ_FRAME_BYTES];
    uint64_t samples, first_sample;
    uint32_t stream_id, sequence, dropped, invalid_words, produced, high_water;
    uint32_t i_word;
    unsigned head, count, used;
    bool active, have_i, gap;
} rb_iq_queue;
void rb_iq_init(rb_iq_queue *q);
bool rb_iq_open(rb_iq_queue *q, uint32_t stream_id);
void rb_iq_close(rb_iq_queue *q);
void rb_iq_gap(rb_iq_queue *q, uint32_t skipped_pairs);
void rb_iq_feed(rb_iq_queue *q, const uint32_t *words, size_t count,
                uint32_t frequency, uint32_t config_id, uint32_t capture_ms, uint8_t flags);
bool rb_iq_read(rb_iq_queue *q, uint8_t frame[RB_IQ_FRAME_BYTES]);

/* Firmware backend, implemented by the radio owner with a short Pico lock. */
bool rb_web_iq_open(uint32_t stream_id);
void rb_web_iq_close(void);
bool rb_web_iq_read(uint8_t frame[RB_IQ_FRAME_BYTES]);
#endif
