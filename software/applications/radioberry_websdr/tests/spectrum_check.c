/* Test the production IQ/FFT pipeline, including signed PIO input and service.
 * No SDK/hardware mocks are used for the numerical code. */
#include "websdr_service.h"
#include "websdr_radio_config.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#define CHECK(x) do { if (!(x)) return __LINE__; ++checks; } while (0)
static rb_spectrum s;
static rb_spectrum_result result;
static uint32_t words[1024];
static struct {
    unsigned calls, fail_at;
    uint8_t commands[13];
    uint32_t data[13];
} controls;
static bool capture_control(void *context, uint8_t command, uint32_t data)
{
    if (context != &controls || controls.calls >= 13) return false;
    const unsigned i = controls.calls++;
    controls.commands[i] = command; controls.data[i] = data;
    return controls.calls != controls.fail_at;
}
static void tone(int bin, float amplitude, bool reversed_q)
{
    for (unsigned n = 0; n < 512; ++n) {
        const float angle = 6.283185307179586f * bin * n / 512.f;
        const int32_t i = (int32_t)lroundf(amplitude * cosf(angle) * 8388608.f);
        const int32_t q = (int32_t)lroundf(amplitude * sinf(angle) * 8388608.f) * (reversed_q ? -1 : 1);
        words[n * 2] = (uint32_t)i & 0xffffffu;
        words[n * 2 + 1] = 0x01000000u | ((uint32_t)q & 0xffffffu);
    }
}
static unsigned peak(void)
{
    unsigned best = 0;
    for (unsigned i = 1; i < 512; ++i) if (result.bins[i] > result.bins[best]) best = i;
    return best;
}
static unsigned calls;
static bool reject;
static uint32_t requested_frequency, requested_config;
static unsigned gain_calls;
static int32_t requested_gain;
static bool gain(void *context, int32_t gain_db, uint32_t config)
{
    (void)context; ++gain_calls; requested_gain = gain_db; requested_config = config;
    return !reject;
}
static bool tune(void *context, uint32_t frequency, uint32_t config)
{
    (void)context; ++calls; requested_frequency = frequency; requested_config = config;
    return !reject;
}
static int get(rb_web_service *service, const char *path, uint32_t now, rb_web_response *r)
{
    char request[128];
    const int length = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\n\r\n", path);
    return rb_web_request(service, request, length, now, r);
}
static int set(rb_web_service *service, unsigned frequency, rb_web_response *r)
{
    char request[256], body[64];
    const int length = snprintf(body, sizeof(body), "{\"frequency_hz\":%u}", frequency);
    const int n = snprintf(request, sizeof(request),
        "POST /api/tune HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", length, body);
    return rb_web_request(service, request, n, 1200, r);
}
static int set_gain(rb_web_service *service, int value, rb_web_response *r)
{
    char request[256], body[64];
    const int length = snprintf(body, sizeof(body), "{\"rx_gain_db\":%d}", value);
    const int n = snprintf(request, sizeof(request),
        "POST /api/gain HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: %d\r\n\r\n%s", length, body);
    return rb_web_request(service, request, n, 1200, r);
}
int rb_test_spectrum(void)
{
    unsigned checks = 0;
    rb_spectrum_input input;
    const uint32_t raw[] = {0x00fffffdu, 0x01000004u, 0x00000003u, 0x01fffffcu};
    rb_spectrum_measure(raw, 4, &input);
    CHECK(input.i_min == -3 && input.i_max == 3 && input.q_min == -4 && input.q_max == 4);
    CHECK(input.i_rms == 3 && input.q_rms == 4 && input.word_count == 4);
    CHECK(!memcmp(input.words, raw, sizeof(raw)) && input.words[4] == 0);
    const uint32_t limits[] = {0x00800000u, 0x017fffffu, 0x02000000u};
    rb_spectrum_measure(limits, 3, &input);
    CHECK(input.i_min == -8388608 && input.q_max == 8388607 && input.i_rms == 8388608);
    rb_spectrum_measure(NULL, 0, &input);
    CHECK(input.word_count == 0 && input.i_rms == 0 && input.q_rms == 0);
    memset(&controls, 0, sizeof(controls));
    CHECK(!rb_web_radio_configure(NULL, &controls, 7100000u));
    CHECK(rb_web_radio_configure(capture_control, &controls, 7100000u) && controls.calls == 13);
    CHECK(controls.commands[0] == 0x12 && controls.data[0] == 0x70000000u);
    CHECK(controls.commands[1] == 0x14 && controls.data[1] == 0x4017005au && RB_WEB_RX_GAIN_DB == 14);
    CHECK(controls.commands[8] == 0xfa && controls.data[8] == 0x07c10000u);
    CHECK(controls.commands[9] == 0 && controls.commands[10] == 0 &&
        controls.data[9] == 0x00880004u && controls.data[10] == controls.data[9]);
    CHECK(((controls.data[9] >> 24) & 3u) == 0 && ((controls.data[9] >> 3) & 15u) == 0);
    CHECK(controls.commands[11] == 2 && controls.commands[12] == 4 &&
        controls.data[11] == 7100000 && controls.data[12] == 7100000);
    for (unsigned fail = 1; fail <= 13; ++fail) {
        memset(&controls, 0, sizeof(controls)); controls.fail_at = fail;
        CHECK(!rb_web_radio_configure(capture_control, &controls, 7100000) && controls.calls == fail);
    }
    for (int db = -12; db <= 48; ++db) {
        memset(&controls, 0, sizeof(controls));
        CHECK(rb_web_radio_apply_gain(capture_control, &controls, db) && controls.calls == 1);
        CHECK(controls.commands[0] == 0x14 && controls.data[0] == (0x40170040u | (unsigned)(db + 12)));
    }
    memset(&controls, 0, sizeof(controls));
    CHECK(!rb_web_radio_apply_gain(capture_control, &controls, -13) &&
        !rb_web_radio_apply_gain(capture_control, &controls, 49) && controls.calls == 0);
    CHECK(!rb_web_radio_apply_gain(NULL, &controls, 14));
    controls.fail_at = 1;
    CHECK(!rb_web_radio_apply_gain(capture_control, &controls, 14) && controls.calls == 1);
    const int bins[] = {-256, -96, -32, 0, 32, 96, 255};
    for (unsigned n = 0; n < sizeof(bins) / sizeof(bins[0]); ++n) {
        rb_spectrum_init(&s, false, 0); tone(bins[n], .5f, false);
        CHECK(rb_spectrum_feed(&s, words, 1024, 110, &result));
        CHECK(peak() == (unsigned)(256 + bins[n]) && result.bins[peak()] == 243u);
        CHECK(result.averages == 1 && result.capture_ms == 110 && !(result.flags & 1));
    }
    rb_spectrum_init(&s, true, 0); tone(32, .5f, true);
    CHECK(rb_spectrum_feed(&s, words, 129, 10, &result) == false);
    CHECK(rb_spectrum_feed(&s, words + 129, 895, 110, &result));
    CHECK(peak() == 288 && s.dsp.pairs == 512 && s.tag_errors == 0);
    CHECK(fabsf(s.dsp.window[0]) < 1e-7f && fabsf(s.dsp.window[256] - 1.f) < 1e-7f);
    rb_spectrum_init(&s, false, 0); tone(32, .1f, false);
    CHECK(!rb_spectrum_feed(&s, words, 1024, 50, &result));
    tone(32, .01f, false);
    CHECK(rb_spectrum_feed(&s, words, 1024, 100, &result));
    CHECK(result.averages == 2 && result.bins[288] == 209); /* -22.967 dB, not -30 */
    rb_spectrum_init(&s, false, 0); tone(0, -.5f, false);
    CHECK(rb_spectrum_feed(&s, words, 1024, 110, &result) && result.bins[256] == 243);
    rb_spectrum_init(&s, false, 0); tone(0, -1.f, false);
    CHECK(rb_spectrum_feed(&s, words, 1024, 110, &result) && result.bins[256] == 255);
    rb_spectrum_init(&s, false, 0); memset(words, 0, sizeof(words));
    for (unsigned i = 1; i < 1024; i += 2) words[i] = 0x01000000;
    CHECK(rb_spectrum_feed(&s, words, 1024, 110, &result));
    CHECK(result.flags == 4 && result.bins[256] == 0);
    rb_spectrum_init(&s, false, 0); tone(32, .5f, false);
    CHECK(!rb_spectrum_feed(&s, words, 1024, 50, &result));
    const uint32_t bad[] = {0x02000000, 0x01000000, 0, 0};
    CHECK(!rb_spectrum_feed(&s, bad, 4, 60, &result) && s.tag_errors == 3 && s.dsp.averages == 0);
    rb_spectrum_reset(&s, true, 60);
    CHECK(rb_spectrum_feed(&s, words, 1024, 170, &result));
    CHECK(result.averages == 1 && (result.flags & 2) && peak() == 288);
    rb_spectrum_init(&s, false, 0); s.dsp.power[0] = NAN;
    CHECK(!rb_spectrum_feed(&s, words, 1024, 110, &result) && s.dsp.invalid_fft == 1);
    CHECK(rb_spectrum_feed(&s, words, 1024, 220, &result) && (result.flags & 2));
    rb_spectrum_init(&s, false, UINT32_MAX - 49);
    CHECK(rb_spectrum_feed(&s, words, 1024, 50, &result));
    rb_spectrum_init(&s, false, 0);
    unsigned frames = 0;
    for (unsigned n = 1; n <= 100; ++n)
        if (rb_spectrum_feed(&s, words, 1024, n * 512u / 48u, &result)) ++frames;
    CHECK(frames == 10 && result.averages >= 9 && result.averages <= 10);

    static rb_web_service service;
    rb_web_response response;
    rb_web_init_live(&service, 12345, tune, NULL);
    CHECK(get(&service, "/api/status", 1000, &response) && strstr((char *)response.body, "\"synthetic\":false"));
    CHECK(get(&service, "/api/spectrum", 1000, &response) && response.status == 204 && !response.length);
    CHECK(get(&service, "/api/iq", 1000, &response) && response.status == 204);
    result.capture_ms = 1000;
    CHECK(!rb_web_publish(&service, &result, 7100000, 2, 42));
    CHECK(rb_web_publish(&service, &result, 7100000, 1, 42));
    rb_spectrum_measure(raw, 4, &service.input);
    CHECK(get(&service, "/api/iq", 1100, &response) && response.status == 200 &&
        strstr((char *)response.body, "\"i_min\":-3") && strstr((char *)response.body, "00FFFFFD"));
    CHECK(get(&service, "/api/spectrum", 1100, &response) && response.status == 200 && response.length == 560);
    CHECK(response.body[38] == 0 && response.body[39] == 2 && response.body[46] == 1 && response.body[8] == 42);
    CHECK(get(&service, "/api/spectrum", 1501, &response) && response.status == 204);
    CHECK(get(&service, "/api/iq", 1501, &response) && response.status == 204);
    rb_web_generate(&service, 1600);
    CHECK(service.capture_ms == 1000 && !service.synthetic);
    calls = 0; reject = true;
    CHECK(set(&service, 7105000, &response) && response.status == 503 && service.config_id == 1 && service.frequency == 7100000);
    CHECK(calls == 1 && requested_frequency == 7105000 && requested_config == 2);
    reject = false;
    CHECK(set(&service, 7105000, &response) && response.status == 200 && service.config_id == 2 && !service.frame_ready);
    CHECK(get(&service, "/api/spectrum", 1200, &response) && response.status == 204);
    CHECK(get(&service, "/api/iq", 1200, &response) && response.status == 204);
    CHECK(!rb_web_publish(&service, &result, 7100000, 1, 43));
    CHECK(rb_web_publish(&service, &result, 7105000, 2, 44));
    CHECK(set(&service, 7105000, &response) && response.status == 200 && calls == 2 && service.config_id == 2);
    result.averages = 0;
    CHECK(!rb_web_publish(&service, &result, 7105000, 2, 45));
    memset(&service.diagnostics, 0xff, sizeof(service.diagnostics));
    CHECK(get(&service, "/api/diagnostics", 1200, &response) && response.length < sizeof(response.storage));
    memset(&service.input, 0xff, sizeof(service.input));
    CHECK(get(&service, "/api/iq", 1100, &response) && response.length < sizeof(response.storage));
    rb_web_init_live(&service, 12345, tune, NULL);
    CHECK(service.rx_gain_db == 14);
    CHECK(set_gain(&service, 48, &response) && response.status == 503 && service.config_id == 1);
    service.gain = gain; gain_calls = 0; reject = true;
    CHECK(set_gain(&service, 48, &response) && response.status == 503 && service.rx_gain_db == 14);
    CHECK(gain_calls == 1 && requested_gain == 48 && requested_config == 2);
    reject = false;
    result.averages = 1; result.capture_ms = 1000;
    CHECK(rb_web_publish(&service, &result, 7100000, 1, 55));
    CHECK(set_gain(&service, 48, &response) && response.status == 200 && service.rx_gain_db == 48 &&
        service.config_id == 2 && service.frequency == 7100000 && !service.frame_ready);
    CHECK(strstr((char *)response.body, "\"rx_gain_db\":48"));
    CHECK(set_gain(&service, 48, &response) && gain_calls == 2 && service.config_id == 2);
    CHECK(!rb_web_publish(&service, &result, 7100000, 1, 56));
    CHECK(get(&service, "/api/spectrum", 1200, &response) && response.status == 204);
    CHECK(rb_web_publish(&service, &result, 7100000, 2, 57));
    CHECK(set_gain(&service, -12, &response) && service.rx_gain_db == -12 && service.config_id == 3);
    CHECK(set_gain(&service, 0, &response) && service.rx_gain_db == 0 && service.config_id == 4);
    CHECK(set_gain(&service, -13, &response) && response.status == 400 && gain_calls == 4);
    CHECK(set_gain(&service, 49, &response) && response.status == 400 && gain_calls == 4);
    CHECK(get(&service, "/api/diagnostics", 1200, &response) &&
        strstr((char *)response.body, "\"rx_gain_db\":0"));
    printf("Production spectrum and live service: %u checks passed.\n", checks);
    return 0;
}

/* Browser-only fixture: generated tones pass through the production pipeline.
 * This is never linked into the board firmware. */
void rb_test_live_generate(rb_web_service *service, uint32_t now)
{
    rb_spectrum_init(&s, true, now - 100u);
    tone(32, .01f, true);
    if (rb_spectrum_feed(&s, words, 1024, now, &result))
        rb_web_publish(service, &result, service->frequency, service->config_id, service->sequence + 1u);
}
