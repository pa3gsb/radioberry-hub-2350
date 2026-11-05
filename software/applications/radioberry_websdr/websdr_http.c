#include "websdr_http.h"
#include "websdr_iq.h"
#include "websdr_audio.h"
#include "websdr_receiver.h"
#include "socket.h"
#include <stdio.h>
#include <string.h>

/* Socket 1 belongs to DHCP. Small independent HTTP connections also accommodate
 * browser preconnects without holding up the spectrum poll. No request queue. */
static const uint8_t sockets[] = {0, 2, 3};
#define IQ_TCP_BYTES 2920u /* Two full 1460-byte TCP segments, within 4 KiB TX. */
typedef struct {
    char request[RB_WEB_REQUEST_BYTES];
    char header[256];
    rb_web_response response;
    size_t received, header_length, offset;
    uint32_t started;
    unsigned phase; /* 0 receive, 1 header, 2 body, 3 final ACK, 4 FIN, 5 IQ */
    unsigned active;
    bool iq_stream;
    bool audio_stream;
    bool am_stream;
    uint8_t rx_mode;
    uint8_t chunk[RB_IQ_FRAME_BYTES + 16u];
    size_t chunk_length;
    uint8_t wire[IQ_TCP_BYTES];
    size_t wire_length, send_limit;
    uint32_t wire_started;
} http_client;
static http_client clients[3];
static int iq_owner = -1;
static rb_web_audio audio;
static int audio_owner = -1;
static int am_owner = -1;

static void release_stream(uint8_t sn)
{
    if (iq_owner == (int)sn) { rb_web_iq_close(); iq_owner = -1; }
    if (audio_owner == (int)sn) {
        if(am_owner==(int)sn){rb_web_am_close();am_owner=-1;}
        audio_owner = -1;
    }
}
static void close_client(uint8_t sn) { release_stream(sn); close(sn); }

static void poll_client(rb_web_service *s, http_client *c, uint8_t sn, uint32_t now)
{
    const uint8_t state = getSn_SR(sn);
    if (state == SOCK_CLOSED) {
        release_stream(sn);
        memset(c, 0, sizeof(*c));
        if (socket(sn, Sn_MR_TCP, 80, SF_IO_NONBLOCK | SF_TCP_NODELAY) >= 0) listen(sn);
        return;
    }
    if (state == SOCK_LISTEN || state == SOCK_INIT) return;
    if (!c->active) { c->active = 1; c->started = now; }
    const uint32_t deadline = c->phase >= 5 ? 1500u : c->phase ? 8000u : 3000u;
    if ((uint32_t)(now - c->started) > deadline ||
        (c->wire_length && (uint32_t)(now - c->wire_started) > 500u) ||
        (getSn_IR(sn) & Sn_IR_TIMEOUT)) { close_client(sn); return; }
    if (state != SOCK_ESTABLISHED && state != SOCK_CLOSE_WAIT) return;
    if ((c->iq_stream || c->audio_stream) && state == SOCK_CLOSE_WAIT) { close_client(sn); return; }
    if (c->phase == 0) {
        const uint16_t available = getSn_RX_RSR(sn);
        if (!available) { if (state == SOCK_CLOSE_WAIT) close_client(sn); return; }
        size_t room = sizeof(c->request) - c->received;
        uint16_t n = available < room ? available : (uint16_t)room;
        if (n > 1024) n = 1024;
        int32_t count = n ? recv(sn, (uint8_t *)c->request + c->received, n) : 0;
        if (count < 0) { close_client(sn); return; }
        c->received += (size_t)count;
        if (!rb_web_request(s, c->request, c->received, now, &c->response)) return;
        if (c->response.status == 200 && (!strcmp(c->response.type, RB_IQ_MIME) ||
                !strcmp(c->response.type, RB_AUDIO_MIME) || !strcmp(c->response.type,RB_AM_MIME) ||
                !strcmp(c->response.type,RB_USB_MIME) || !strcmp(c->response.type,RB_LSB_MIME))) {
            const bool am = !strcmp(c->response.type,RB_AM_MIME)||!strcmp(c->response.type,RB_USB_MIME)||!strcmp(c->response.type,RB_LSB_MIME);
            const bool tone = !strcmp(c->response.type, RB_AUDIO_MIME);
            const unsigned status = iq_owner >= 0 || audio_owner >= 0 ? 409u :
                !am && !tone && !rb_web_iq_open(s->stream_id) ? 503u : 200u;
            if (status == 200u) {
                if (tone || am) {
                    audio_owner = sn; c->audio_stream = true;
                    c->am_stream=am;
                    if(am) {
                        am_owner=sn;
                        c->rx_mode=!strcmp(c->response.type,RB_USB_MIME)?RB_RX_USB:
                            !strcmp(c->response.type,RB_LSB_MIME)?RB_RX_LSB:RB_RX_AM;
                    }
                    else rb_web_audio_open(&audio, s->stream_id, now);
                } else { iq_owner = sn; c->iq_stream = true; }
                const uint16_t mss = getSn_MSSR(sn);
                c->send_limit = mss && mss <= 1460u ? 2u * mss : IQ_TCP_BYTES;
            }
            else {
                c->response.status = status; c->response.type = "application/json";
                c->response.body = c->response.storage;
                c->response.length = (size_t)snprintf((char *)c->response.storage,
                    sizeof(c->response.storage), "{\"error\":%u}", status);
            }
        }
        const char *reason = c->response.status == 200 ? "OK" :
                             c->response.status == 204 ? "No Content" : "Error";
        if (c->iq_stream || c->audio_stream) c->header_length = (size_t)snprintf(c->header, sizeof(c->header),
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nTransfer-Encoding: chunked\r\n"
            "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n\r\n", c->response.type);
        else c->header_length = (size_t)snprintf(c->header, sizeof(c->header),
            "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
            "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
            "Connection: close\r\n\r\n", c->response.status, reason, c->response.type,
            (unsigned long)c->response.length);
        c->phase = 1; c->offset = 0; c->started = now;
    }
    if (c->phase == 1 || c->phase == 2) {
        const uint8_t *data = c->phase == 1 ? (const uint8_t *)c->header : c->response.body;
        size_t length = c->phase == 1 ? c->header_length : c->response.length;
        if (c->offset == length) {
            if (c->phase == 1 && (c->iq_stream || c->audio_stream)) {
                /* Start the test clock after the HTTP header is acknowledged.
                 * A delayed header ACK must not create a startup audio gap. */
                if (c->audio_stream) {
                    if (!(getSn_IR(sn) & Sn_IR_SENDOK)) return;
                    if(c->am_stream) {
                        if(!rb_web_rx_open(s->stream_id,c->rx_mode)){close_client(sn);return;}
                    } else rb_web_audio_open(&audio, s->stream_id, now);
                }
                c->phase = 5; c->offset = 0; c->started = now; return;
            }
            ++c->phase; c->offset = 0;
            if (c->phase == 2 && !c->response.length) c->phase = 3;
            return;
        }
        uint16_t n = length - c->offset > 1024 ? 1024 : (uint16_t)(length - c->offset);
        const int32_t sent = send(sn, (uint8_t *)data + c->offset, n);
        if (sent < 0) { close_client(sn); return; }
        c->offset += (size_t)sent;
        return;
    }
    if (c->phase == 5) {
        /* The vendor driver waits for SENDOK before every new send(). Four
         * small sends per IQ frame expose the stream to delayed TCP ACKs.
         * Coalesce HTTP bytes across application-frame boundaries, so ordinary
         * sends contain two full TCP segments. Flush an idle remainder at 20 ms.
         * Both buffers stay immutable while send() returns SOCK_BUSY. */
        unsigned fetched = 0;
        while (c->wire_length < c->send_limit) {
            if (c->offset == c->chunk_length) {
                if (fetched == 2u) break; /* Bound copies and lock acquisition. */
                const unsigned frame_bytes = c->am_stream ? RB_AM_FRAME_BYTES :
                    c->audio_stream ? RB_AUDIO_FRAME_BYTES : RB_IQ_FRAME_BYTES;
                const size_t prefix = (size_t)snprintf((char *)c->chunk, 16u, "%X\r\n", frame_bytes);
                if (c->am_stream ? !rb_web_am_read(c->chunk+prefix) : c->audio_stream ?
                        !rb_web_audio_read(&audio, now, c->chunk + prefix) : !rb_web_iq_read(c->chunk + prefix)) break;
                memcpy(c->chunk + prefix + frame_bytes, "\r\n", 2u);
                c->chunk_length = prefix + frame_bytes + 2u;
                c->offset = 0; ++fetched;
            }
            size_t n = c->chunk_length - c->offset;
            if (n > c->send_limit - c->wire_length) n = c->send_limit - c->wire_length;
            if (!c->wire_length) c->wire_started = now;
            memcpy(c->wire + c->wire_length, c->chunk + c->offset, n);
            c->offset += n; c->wire_length += n;
        }
        if (!c->wire_length || (c->wire_length < c->send_limit &&
                (uint32_t)(now - c->wire_started) < (c->audio_stream ? 120u : 20u))) return;
        const int32_t sent = send(sn, c->wire, (uint16_t)c->wire_length);
        if (sent < 0) { close_client(sn); return; }
        if (sent > 0) {
            c->wire_length -= (size_t)sent;
            if (c->wire_length) memmove(c->wire, c->wire + sent, c->wire_length);
            c->started = c->wire_started = now;
        }
        return;
    }
    /* ioLibrary send() reports bytes QUEUED. Closing before SENDOK can truncate
     * the final chunk. Intermediate SENDOK is consumed by the next send(). */
    if (c->phase == 3 && (getSn_IR(sn) & Sn_IR_SENDOK)) {
        disconnect(sn); c->phase = 4;
    }
}

void rb_web_http_poll(rb_web_service *service, uint32_t now)
{
    for (unsigned i = 0; i < sizeof(sockets); ++i)
        poll_client(service, &clients[i], sockets[i], now);
}
