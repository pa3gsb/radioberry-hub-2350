#include "websdr_service.h"
#include "websdr_radio_config.h"
#include "websdr_iq.h"
#include "websdr_audio.h"
#include "websdr_receiver.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void le16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void le32(uint8_t *p, uint32_t v) { le16(p, v); le16(p + 2, v >> 16); }

static void frame_header(rb_web_service *s, uint32_t now, uint8_t flags,
                          uint16_t fft_size, uint16_t averages, uint8_t window)
{
    uint8_t *p = s->frame;
    memset(p, 0, 48);
    memcpy(p, "RBS1", 4); p[4] = 1; p[5] = flags; le16(p + 6, 48);
    le32(p + 8, s->sequence); le32(p + 12, s->config_id);
    le32(p + 16, s->stream_id); le32(p + 20, now);
    le32(p + 24, s->frequency); le32(p + 28, 48000); le32(p + 32, 48000);
    le16(p + 36, 512); le16(p + 38, fft_size);
    le16(p + 40, (uint16_t)-12750); le16(p + 42, 50);
    le16(p + 44, averages); p[46] = window;
    s->capture_ms = now; s->frame_ready = true;
}

void rb_web_generate(rb_web_service *s, uint32_t now)
{
    uint8_t *p = s->frame;
    if (!s->synthetic) return;
    ++s->sequence;
    frame_header(s, now, 1, 0, 0, 0);
    /* Demonstration signals are fixed relative to the nearest demo band.
     * Tuning therefore moves them across the display, as a receiver would. */
    const float reference = s->frequency < 5000000 ? 3650000.f :
                            s->frequency < 10000000 ? 7100000.f : 14200000.f;
    const float t = (now % 3600000u) * .001f;
    const float moving = 4200.f + 1300.f * sinf(t / 6.f);
    for (unsigned i = 0; i < 512; ++i) {
        const float hz = (float)((int32_t)s->frequency - (int32_t)reference) +
                         ((int)i - 256) * 93.75f;
        float db = -103.f + 3.f * sinf(hz * .017f + t * 3.1f) +
                           2.f * sinf(hz * .071f - t * 4.3f);
        /* Parabolic dB lobes: a cheap, explicit synthetic source, no FFT. */
        float x = (hz + 6800.f) / 100.f;
        db = fmaxf(db, -43.f - 2.1714724f * x * x);
        x = (hz - moving) / 100.f;
        db = fmaxf(db, -35.f + 2.f * sinf(t) - 2.1714724f * x * x);
        x = (hz - 12600.f) / 950.f;
        db = fmaxf(db, -61.f - 2.1714724f * x * x);
        x = (hz - 12600.f) / 75.f;
        db = fmaxf(db, -47.f - 2.1714724f * x * x);
        if (db < -127.5f) p[5] |= 4;
        if (db > 0.f) p[5] |= 8;
        int q = (int)floorf((db + 127.5f) * 2.f + .5f);
        p[48 + i] = q < 0 ? 0 : q > 255 ? 255 : q;
    }
    s->capture_ms = now;
}

void rb_web_init(rb_web_service *s, uint32_t stream_id, uint32_t now)
{
    memset(s, 0, sizeof(*s));
    s->frequency = 7100000; s->config_id = 1; s->stream_id = stream_id ? stream_id : 1;
    s->synthetic = true;
    s->rx_gain_db = RB_WEB_RX_GAIN_DB;
    s->agc_mode=RB_AGC_NORMAL;s->agc_threshold_dbfs=RB_AGC_DEFAULT_THRESHOLD_DBFS;s->agc_revision=1;
    rb_web_generate(s, now);
}

void rb_web_init_live(rb_web_service *s, uint32_t stream_id, rb_web_tune_fn tune, void *context)
{
    memset(s, 0, sizeof(*s));
    s->frequency = 7100000; s->config_id = 1; s->stream_id = stream_id ? stream_id : 1;
    s->tune = tune; s->tune_context = context;
    s->rx_gain_db = RB_WEB_RX_GAIN_DB;
    s->agc_mode=RB_AGC_NORMAL;s->agc_threshold_dbfs=RB_AGC_DEFAULT_THRESHOLD_DBFS;s->agc_revision=1;
}

bool rb_web_publish(rb_web_service *s, const rb_spectrum_result *r,
                     uint32_t frequency, uint32_t config_id, uint32_t sequence)
{
    if (s->synthetic || frequency != s->frequency || config_id != s->config_id ||
        !r->averages || (r->flags & ~14u)) return false;
    s->sequence = sequence;
    frame_header(s, r->capture_ms, r->flags, 512, r->averages, 1);
    memcpy(s->frame + 48, r->bins, 512);
    return true;
}

static int error(rb_web_response *r, unsigned status)
{
    r->status = status; r->type = "application/json";
    r->body = r->storage;
    r->length = (size_t)snprintf((char *)r->storage, sizeof(r->storage),
                                "{\"error\":%u}", status);
    return 1;
}

static int status(rb_web_service *s, rb_web_response *r)
{
    r->status = 200; r->type = "application/json"; r->body = r->storage;
    r->length = (size_t)snprintf((char *)r->storage, sizeof(r->storage),
        "{\"frequency_hz\":%lu,\"stream_id\":%lu,\"config_id\":%lu,"
        "\"synthetic\":%s,\"span_hz\":48000,\"sample_rate_hz\":48000,"
        "\"bin_count\":512,\"frame_bytes\":560,\"interval_ms\":100,"
        "\"min_frequency_hz\":100000,\"max_frequency_hz\":30000000,"
        "\"rx_gain_db\":%ld,\"min_rx_gain_db\":-12,\"max_rx_gain_db\":48,"
        "\"iq_transport\":\"coalesced\",\"am_revision\":2,\"ssb_revision\":2,"
        "\"agc_mode\":%lu,\"agc_threshold_dbfs\":%ld,\"agc_revision\":%lu}",
        (unsigned long)s->frequency, (unsigned long)s->stream_id, (unsigned long)s->config_id,
        s->synthetic ? "true" : "false", (long)s->rx_gain_db,
        (unsigned long)s->agc_mode,(long)s->agc_threshold_dbfs,(unsigned long)s->agc_revision);
    return 1;
}

static int equal_case(const char *a, const char *b)
{
    while (*a && *b) if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++)) return 0;
    return *a == *b;
}
static void whitespace(const char **p) { while (**p && isspace((unsigned char)**p)) ++*p; }
static int integer_json(const char *p, const char *key, int32_t min, int32_t max, int32_t *value)
{
    whitespace(&p); if (*p++ != '{') return 0;
    whitespace(&p);
    const size_t key_length = strlen(key);
    if (*p++ != '"' || strncmp(p, key, key_length)) return 0;
    p += key_length; if (*p++ != '"') return 0;
    whitespace(&p); if (*p++ != ':') return 0;
    whitespace(&p);
    const bool negative = *p == '-';
    if (negative) ++p;
    if (*p < '0' || *p > '9' || (*p == '0' && p[1] >= '0' && p[1] <= '9')) return 0;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') {
        if (v > 30000000u / 10u) return 0;
        v = v * 10u + (unsigned)(*p++ - '0');
    }
    if (v > 30000000u) return 0;
    const int32_t signed_value = negative ? -(int32_t)v : (int32_t)v;
    whitespace(&p); if (*p++ != '}') return 0;
    whitespace(&p); if (*p || signed_value < min || signed_value > max) return 0;
    *value = signed_value; return 1;
}

static int agc_json(const char *p,uint32_t *mode,int32_t *threshold)
{
    unsigned seen=0;whitespace(&p);if(*p++!='{')return 0;
    for(unsigned field=0;field<2u;++field) {
        whitespace(&p);unsigned bit;
        if(!strncmp(p,"\"agc_mode\"",10)){bit=1;p+=10;}
        else if(!strncmp(p,"\"agc_threshold_dbfs\"",20)){bit=2;p+=20;}
        else return 0;
        if(seen&bit)return 0;
        seen|=bit;
        whitespace(&p);if(*p++!=':')return 0;whitespace(&p);
        const bool negative=*p=='-';if(negative)++p;
        if(*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9'))return 0;
        uint32_t value=0;
        while(*p>='0'&&*p<='9'){if(value>100u)return 0;value=value*10u+(unsigned)(*p++-'0');}
        if(value>100u)return 0;
        int32_t number=negative?-(int32_t)value:(int32_t)value;
        if(bit==1){if(number<0||number>3)return 0;*mode=(uint32_t)number;}
        else {if(number<RB_AGC_THRESHOLD_MIN_DBFS||number>RB_AGC_THRESHOLD_MAX_DBFS)return 0;*threshold=number;}
        whitespace(&p);if(*p++!=(field==0u?',':'}'))return 0;
    }
    whitespace(&p);return !*p&&seen==3u;
}

int rb_web_request(rb_web_service *s, const char *request, size_t length,
                   uint32_t now, rb_web_response *r)
{
    if (length > RB_WEB_REQUEST_BYTES) return error(r, 413);
    if (memchr(request, 0, length)) return error(r, 400);
    /* Handler is serialized on the firmware loop. Keep this off the Pico stack. */
    char *text = s->request_scratch;
    memcpy(text, request, length); text[length] = 0;
    char *end = strstr(text, "\r\n\r\n");
    if (!end) return length == RB_WEB_REQUEST_BYTES ? error(r, 431) : 0;
    const size_t header_length = (size_t)(end + 4 - text);
    char method[8], path[128], version[16], extra;
    char *line = strstr(text, "\r\n"); *line = 0;
    if (sscanf(text, "%7s %127s %15s %c", method, path, version, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) return error(r, 400);
    size_t body_length = 0;
    int has_length = 0, json = 0;
    for (char *h = line + 2; h < end; h = line + 2) {
        line = strstr(h, "\r\n");
        if (!line) return error(r, 400);
        *line = 0;
        char *colon = strchr(h, ':');
        if (!colon || colon == h) return error(r, 400);
        *colon = 0;
        char *v = colon + 1;
        while (*v == ' ' || *v == '\t') ++v;
        char *tail = v + strlen(v);
        while (tail > v && (tail[-1] == ' ' || tail[-1] == '\t')) *--tail = 0;
        if (equal_case(h, "Transfer-Encoding")) return error(r, 400);
        if (equal_case(h, "Content-Length")) {
            if (has_length++ || !*v) return error(r, 400);
            for (; *v; ++v) {
                if (*v < '0' || *v > '9') return error(r, 400);
                body_length = body_length * 10u + (unsigned)(*v - '0');
                if (body_length > 128) return error(r, 413);
            }
        } else if (equal_case(h, "Content-Type")) {
            json = equal_case(v, "application/json");
        }
    }
    if (length < header_length + body_length) return 0;
    if (length != header_length + body_length) return error(r, 400);
    if (!strcmp(method, "POST") && !strcmp(path, "/api/tune")) {
        int32_t parsed_frequency;
        if (!has_length) return error(r, 411);
        if (!json) return error(r, 415);
        if (!integer_json(text + header_length, "frequency_hz", 100000, 30000000, &parsed_frequency))
            return error(r, 400);
        const uint32_t frequency = (uint32_t)parsed_frequency;
        if (frequency != s->frequency) {
            if (!s->synthetic && (!s->tune ||
                !s->tune(s->tune_context, frequency, s->config_id + 1u))) return error(r, 503);
            s->frequency = frequency; ++s->config_id;
            s->frame_ready = false;
            rb_web_generate(s, now); /* New config and its first frame become visible together. */
        }
        return status(s, r);
    }
    if (!strcmp(method, "POST") && !strcmp(path, "/api/gain")) {
        int32_t gain_db;
        if (!has_length) return error(r, 411);
        if (!json) return error(r, 415);
        if (!integer_json(text + header_length, "rx_gain_db", RB_WEB_RX_GAIN_MIN_DB,
                RB_WEB_RX_GAIN_MAX_DB, &gain_db)) return error(r, 400);
        if (gain_db != s->rx_gain_db) {
            if (!s->synthetic && (!s->gain ||
                !s->gain(s->tune_context, gain_db, s->config_id + 1u))) return error(r, 503);
            s->rx_gain_db = gain_db; ++s->config_id;
            s->frame_ready = false;
            rb_web_generate(s, now);
        }
        return status(s, r);
    }
    if(!strcmp(method,"POST")&&!strcmp(path,"/api/agc")) {
        uint32_t mode;int32_t threshold;
        if(!has_length)return error(r,411);
        if(!json)return error(r,415);
        if(!agc_json(text+header_length,&mode,&threshold))return error(r,400);
        if(mode!=s->agc_mode||threshold!=s->agc_threshold_dbfs) {
            if(!s->synthetic&&(!s->agc||!s->agc(s->tune_context,mode,threshold)))return error(r,503);
            s->agc_mode=mode;s->agc_threshold_dbfs=threshold;++s->agc_revision;
        }
        return status(s,r);
    }
    if (strcmp(method, "GET")) return error(r, 405);
    if (body_length) return error(r, 400);
    if (!strcmp(path, "/api/status")) return status(s, r);
    if (!strcmp(path, "/api/audio-tone")) {
        if (strcmp(version, "HTTP/1.1")) return error(r, 400);
        r->status = 200; r->type = RB_AUDIO_MIME; r->body = NULL; r->length = 0;
        return 1;
    }
    if (!strcmp(path, "/api/audio-am") || !strcmp(path,"/api/audio-usb") || !strcmp(path,"/api/audio-lsb")) {
        if (strcmp(version, "HTTP/1.1")) return error(r, 400);
        if (s->synthetic) return error(r, 503);
        r->status=200; r->type=!strcmp(path,"/api/audio-usb")?RB_USB_MIME:
            !strcmp(path,"/api/audio-lsb")?RB_LSB_MIME:RB_AM_MIME; r->body=NULL; r->length=0;
        return 1;
    }
    if (!strcmp(path, "/api/iq-stream")) {
        if (strcmp(version, "HTTP/1.1")) return error(r, 400);
        if (s->synthetic) return error(r, 503);
        /* Marker consumed by the HTTP transport, which owns the stream socket. */
        r->status = 200; r->type = RB_IQ_MIME; r->body = NULL; r->length = 0;
        return 1;
    }
    if (!strcmp(path, "/api/spectrum")) {
        r->type = "application/octet-stream"; r->body = r->storage;
        if (!s->frame_ready || (!s->synthetic && (uint32_t)(now - s->capture_ms) > 500u)) {
            r->status = 204; r->length = 0; return 1;
        }
        r->status = 200;
        memcpy(r->storage, s->frame, RB_WEB_FRAME_BYTES);
        r->body = r->storage; r->length = RB_WEB_FRAME_BYTES; return 1;
    }
    if (!strcmp(path, "/api/diagnostics")) {
        const rb_web_diagnostics *d = &s->diagnostics;
        r->status = 200; r->type = "application/json"; r->body = r->storage;
        r->length = (size_t)snprintf((char *)r->storage, sizeof(r->storage),
            "{\"rx_pairs\":%lu,\"rx_buffers\":%lu,\"rx_stalls\":%lu,\"tag_errors\":%lu,"
            "\"fft_windows\":%lu,\"dsp_us_max\":%lu,\"frames\":%lu,\"invalid_fft\":%lu,"
            "\"gateware_major\":%lu,\"gateware_minor\":%lu,\"rx_gain_db\":%d,\"q_inverted\":true}",
            (unsigned long)d->rx_pairs, (unsigned long)d->rx_buffers, (unsigned long)d->rx_stalls,
            (unsigned long)d->tag_errors, (unsigned long)d->fft_windows, (unsigned long)d->dsp_us_max,
            (unsigned long)d->frames, (unsigned long)d->invalid_fft,
            (unsigned long)d->gateware_major, (unsigned long)d->gateware_minor, (int)s->rx_gain_db);
        return 1;
    }
    if (!strcmp(path, "/api/iq")) {
        const rb_spectrum_input *d = &s->input;
        r->type = "application/json"; r->body = r->storage;
        if (!s->frame_ready || !d->word_count || (uint32_t)(now - s->capture_ms) > 500u) {
            r->status = 204; r->length = 0; return 1;
        }
        r->status = 200;
        r->length = (size_t)snprintf((char *)r->storage, sizeof(r->storage),
            "{\"frequency_hz\":%lu,\"config_id\":%lu,\"capture_ms\":%lu,\"word_count\":%lu,"
            "\"i_min\":%ld,\"i_max\":%ld,\"q_min\":%ld,\"q_max\":%ld,"
            "\"i_rms_counts\":%lu,\"q_rms_counts\":%lu,\"full_scale_counts\":8388608,"
            "\"words_hex\":[\"%08lX\",\"%08lX\",\"%08lX\",\"%08lX\","
            "\"%08lX\",\"%08lX\",\"%08lX\",\"%08lX\"]}",
            (unsigned long)s->frequency, (unsigned long)s->config_id,
            (unsigned long)s->capture_ms, (unsigned long)d->word_count,
            (long)d->i_min, (long)d->i_max, (long)d->q_min, (long)d->q_max,
            (unsigned long)d->i_rms, (unsigned long)d->q_rms,
            (unsigned long)d->words[0], (unsigned long)d->words[1],
            (unsigned long)d->words[2], (unsigned long)d->words[3],
            (unsigned long)d->words[4], (unsigned long)d->words[5],
            (unsigned long)d->words[6], (unsigned long)d->words[7]);
        return 1;
    }
    if (!strcmp(path, "/favicon.ico")) {
        r->status = 204; r->type = "image/x-icon"; r->body = r->storage; r->length = 0; return 1;
    }
    /* Static pages may select a browser-only preview through ?demo=1. API
     * paths remain exact above, so query parameters do not change commands. */
    char *query = strchr(path, '?');
    if (query) *query = 0;
    for (size_t i = 0; i < rb_web_asset_count; ++i) {
        if (!strcmp(path, rb_web_assets[i].path)) {
            r->status = 200; r->type = rb_web_assets[i].type;
            r->body = rb_web_assets[i].data; r->length = rb_web_assets[i].length; return 1;
        }
    }
    return error(r, 404);
}
