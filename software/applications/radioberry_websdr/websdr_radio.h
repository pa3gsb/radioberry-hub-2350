#ifndef RB_WEBSDR_RADIO_H
#define RB_WEBSDR_RADIO_H
#include "websdr_service.h"
/* Core 1 owns SPI1, radio PIO/DMA IRQ and DSP. Core 0 owns Ethernet/service. */
void rb_web_radio_start(void);
bool rb_web_radio_tune(void *context, uint32_t frequency, uint32_t config_id);
bool rb_web_radio_gain(void *context, int32_t gain_db, uint32_t config_id);
bool rb_web_radio_agc(void *context,uint32_t mode,int32_t threshold_dbfs);
void rb_web_radio_poll(rb_web_service *service);
#endif
