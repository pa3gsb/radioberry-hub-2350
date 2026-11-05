#ifndef RADIOBERRY_DSP_RECEIVER_H
#define RADIOBERRY_DSP_RECEIVER_H
#include "radioberry/am.h"
#include "radioberry/ssb.h"

typedef enum {
    RB_DSP_RX_AM = 0,
    RB_DSP_RX_USB = 1,
    RB_DSP_RX_LSB = 2
} rb_dsp_rx_mode;

/* Shared 48 kHz normalized IQ -> 12 kHz mono PCM16 receiver.
 * The caller owns sample continuity, mode selection and output buffers.
 * No gateware word format, clock, queue, allocation or network dependency. */
typedef struct {
    rb_dsp_am am;
    rb_dsp_ssb ssb;
} rb_dsp_receiver;

void rb_dsp_receiver_init(rb_dsp_receiver *receiver);
/* Reset on missing samples or a mode change. AGC settings are preserved. */
void rb_dsp_receiver_reset(rb_dsp_receiver *receiver);
bool rb_dsp_receiver_agc(rb_dsp_receiver *receiver,uint32_t mode,int32_t threshold_dbfs);
/* True every fourth valid input pair. Writes one PCM16 sample only if true. */
bool rb_dsp_receiver_pcm16(rb_dsp_receiver *receiver,rb_dsp_rx_mode mode,
                           float i,float q,int16_t *sample);
#endif
