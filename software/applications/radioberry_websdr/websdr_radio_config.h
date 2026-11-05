#ifndef RB_WEBSDR_RADIO_CONFIG_H
#define RB_WEBSDR_RADIO_CONFIG_H
#include <stdbool.h>
#include <stdint.h>

/* Board owner's working P1 initialization profile. Preserve complete words,
 * including controls outside the sample-rate/receiver fields. */
#define RB_WEB_RX_CONTROL_WORD 0x00880004u
#define RB_WEB_RX_GAIN_WORD 0x4017005au
#define RB_WEB_RX_GAIN_DB ((int)(RB_WEB_RX_GAIN_WORD & 0x3fu) - 12)
#define RB_WEB_RX_GAIN_MIN_DB (-12)
#define RB_WEB_RX_GAIN_MAX_DB 48
typedef bool (*rb_web_radio_send_fn)(void *context, uint8_t command, uint32_t data);
/* Send the initialization sequence, then set TX/RX1 NCO frequencies.
 * Stop on the first failed transfer; caller starts RX only after success. */
bool rb_web_radio_configure(rb_web_radio_send_fn send, void *context, uint32_t frequency);
/* Preserve C1-C3 and C4 bit 7 from the initialization profile. C4 bit 6
 * selects the AD9866 extended gain range; bits 5:0 encode dB + 12. */
bool rb_web_radio_apply_gain(rb_web_radio_send_fn send, void *context, int32_t gain_db);
#endif
