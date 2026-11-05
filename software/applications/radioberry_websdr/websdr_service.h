#ifndef RADIOBERRY_WEBSDR_SERVICE_H
#define RADIOBERRY_WEBSDR_SERVICE_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "websdr_spectrum.h"
#define RB_WEB_FRAME_BYTES 560
#define RB_WEB_REQUEST_BYTES 2048
typedef bool (*rb_web_tune_fn)(void *context, uint32_t frequency, uint32_t config_id);
typedef bool (*rb_web_gain_fn)(void *context, int32_t gain_db, uint32_t config_id);
typedef bool (*rb_web_agc_fn)(void *context,uint32_t mode,int32_t threshold_dbfs);
typedef struct {
    uint32_t rx_pairs, rx_buffers, rx_stalls, tag_errors, fft_windows;
    uint32_t dsp_us_max, frames, invalid_fft, gateware_major, gateware_minor;
} rb_web_diagnostics;
typedef struct {
    uint32_t frequency, config_id, stream_id, sequence, capture_ms;
    uint8_t frame[RB_WEB_FRAME_BYTES];
    char request_scratch[RB_WEB_REQUEST_BYTES + 1];
    bool synthetic, frame_ready;
    rb_web_tune_fn tune;
    void *tune_context;
    rb_web_diagnostics diagnostics;
    rb_spectrum_input input;
    rb_web_gain_fn gain;
    int32_t rx_gain_db;
    uint32_t agc_mode,agc_revision;
    int32_t agc_threshold_dbfs;
    rb_web_agc_fn agc;
} rb_web_service;
typedef struct {
    unsigned status;
    const char *type;
    const uint8_t *body;
    size_t length;
    uint8_t storage[RB_WEB_FRAME_BYTES];
} rb_web_response;
typedef struct { const char *path, *type; const uint8_t *data; size_t length; } rb_web_asset;
extern const rb_web_asset rb_web_assets[];
extern const size_t rb_web_asset_count;
void rb_web_init(rb_web_service *service, uint32_t stream_id, uint32_t now);
void rb_web_generate(rb_web_service *service, uint32_t now);
void rb_web_init_live(rb_web_service *service, uint32_t stream_id,
                      rb_web_tune_fn tune, void *context);
bool rb_web_publish(rb_web_service *service, const rb_spectrum_result *spectrum,
                     uint32_t frequency, uint32_t config_id, uint32_t sequence);
/* 0: fragmented/incomplete request; 1: complete response (including errors).
 * Exactly one request per connection. Input is not required to be NUL terminated. */
int rb_web_request(rb_web_service *service, const char *request, size_t length,
                   uint32_t now, rb_web_response *response);
#endif
