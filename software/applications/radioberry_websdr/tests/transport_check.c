#include "websdr_http.h"
#include "websdr_iq.h"
#include "websdr_audio.h"
#include "websdr_receiver.h"
#include "socket.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct {
    uint8_t state, ir, pending;
    const char *input;
    size_t remaining, output_bytes;
    uint32_t ack_at;
    unsigned closes, fins;
    unsigned large_sends;
    char output[400000];
} model[8];
static uint32_t model_clock;
static bool delayed_ack;
static unsigned small_ack_ms = 40u;
static rb_iq_queue iq_queue;
static rb_am_queue am_queue;
bool rb_web_am_open(uint32_t id){return rb_am_queue_open(&am_queue,id);}
bool rb_web_rx_open(uint32_t id,uint8_t mode){return rb_am_queue_open_mode(&am_queue,id,mode);}
void rb_web_am_close(void){rb_am_queue_close(&am_queue);}
bool rb_web_am_read(uint8_t *frame){return rb_am_queue_read(&am_queue,frame);}
static rb_iq_queue expected_queue;
static uint8_t expected_frames[93][RB_IQ_FRAME_BYTES];
bool rb_web_iq_open(uint32_t id) { return rb_iq_open(&iq_queue,id); }
void rb_web_iq_close(void) { rb_iq_close(&iq_queue); }
bool rb_web_iq_read(uint8_t frame[RB_IQ_FRAME_BYTES]) { return rb_iq_read(&iq_queue,frame); }
uint8_t getSn_SR(uint8_t sn) { return model[sn].state; }
uint8_t getSn_IR(uint8_t sn) { return model[sn].ir; }
uint16_t getSn_RX_RSR(uint8_t sn) { return (uint16_t)model[sn].remaining; }
uint16_t getSn_MSSR(uint8_t sn) { (void)sn; return 1460u; }
int8_t socket(uint8_t sn, uint8_t mode, uint16_t port, uint8_t flags) {
    assert(sn != 1 && mode == Sn_MR_TCP && port == 80 && (flags & SF_IO_NONBLOCK));
    model[sn].state = SOCK_INIT; model[sn].pending = model[sn].ir = 0;
    return sn;
}
int8_t listen(uint8_t sn) { model[sn].state = SOCK_LISTEN; return 1; }
int8_t close(uint8_t sn) { ++model[sn].closes; model[sn].state = SOCK_CLOSED; return 1; }
int8_t disconnect(uint8_t sn) {
    assert(model[sn].ir & Sn_IR_SENDOK); ++model[sn].fins;
    model[sn].state = SOCK_CLOSED; return 0;
}
int32_t recv(uint8_t sn, uint8_t *data, uint16_t size) {
    assert(size <= model[sn].remaining && size <= 1024);
    memcpy(data, model[sn].input, size); model[sn].input += size;
    model[sn].remaining -= size; return size;
}
int32_t send(uint8_t sn, uint8_t *data, uint16_t size) {
    assert(size && size <= 2920);
    if (model[sn].pending && !(model[sn].ir & Sn_IR_SENDOK)) return 0;
    model[sn].ir &= ~Sn_IR_SENDOK;
    assert(model[sn].output_bytes + size < sizeof(model[sn].output));
    memcpy(model[sn].output + model[sn].output_bytes, data, size);
    model[sn].output_bytes += size; model[sn].pending = 1;
    model[sn].ack_at=model_clock+(size<1460u?small_ack_ms:1u);
    if(size==2920u)++model[sn].large_sends;
    return size;
}
static void connect_client(uint8_t sn, const char *request) {
    model[sn].state = SOCK_ESTABLISHED; model[sn].input = request;
    model[sn].remaining = strlen(request); model[sn].output_bytes = 0;
}
static size_t response_length(unsigned sn) {
    model[sn].output[model[sn].output_bytes] = 0;
    char *body = strstr(model[sn].output, "\r\n\r\n");
    char *length = strstr(model[sn].output, "Content-Length: ");
    if (!body || !length) return 0;
    unsigned n; assert(sscanf(length, "Content-Length: %u", &n) == 1);
    return (size_t)(body + 4 - model[sn].output) + n;
}
int rb_test_transport(void) {
    rb_iq_init(&iq_queue);
    rb_web_service s;
    rb_web_init(&s, 123, 0);
    uint8_t original_frame[RB_WEB_FRAME_BYTES];
    memcpy(original_frame, s.frame, sizeof(original_frame));
    rb_web_http_poll(&s, 0);
    assert(model[0].state == SOCK_LISTEN && model[2].state == SOCK_LISTEN && model[3].state == SOCK_LISTEN);
    connect_client(0, "GET / HTTP/1.1\r\nHost: board\r\n\r\n");
    connect_client(2, "GET /api/spectrum HTTP/1.1\r\n\r\n");
    connect_client(3, "");
    for (unsigned i = 1; i < 500; ++i) {
        rb_web_http_poll(&s, i);
        if (i == 1) rb_web_generate(&s, 9000); /* Change live data during response transmission. */
        if (response_length(2) && model[2].output_bytes == response_length(2)) break;
        if (model[0].pending) model[0].ir |= Sn_IR_SENDOK;
        if (model[2].pending) model[2].ir |= Sn_IR_SENDOK;
    }
    assert(model[2].output_bytes == response_length(2) && !model[2].fins);
    char *spectrum_body = strstr(model[2].output, "\r\n\r\n") + 4;
    assert(memcmp(spectrum_body, original_frame, sizeof(original_frame)) == 0);
    for (unsigned i = 500; i < 520; ++i) rb_web_http_poll(&s, i);
    assert(!model[2].fins); /* Final send queued but not acknowledged. */
    model[2].ir |= Sn_IR_SENDOK;
    rb_web_http_poll(&s, 520);
    assert(model[2].fins == 1);
    for (unsigned i = 521; i < 1000 && !model[0].fins; ++i) {
        model[0].ir |= Sn_IR_SENDOK; rb_web_http_poll(&s, i);
    }
    assert(model[0].fins == 1 && model[0].output_bytes == response_length(0));
    assert(model[0].output_bytes > 40000);
    rb_web_http_poll(&s, 3100);
    assert(model[3].closes == 1 && model[1].closes == 0);
    rb_web_http_poll(&s, 3101);
    assert(model[3].state == SOCK_LISTEN);
    connect_client(3, "GET /api/status HTTP/1.1\r\n\r\n");
    rb_web_http_poll(&s, 3102);
    rb_web_http_poll(&s, 12000);
    assert(model[3].closes == 2);
    rb_web_http_poll(&s, 12001);
    connect_client(3, "GET /api/status HTTP/1.1\r\n");
    rb_web_http_poll(&s, 12002);
    model[3].state = SOCK_CLOSE_WAIT;
    rb_web_http_poll(&s, 12003);
    assert(model[3].closes == 3);
    memset(model,0,sizeof(model)); rb_web_http_poll(&s,13000);
    rb_web_init_live(&s,567,NULL,NULL);
    connect_client(0,"GET /api/iq-stream HTTP/1.1\r\n\r\n");
    rb_web_http_poll(&s,13001); assert(iq_queue.active);
    connect_client(2,"GET /api/iq-stream HTTP/1.1\r\n\r\n");
    connect_client(3,"GET /api/status HTTP/1.1\r\n\r\n");
    uint32_t words[1024]; for(unsigned n=0;n<512;++n) { words[n*2]=n;words[n*2+1]=0x01000000u+n; }
    rb_iq_feed(&iq_queue,words,1024,7100000,1,13001,4);
    for(unsigned n=13002;n<13030;++n) {
        for(unsigned sn=0;sn<4;++sn) if(model[sn].pending)model[sn].ir|=Sn_IR_SENDOK;
        rb_web_http_poll(&s,n);
    }
    model[0].output[model[0].output_bytes]=0; model[2].output[model[2].output_bytes]=0;
    assert(strstr(model[0].output,"Transfer-Encoding: chunked") && !strstr(model[0].output,"Content-Length:"));
    assert(strstr(model[2].output,"HTTP/1.1 409") && model[2].fins==1);
    assert(model[3].fins==1 && model[3].output_bytes==response_length(3));
    const char *body=strstr(model[0].output,"\r\n\r\n")+4;
    assert(!memcmp(body,"C30\r\nRBI1",9));
    assert(model[0].output_bytes==(size_t)(body-model[0].output)+3120+7 && !model[0].fins);
    /* A stream is not subject to the ordinary eight-second HTTP deadline. */
    for(unsigned n=13100;n<25100;n+=100) {
        rb_iq_feed(&iq_queue,words,1024,7100000,1,n,4);
        for(unsigned i=0;i<6;++i) {model[0].ir|=Sn_IR_SENDOK;rb_web_http_poll(&s,n+i);}
        /* Keep the capture model bounded independently of the real stream. */
        model[0].output_bytes=0;
    }
    assert(iq_queue.active && !model[0].closes);
    model[0].state=SOCK_CLOSE_WAIT;rb_web_http_poll(&s,25101);
    assert(!iq_queue.active && model[0].closes==1);
    rb_web_http_poll(&s,25102);connect_client(0,"GET /api/iq-stream HTTP/1.1\r\n\r\n");
    rb_web_http_poll(&s,25103);assert(iq_queue.active);
    rb_iq_feed(&iq_queue,words,1024,7100000,1,25104,4);
    rb_web_http_poll(&s,25104);rb_web_http_poll(&s,25105);
    /* No SENDOK: a partial packet expires and releases the single owner. */
    rb_web_http_poll(&s,25700);assert(!iq_queue.active && model[0].closes==2);
    /* Model a receiver that ACKs small segments at 40 ms, but a pair of full
     * segments immediately. Old 1024-byte sends cannot sustain 48 kHz here. */
    memset(model,0,sizeof(model));rb_iq_init(&iq_queue);rb_web_http_poll(&s,26000);
    connect_client(0,"GET /api/iq-stream HTTP/1.1\r\n\r\n");
    rb_iq_init(&expected_queue);assert(rb_iq_open(&expected_queue,s.stream_id));
    unsigned produced=0;delayed_ack=true;
    for(unsigned step=0;step<1080;++step) {
        model_clock=26001+step;
        const unsigned due=(step<1000u?step:1000u)*48u/512u;
        while(produced<due) {
            rb_iq_feed(&iq_queue,words,1024,7100000,1,model_clock,4);
            rb_iq_feed(&expected_queue,words,1024,7100000,1,model_clock,4);
            assert(rb_iq_read(&expected_queue,expected_frames[produced]));++produced;
        }
        if(delayed_ack && model[0].pending && model_clock>=model[0].ack_at)model[0].ir|=Sn_IR_SENDOK;
        rb_web_http_poll(&s,model_clock);
    }
    assert(produced==93u && iq_queue.dropped==0u && model[0].large_sends>90u);
    model[0].output[model[0].output_bytes]=0;
    body=strstr(model[0].output,"\r\n\r\n")+4;
    assert(model[0].output_bytes==(size_t)(body-model[0].output)+produced*3127u);
    for(unsigned n=0;n<produced;++n) {
        const char *p=body+n*3127u;
        assert(!memcmp(p,"C30\r\nRBI1",9));
        assert(!memcmp(p+5,expected_frames[n],RB_IQ_FRAME_BYTES));
        assert(!memcmp(p+3125,"\r\n",2));
    }
    model[0].state=SOCK_CLOSE_WAIT;rb_web_http_poll(&s,27100);delayed_ack=false;
    assert(!iq_queue.active);
    memset(model,0,sizeof(model)); rb_web_http_poll(&s,28000);
    connect_client(0,"GET /api/audio-tone HTTP/1.1\r\n\r\n"); small_ack_ms=200u;
    for(unsigned step=0;step<2000;++step) {
        model_clock=28001+step;
        for(unsigned sn=0;sn<4;++sn)
            if(model[sn].pending && model_clock>=model[sn].ack_at)model[sn].ir|=Sn_IR_SENDOK;
        if(step==500)connect_client(2,"GET /api/iq-stream HTTP/1.1\r\n\r\n");
        if(step==600)connect_client(3,"GET /api/spectrum HTTP/1.1\r\n\r\n");
        rb_web_http_poll(&s,model_clock);
    }
    model[0].output[model[0].output_bytes]=0;
    body=strstr(model[0].output,"\r\n\r\n")+4;
    size_t available=model[0].output_bytes-(size_t)(body-model[0].output);
    const unsigned frames=(unsigned)(available/(RB_AUDIO_FRAME_BYTES+7u));
    assert(frames>=80u && model[0].large_sends>10u && !model[0].closes);
    for(unsigned n=0;n<frames;++n) {
        const uint8_t *p=(const uint8_t *)body+n*(RB_AUDIO_FRAME_BYTES+7u);
        assert(!memcmp(p,"200\r\nRBA1",9) && p[10]==2u); /* No source gap. */
        uint32_t seq=p[13]|(uint32_t)p[14]<<8|(uint32_t)p[15]<<16|(uint32_t)p[16]<<24;
        assert(seq==n && !memcmp(p+517,"\r\n",2));
    }
    model[2].output[model[2].output_bytes]=0;assert(strstr(model[2].output,"HTTP/1.1 409"));
    assert(model[3].fins==1 && !model[1].closes);
    model[0].state=SOCK_CLOSE_WAIT;rb_web_http_poll(&s,30001);
    rb_web_http_poll(&s,30002);connect_client(0,"GET /api/audio-tone HTTP/1.1\r\n\r\n");
    for(unsigned n=30003;n<30100;++n){model[0].ir|=Sn_IR_SENDOK;rb_web_http_poll(&s,n);}
    rb_web_http_poll(&s,30650);assert(model[0].closes==2u);
    memset(model,0,sizeof(model));rb_web_http_poll(&s,31000);rb_am_queue_init(&am_queue);
    connect_client(0,"GET /api/audio-am HTTP/1.1\r\n\r\n");
    int16_t pcm[240]={0};unsigned generated=0;
    for(unsigned step=0;step<1600;++step) {
        model_clock=31001+step;
        for(unsigned sn=0;sn<4;++sn)
            if(model[sn].pending&&model_clock>=model[sn].ack_at)model[sn].ir|=Sn_IR_SENDOK;
        if(am_queue.active&&step%20u==0){rb_am_queue_publish(&am_queue,pcm,7100000,1);++generated;}
        if(step==500)connect_client(2,"GET /api/audio-tone HTTP/1.1\r\n\r\n");
        if(step==600)connect_client(3,"GET /api/status HTTP/1.1\r\n\r\n");
        rb_web_http_poll(&s,model_clock);
    }
    assert(generated>=60u&&am_queue.dropped==0u&&am_queue.active);
    model[0].output[model[0].output_bytes]=0;body=strstr(model[0].output,"\r\n\r\n")+4;
    available=model[0].output_bytes-(size_t)(body-model[0].output);
    unsigned am_frames=(unsigned)(available/(RB_AM_FRAME_BYTES+7u));assert(am_frames>=55u);
    for(unsigned n=0;n<am_frames;++n) {
        const uint8_t *p=(const uint8_t *)body+n*(RB_AM_FRAME_BYTES+7u);
        assert(!memcmp(p,"208\r\nRBA2",9)&&p[10]==0&&p[13]==n);
        assert(!memcmp(p+525,"\r\n",2));
    }
    model[2].output[model[2].output_bytes]=0;assert(strstr(model[2].output,"HTTP/1.1 409"));
    assert(model[3].fins==1);model[0].state=SOCK_CLOSE_WAIT;rb_web_http_poll(&s,32601);
    assert(!am_queue.active);
    for(uint8_t mode=RB_RX_USB;mode<=RB_RX_LSB;++mode) {
        uint32_t base=34000u+(uint32_t)mode*2000u;
        memset(model,0,sizeof(model));rb_web_http_poll(&s,base);rb_am_queue_init(&am_queue);
        connect_client(0,mode==RB_RX_USB?"GET /api/audio-usb HTTP/1.1\r\n\r\n":"GET /api/audio-lsb HTTP/1.1\r\n\r\n");
        for(unsigned step=0;step<1000;++step) {
            model_clock=base+1u+step;
            if(model[0].pending&&model_clock>=model[0].ack_at)model[0].ir|=Sn_IR_SENDOK;
            if(am_queue.active&&step%20u==0)rb_am_queue_publish(&am_queue,pcm,7100000,1);
            rb_web_http_poll(&s,model_clock);
        }
        assert(am_queue.active&&am_queue.mode==mode&&am_queue.dropped==0u);
        model[0].output[model[0].output_bytes]=0;body=strstr(model[0].output,"\r\n\r\n")+4;
        assert(strstr(model[0].output,mode==RB_RX_USB?RB_USB_MIME:RB_LSB_MIME));
        assert(!memcmp(body,"208\r\nRBA2",9)&&(uint8_t)body[10]==(mode==RB_RX_USB?4u:8u));
        model[0].state=SOCK_CLOSE_WAIT;rb_web_http_poll(&s,base+1001u);assert(!am_queue.active);
    }
    puts("SSB transport: USB/LSB endpoint, selected backend mode, PCM mode flags, bounded queue and close passed.");
    puts("AM transport: live PCM framing, header ACK startup, bounded queue, stream owner, concurrent controls and close passed.");
    puts("PCM transport: 200 ms small-packet ACK model, ordered audio, concurrent spectrum, owner conflict, disconnect and backpressure passed.");
    puts("W5500 model: concurrent HTTP/IQ, chunk framing, single owner, stream lifetime, backpressure deadlines, reconnect and DHCP isolation passed.");
    puts("W5500 delayed ACK model: full-rate IQ, coalesced segment pairs, exact byte order and bounded remainder flush passed.");
    return 0;
}
