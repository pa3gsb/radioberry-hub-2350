#include "websdr_radio.h"
#include "websdr_radio_config.h"
#include "websdr_iq.h"
#include "websdr_receiver.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/critical_section.h"
#include <radioberry/radio.h>

typedef struct {
    uint32_t frequency, config_id;
    int32_t gain_db;
    bool gain_only;
    bool agc_only;
    uint32_t agc_mode;
    int32_t agc_threshold;
} radio_request;
static queue_t requests, replies;
static critical_section_t snapshot_lock;
static critical_section_t iq_lock;
static rb_iq_queue iq_queue;
static critical_section_t am_lock;
static rb_am_queue am_queue;
static rb_am_rx am_rx;

static void am_gap(void)
{
    critical_section_enter_blocking(&am_lock);
    rb_am_queue_gap(&am_queue);
    critical_section_exit(&am_lock);
}
typedef struct {uint32_t generation,frequency,config;} am_capture;
static void am_input_gap(void *context)
{
    am_capture *capture=context;
    critical_section_enter_blocking(&am_lock);
    /* A close/open on core 0 must not let this old producer reset the new
     * session. Only an input gap belonging to this generation can advance it. */
    if(am_queue.active&&am_queue.generation==capture->generation) {
        rb_am_queue_gap(&am_queue);capture->generation=am_queue.generation;
    }
    critical_section_exit(&am_lock);
}
static void am_input_block(void *context,const int16_t *pcm)
{
    const am_capture *capture=context;
    critical_section_enter_blocking(&am_lock);
    if(am_queue.active&&am_queue.generation==capture->generation)
        rb_am_queue_publish(&am_queue,pcm,capture->frequency,capture->config);
    critical_section_exit(&am_lock);
}
static void am_feed(const uint32_t *words,uint32_t frequency,uint32_t config)
{
    static uint32_t generation;
    critical_section_enter_blocking(&am_lock);
    const bool active=am_queue.active;
    const uint32_t current=am_queue.generation;
    const uint8_t mode=am_queue.mode;
    critical_section_exit(&am_lock);
    if(!active)return;
    if(generation!=current){am_rx.mode=mode;rb_am_rx_reset(&am_rx);generation=current;}
    am_capture capture={.generation=current,.frequency=frequency,.config=config};
    rb_am_rx_feed(&am_rx,words,STREAM_RX_DMA_WORDS,am_input_block,am_input_gap,&capture);
    generation=capture.generation;
}
static rb_radio_t radio;
static rb_spectrum spectrum;
static uint32_t copied_words[STREAM_RX_DMA_WORDS];
static struct {
    rb_spectrum_result result;
    rb_web_diagnostics diagnostics;
    rb_spectrum_input input;
    uint32_t frequency, config_id, sequence;
    bool ready;
} latest;

static bool control(uint8_t command, uint32_t data)
{
    uint8_t response[6];
    return rb_radio_write_control(&radio, command, data, response) &&
        response[4] == radio.info.major && response[5] == radio.info.minor;
}

static bool initialization_control(void *context, uint8_t command, uint32_t data)
{
    (void)context;
    const bool ok = control(command, data);
    printf("RB-Command = 05 Command = %02X command_data = %08lX%s\n",
        (unsigned)command, (unsigned long)data, ok ? "" : " FAILED");
    /* Allow the gateware/AD9866 to finish each startup command. This delay and
     * serial trace occur before RX starts, never in the live tuning path. */
    if (ok) sleep_ms(1u);
    return ok;
}

static bool gain_control(void *context, uint8_t command, uint32_t data)
{
    (void)context;
    return control(command, data);
}

static void radio_core(void)
{
    bool ok = rb_radio_init(&radio, pio0, 0u, 1u) &&
        rb_radio_detect(&radio, 50u, 3u, 100u);
    if (ok) {
        printf("RadioBerry: gateware %u.%u, FPGA %u, RX %u, TX %u\n",
            radio.info.major, radio.info.minor, radio.info.fpga,
            radio.info.receiver_count, radio.info.transmitter_count);
        /* P1 command encoding over the existing six-byte gateware SPI link.
         * MOX stays zero; TX DMA is not started. No P1 network stack is needed. */
        ok = rb_web_radio_configure(initialization_control, NULL, 7100000u);
    }
    uint32_t frequency = 7100000u, config_id = 1u, sequence = 0u;
    uint32_t settling = 6144u, last_stalls = 0u, dsp_us_max = 0u, next_diagnostics = 0u;
    rb_spectrum_init(&spectrum, true, to_ms_since_boot(get_absolute_time()));
    rb_am_rx_init(&am_rx);
    if (ok) ok = rb_radio_rx_start(&radio);
    queue_add_blocking(&replies, &ok);
    if (!ok) for (;;) tight_loop_contents();

    for (;;) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        radio_request command;
        if (queue_try_remove(&requests, &command)) {
            if(command.agc_only) {
                const bool applied=rb_dsp_receiver_agc(&am_rx.receiver,command.agc_mode,command.agc_threshold);
                queue_add_blocking(&replies,&applied);
                continue; /* Keep ADC, filters, partial PCM and stream clock running. */
            }
            rb_radio_rx_stop(&radio);
            am_gap();
            critical_section_enter_blocking(&iq_lock);
            rb_iq_gap(&iq_queue, 0u);
            critical_section_exit(&iq_lock);
            ok = command.gain_only ? rb_web_radio_apply_gain(gain_control,
                NULL, command.gain_db) : control(0x04u, command.frequency);
            rb_spectrum_reset(&spectrum, false, now);
            settling = 6144u; /* Flush FPGA FIFO/DDC settling before the new axis. */
            if (ok) {
                if (!command.gain_only) frequency = command.frequency;
                config_id = command.config_id;
                ok = rb_radio_rx_start(&radio);
            }
            critical_section_enter_blocking(&snapshot_lock);
            latest.ready = false;
            critical_section_exit(&snapshot_lock);
            queue_add_blocking(&replies, &ok);
        }
        stream_rx_dma_stats_t stats = stream_rx_dma_get_stats(&radio.rx_dma);
        if (stats.stalled_transfers != last_stalls) {
            /* Both buffers full: do not consume them in index order or join
             * samples across the unknown gap. Restart and discard stale input. */
            last_stalls = stats.stalled_transfers;
            rb_radio_rx_stop(&radio);
            am_gap();
            critical_section_enter_blocking(&iq_lock);
            rb_iq_gap(&iq_queue, 0u); /* Lost FPGA samples have unknown duration. */
            critical_section_exit(&iq_lock);
            rb_spectrum_reset(&spectrum, true, now);
            settling = 6144u;
            if (ok) ok = rb_radio_rx_start(&radio);
            critical_section_enter_blocking(&snapshot_lock);
            latest.ready = false;
            critical_section_exit(&snapshot_lock);
        }
        const uint32_t *words;
        uint8_t index;
        if (ok && rb_radio_rx_acquire(&radio, &words, &index)) {
            const uint32_t capture_ms = to_ms_since_boot(get_absolute_time());
            memcpy(copied_words, words, sizeof(copied_words));
            rb_radio_rx_release(&radio, index); /* Return DMA buffer BEFORE FFT/log. */
            if (stream_rx_dma_get_stats(&radio.rx_dma).stalled_transfers != last_stalls)
                continue; /* Stall raced with acquire: restart before consuming either buffer. */
            if (settling) {
                critical_section_enter_blocking(&iq_lock);
                rb_iq_gap(&iq_queue, STREAM_RX_DMA_IQ_PAIRS);
                critical_section_exit(&iq_lock);
                settling = settling > STREAM_RX_DMA_IQ_PAIRS ? settling - STREAM_RX_DMA_IQ_PAIRS : 0u;
                spectrum.last_publish = capture_ms;
            } else {
                critical_section_enter_blocking(&iq_lock);
                rb_iq_feed(&iq_queue, copied_words, STREAM_RX_DMA_WORDS,
                    frequency, config_id, capture_ms, RB_IQ_INVERT_Q);
                critical_section_exit(&iq_lock);
                rb_spectrum_result result;
                const uint32_t started = time_us_32();
                am_feed(copied_words,frequency,config_id);
                const bool ready = rb_spectrum_feed(&spectrum, copied_words,
                    STREAM_RX_DMA_WORDS, capture_ms, &result);
                const uint32_t elapsed = time_us_32() - started;
                if (elapsed > dsp_us_max) dsp_us_max = elapsed;
                if (ready) {
                    rb_spectrum_input input;
                    rb_spectrum_measure(copied_words, STREAM_RX_DMA_WORDS, &input);
                    ++sequence;
                    critical_section_enter_blocking(&snapshot_lock);
                    latest.result = result;
                    latest.input = input;
                    latest.frequency = frequency; latest.config_id = config_id;
                    latest.sequence = sequence; latest.ready = true;
                    critical_section_exit(&snapshot_lock);
                }
            }
        }
        if ((int32_t)(now - next_diagnostics) >= 0) {
            rb_web_diagnostics d = {
                .rx_pairs = spectrum.dsp.pairs, .rx_buffers = stats.completed_buffers,
                .rx_stalls = stats.stalled_transfers, .tag_errors = spectrum.tag_errors,
                .fft_windows = spectrum.dsp.windows, .dsp_us_max = dsp_us_max,
                .frames = sequence, .invalid_fft = spectrum.dsp.invalid_fft,
                .gateware_major = radio.info.major, .gateware_minor = radio.info.minor
            };
            critical_section_enter_blocking(&snapshot_lock);
            latest.diagnostics = d;
            critical_section_exit(&snapshot_lock);
            next_diagnostics = now + 250u;
        }
        tight_loop_contents();
    }
}

void rb_web_radio_start(void)
{
    critical_section_init(&snapshot_lock);
    critical_section_init(&iq_lock);
    critical_section_init(&am_lock);
    rb_am_queue_init(&am_queue);
    rb_iq_init(&iq_queue);
    queue_init(&requests, sizeof(radio_request), 1);
    queue_init(&replies, sizeof(bool), 1);
    multicore_launch_core1(radio_core);
    bool ok;
    queue_remove_blocking(&replies, &ok);
    if (!ok) panic("RadioBerry RX initialization failed");
}

bool rb_web_iq_open(uint32_t stream_id)
{
    critical_section_enter_blocking(&iq_lock);
    const bool ok = rb_iq_open(&iq_queue, stream_id);
    critical_section_exit(&iq_lock);
    return ok;
}
void rb_web_iq_close(void)
{
    critical_section_enter_blocking(&iq_lock);
    rb_iq_close(&iq_queue);
    critical_section_exit(&iq_lock);
}
bool rb_web_iq_read(uint8_t frame[RB_IQ_FRAME_BYTES])
{
    critical_section_enter_blocking(&iq_lock);
    const bool ok = rb_iq_read(&iq_queue, frame);
    critical_section_exit(&iq_lock);
    return ok;
}

bool rb_web_am_open(uint32_t id)
{
    return rb_web_rx_open(id,RB_RX_AM);
}
bool rb_web_rx_open(uint32_t id,uint8_t mode)
{
    critical_section_enter_blocking(&am_lock);
    const bool ok=rb_am_queue_open_mode(&am_queue,id,mode);
    critical_section_exit(&am_lock);return ok;
}
void rb_web_am_close(void)
{
    critical_section_enter_blocking(&am_lock);rb_am_queue_close(&am_queue);
    critical_section_exit(&am_lock);
}
bool rb_web_am_read(uint8_t *frame)
{
    critical_section_enter_blocking(&am_lock);
    const bool ok=rb_am_queue_read(&am_queue,frame);
    critical_section_exit(&am_lock);return ok;
}

static bool submit(const radio_request *request)
{
    if (!queue_try_add(&requests, request)) return false;
    bool ok;
    const absolute_time_t deadline = make_timeout_time_ms(100u);
    while (!queue_try_remove(&replies, &ok)) {
        /* Fail loudly if the owning core stops; never acknowledge an uncertain
         * configuration or let a late command silently change the frequency. */
        if (time_reached(deadline)) panic("RadioBerry tuning core did not respond");
        tight_loop_contents();
    }
    return ok;
}

bool rb_web_radio_tune(void *context, uint32_t frequency, uint32_t config_id)
{
    (void)context;
    const radio_request request = {.frequency = frequency, .config_id = config_id};
    return submit(&request);
}

bool rb_web_radio_gain(void *context, int32_t gain_db, uint32_t config_id)
{
    (void)context;
    if (gain_db < RB_WEB_RX_GAIN_MIN_DB || gain_db > RB_WEB_RX_GAIN_MAX_DB) return false;
    const radio_request request = {.config_id = config_id, .gain_db = gain_db, .gain_only = true};
    return submit(&request);
}
bool rb_web_radio_agc(void *context,uint32_t mode,int32_t threshold)
{
    (void)context;
    if(mode>RB_AGC_SLOW||threshold<RB_AGC_THRESHOLD_MIN_DBFS||threshold>RB_AGC_THRESHOLD_MAX_DBFS)return false;
    const radio_request request={.agc_only=true,.agc_mode=mode,.agc_threshold=threshold};
    return submit(&request);
}

void rb_web_radio_poll(rb_web_service *service)
{
    rb_spectrum_result result;
    rb_spectrum_input input;
    uint32_t frequency = 0, config_id = 0, sequence = 0;
    critical_section_enter_blocking(&snapshot_lock);
    service->diagnostics = latest.diagnostics;
    const bool ready = latest.ready;
    if (ready) {
        result = latest.result; frequency = latest.frequency;
        input = latest.input;
        config_id = latest.config_id; sequence = latest.sequence;
        latest.ready = false;
    }
    critical_section_exit(&snapshot_lock);
    if (ready && rb_web_publish(service, &result, frequency, config_id, sequence))
        service->input = input;
}
