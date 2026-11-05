#include "websdr_iq.h"
#include <string.h>
static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { le16(p, (uint16_t)v); le16(p + 2, (uint16_t)(v >> 16)); }
static void le24(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; }
void rb_iq_init(rb_iq_queue *q) { memset(q, 0, sizeof(*q)); }
bool rb_iq_open(rb_iq_queue *q, uint32_t stream_id)
{
    if (q->active || !stream_id) return false;
    q->stream_id = stream_id; q->active = true;
    q->head = q->count = q->used = 0; q->have_i = q->gap = false;
    return true;
}
void rb_iq_close(rb_iq_queue *q)
{
    q->active = false; q->head = q->count = q->used = 0; q->have_i = false;
}
void rb_iq_gap(rb_iq_queue *q, uint32_t skipped_pairs)
{
    q->samples += skipped_pairs;
    q->head = q->count = q->used = 0; q->have_i = false;
    q->gap = true;
}
static void publish(rb_iq_queue *q, uint32_t frequency, uint32_t config_id,
                     uint32_t capture_ms, uint8_t flags)
{
    uint8_t *p = q->assembling;
    memset(p, 0, RB_IQ_HEADER_BYTES);
    memcpy(p, "RBI1", 4); p[4] = 1; p[5] = flags | (q->gap ? RB_IQ_GAP : 0u);
    le16(p + 6, RB_IQ_HEADER_BYTES); le32(p + 8, RB_IQ_FRAME_BYTES);
    le32(p + 12, q->stream_id); le32(p + 16, config_id); le32(p + 20, ++q->sequence);
    le32(p + 24, (uint32_t)q->first_sample); le32(p + 28, (uint32_t)(q->first_sample >> 32));
    le32(p + 32, capture_ms); le32(p + 36, frequency); le32(p + 40, 48000u);
    le16(p + 44, RB_IQ_PAIRS); p[46] = 24u; /* byte 47: interleaved I,Q */
    if (q->count == RB_IQ_QUEUE_BLOCKS) {
        q->head = (q->head + 1u) % RB_IQ_QUEUE_BLOCKS; --q->count; ++q->dropped;
        q->frames[q->head][5] |= RB_IQ_GAP; /* Notify on the first surviving block. */
    }
    const unsigned tail = (q->head + q->count) % RB_IQ_QUEUE_BLOCKS;
    memcpy(q->frames[tail], p, RB_IQ_FRAME_BYTES);
    ++q->count; ++q->produced;
    if (q->count > q->high_water) q->high_water = q->count;
    q->used = 0; q->gap = false;
}
void rb_iq_feed(rb_iq_queue *q, const uint32_t *words, size_t count,
                uint32_t frequency, uint32_t config_id, uint32_t capture_ms, uint8_t flags)
{
    for (size_t n = 0; n < count; ++n) {
        const uint32_t word = words[n];
        const unsigned tag = word >> 24;
        if (tag > 1u || (tag == 0u && q->have_i) || (tag == 1u && !q->have_i)) {
            ++q->invalid_words; q->used = 0; q->have_i = false; q->gap = true;
            if (tag != 0u) continue;
        }
        if (!tag) { q->i_word = word; q->have_i = true; }
        else {
            if (q->active) {
                if (!q->used) q->first_sample = q->samples;
                uint8_t *p = q->assembling + RB_IQ_HEADER_BYTES + q->used * 6u;
                le24(p, q->i_word); le24(p + 3, word);
                ++q->used;
            }
            ++q->samples; q->have_i = false;
            if (q->active && q->used == RB_IQ_PAIRS)
                publish(q, frequency, config_id, capture_ms, flags);
        }
    }
}
bool rb_iq_read(rb_iq_queue *q, uint8_t frame[RB_IQ_FRAME_BYTES])
{
    if (!q->active || !q->count) return false;
    memcpy(frame, q->frames[q->head], RB_IQ_FRAME_BYTES);
    q->head = (q->head + 1u) % RB_IQ_QUEUE_BLOCKS; --q->count;
    return true;
}
