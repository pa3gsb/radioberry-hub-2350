#include "protocol1_w5500_app.h"
#include <radioberry/protocol1.h>
#include <radioberry/w5500.h>

static rb_radio_t radio;
static volatile bool tx_dma_running_shared = false;
static uint32_t *tx_fill_words = NULL;
static uint8_t tx_fill_buffer_index = 0u;
static size_t tx_fill_used_words = 0u;
static uint32_t tx_pending_words[HPSDR_IQ_WORDS_PER_PACKET] = {0};
static size_t tx_pending_word_count = 0u;
volatile uint32_t spi_debug_window_shared = 0u;
static volatile bool radio_running_shared = false;
static volatile bool radio_tx_enabled_shared = false;
static volatile uint8_t active_receiver_count_shared = 1u;

#define RB_COMMAND 0x05

volatile uint32_t core1_made_shared = 0;
volatile uint32_t core1_wait_shared = 0;
volatile uint32_t core1_running_shared = 0;
volatile uint32_t core1_alive_shared = 0;
volatile uint32_t core1_seen_running_shared = 0;

static hpsdr_ctx_t hpsdr;
static fpga_loader_t gateware_loader;
static w5500_udp_t w5500_udp;
static net_datagram_t network_transport;
static rb2_ctx_t rb2_device;
static hpsdr_tx_packet_t pkt;
static hpsdr_tx_packet_t core1_pkt;

queue_t tx_packet_queue;
queue_t command_queue;

void radioberry_process_command_queue()
{
    rb_cmd_t msg;

    if (!queue_try_remove(&command_queue, &msg)) {
        return;
    }

    uint8_t response[6] = {0};
    rb_radio_write_control(&radio, msg.command, msg.data, response);

    if (spi_debug_window_shared > 0u) {
        printf(
            "spi_cmd cmd=0x%02x data=0x%08lx tx=%02x %02x %02x %02x %02x %02x rx=%02x %02x %02x %02x %02x %02x\n",
            (unsigned)msg.command,
            (unsigned long)msg.data,
            RB_COMMAND, (unsigned)msg.command,
            (unsigned)((msg.data >> 24) & 0xffu),
            (unsigned)((msg.data >> 16) & 0xffu),
            (unsigned)((msg.data >> 8) & 0xffu),
            (unsigned)(msg.data & 0xffu),
            response[0], response[1], response[2], response[3],
            response[4], response[5]);
        spi_debug_window_shared--;
    }
}

static const w5500_network_config_t network_config = {
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
    .ip = {192, 168, 2, 43},
    .subnet = {255, 255, 255, 0},
    .gateway = {0, 0, 0, 0},
    .dns = {0, 0, 0, 0},
    .use_dhcp = true
};


volatile uint32_t core1_dropped_shared = 0;
volatile uint32_t core1_invalid_shared = 0;
volatile uint32_t tx_submit_ok_shared = 0;
volatile uint32_t tx_submit_drop_shared = 0;
volatile uint32_t tx_extract_fail_shared = 0;

static void tx_fill_reset_state(void)
{
    tx_fill_words = NULL;
    tx_fill_buffer_index = 0u;
    tx_fill_used_words = 0u;
    tx_pending_word_count = 0u;
}

static size_t tx_accumulate_words(const uint32_t *src_words, size_t word_count)
{
    size_t src_offset = 0u;

    while (src_offset < word_count) {
        if (tx_fill_words == NULL) {
            if (!rb_radio_tx_acquire(&radio,
                                           &tx_fill_words,
                                           &tx_fill_buffer_index)) {
                break;
            }
            tx_fill_used_words = 0u;
        }

        size_t free_words = STREAM_TX_DMA_WORDS_PER_BUFFER - tx_fill_used_words;
        size_t copy_words = word_count - src_offset;
        if (copy_words > free_words) {
            copy_words = free_words;
        }

        memcpy(&tx_fill_words[tx_fill_used_words],
               &src_words[src_offset],
               copy_words * sizeof(uint32_t));

        tx_fill_used_words += copy_words;
        src_offset += copy_words;

        if (tx_fill_used_words == STREAM_TX_DMA_WORDS_PER_BUFFER) {
            rb_radio_tx_submit(&radio, tx_fill_buffer_index);
            tx_submit_ok_shared++;
            tx_fill_words = NULL;
            tx_fill_used_words = 0u;
        }
    }

    return src_offset;
}

static void radioberry_handle_tx_words(const uint32_t *words,
                                       size_t word_count)
{
    if (!radio_running_shared || !radio_tx_enabled_shared ||
        !tx_dma_running_shared) {
        return;
    }

    if (word_count != HPSDR_IQ_WORDS_PER_PACKET) {
        tx_extract_fail_shared++;
        return;
    }

    if (tx_pending_word_count > 0u) {
        size_t consumed_pending = tx_accumulate_words(tx_pending_words,
                                                      tx_pending_word_count);
        if (consumed_pending < tx_pending_word_count) {
            size_t remaining = tx_pending_word_count - consumed_pending;
            memmove(tx_pending_words,
                    &tx_pending_words[consumed_pending],
                    remaining * sizeof(uint32_t));
            tx_pending_word_count = remaining;
            tx_submit_drop_shared++;
            return;
        }
        tx_pending_word_count = 0u;
    }

    size_t consumed = tx_accumulate_words(words, word_count);
    if (consumed < word_count) {
        tx_pending_word_count = word_count - consumed;
        memcpy(tx_pending_words,
               &words[consumed],
               tx_pending_word_count * sizeof(uint32_t));
    }
}

static void protocol_state_changed(void *user,
                                   bool running,
                                   bool tx_enabled)
{
    (void)user;
    radio_running_shared = running;
    radio_tx_enabled_shared = tx_enabled;
    spi_debug_window_shared = 24u;
}

static void protocol_command_received(void *user,
                                      uint8_t command,
                                      uint32_t data)
{
    (void)user;
    rb_cmd_t message = {.command = command, .data = data};
    queue_try_add(&command_queue, &message);
}

static void protocol_receiver_count_changed(void *user,
                                            uint8_t receiver_count)
{
    (void)user;
    active_receiver_count_shared = receiver_count;
}

static void protocol_tx_iq_received(void *user,
                                    const uint32_t *words,
                                    size_t word_count)
{
    (void)user;
    radioberry_handle_tx_words(words, word_count);
}

void core1_main(void)
{
    bool dma_running = false;
    uint8_t assembled_receiver_count = 0u;
    static hpsdr_iq_assembler_t assembler = {0};

    while (true) {
        core1_alive_shared++;
        core1_running_shared = radio_running_shared;

        radioberry_process_command_queue();

        if (radio_running_shared && !dma_running) {
            if (!hpsdr_iq_assembler_init(&assembler,
                                         active_receiver_count_shared)) {
                tight_loop_contents();
                continue;
            }
            assembled_receiver_count = active_receiver_count_shared;

            hpsdr_prepare_tx_packet(&hpsdr, &core1_pkt);
            rb_radio_rx_start(&radio);

            dma_running = true;
        }

        if (dma_running &&
            assembled_receiver_count != active_receiver_count_shared) {
            hpsdr_iq_assembler_init(&assembler,
                                    active_receiver_count_shared);
            assembled_receiver_count = active_receiver_count_shared;
            hpsdr_prepare_tx_packet(&hpsdr, &core1_pkt);
        }

        if (!radio_running_shared && dma_running) {
            rb_radio_rx_stop(&radio);
            dma_running = false;
        }

        if (dma_running) {
            const uint32_t *samples;
            uint8_t buffer_index;

            if (rb_radio_rx_acquire(
                    &radio,
                    &samples,
                    &buffer_index)) {

                for (uint32_t n = 0;
                     n < STREAM_RX_DMA_WORDS;
                     n++) {

                    bool packet_ready = hpsdr_process_rx_dma_word(
                        &assembler,
                        &core1_pkt,
                        samples[n]
                    );

                    if (packet_ready) {
                        if (queue_try_add(
                                &tx_packet_queue,
                                &core1_pkt)) {
                            core1_made_shared++;
                        } else {
                            core1_wait_shared++;
                        }
                        hpsdr_prepare_tx_packet(&hpsdr, &core1_pkt);
                    }
                }
                rb_radio_rx_release(
                    &radio,
                    buffer_index
                );
            }
        }
        tight_loop_contents();
    }
}

int main() {
    stdio_init_all();
    sleep_ms(100);

    queue_init(&command_queue, sizeof(rb_cmd_t), 32);
    queue_init(&tx_packet_queue, sizeof(hpsdr_tx_packet_t), 32);

    printf("Embedded FPGA bitstream: %s (%u bytes)\n", radioberry_rbf_name, (unsigned)radioberry_rbf_size);
    const fpga_loader_io_t gateware_io = fpga_loader_pico_io();
    if (!fpga_loader_init(&gateware_loader, &gateware_io)) {
        panic("Initialiseren FPGA-loader mislukt");
    }
    const fpga_loader_result_t result = fpga_loader_program(
        &gateware_loader, radioberry_rbf_data, radioberry_rbf_size);
    if (result != FPGA_LOADER_OK) {
        printf("FPGA configuration failed with code %d\n", (int)result);
    } else printf("FPGA configuration successfully loaded\n");
    
    sleep_ms(2000);

    if (!rb_radio_init(&radio, pio0, 0u, 1u)) {
        panic("Initialiseren RadioBerry mislukt");
    }
    if (!rb_radio_detect(&radio, 50u, 3u, 100u)) {
        printf("Geen stabiele gatewareversie ontvangen\n");
    }
    memset(&rb2_device, 0, sizeof(rb2_device));
    rb2_device.gateware.major = radio.info.major;
    rb2_device.gateware.minor = radio.info.minor;
    rb2_device.gateware.fpga = radio.info.fpga;
    rb2_device.gateware.nr = radio.info.receiver_count;
    rb2_device.gateware.nt = radio.info.transmitter_count;

    printf("clk_sys  = %lu Hz\n", clock_get_hz(clk_sys));
    printf("clk_peri = %lu Hz\n", clock_get_hz(clk_peri));
    printf("clk_usb  = %lu Hz\n", clock_get_hz(clk_usb));
    printf("clk_adc  = %lu Hz\n", clock_get_hz(clk_adc));

    if (!w5500_network_init(&network_config)) {
        panic("Initialiseren W5500 mislukt");
    }
    w5500_udp_transport_init(&w5500_udp, 0u, &network_transport);
    const hpsdr_config_t hpsdr_config = {
        .transport = network_transport,
        .device = {
            .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},
            .gateware_major = rb2_device.gateware.major,
            .gateware_minor = rb2_device.gateware.minor,
            .receiver_count = rb2_device.gateware.nr
        },
        .callbacks = {
            .user = NULL,
            .on_state_changed = protocol_state_changed,
            .on_command = protocol_command_received,
            .on_receiver_count_changed = protocol_receiver_count_changed,
            .on_tx_iq_words = protocol_tx_iq_received
        }
    };
    if (!hpsdr_init(&hpsdr, &hpsdr_config)) {
        panic("Openen HPSDR UDP transport mislukt");
    }

    multicore_launch_core1(core1_main);

    uint32_t polls = 0;
    uint32_t removes = 0;
    uint32_t sent = 0;
    uint32_t fail = 0;
    uint32_t sent_last = 0;
    uint64_t stat_last_us = time_us_64();
    bool tx_running = false;

    while (true) {
        polls++;

        w5500_network_poll();

        if (radio_running_shared && radio_tx_enabled_shared && !tx_running) {
            tx_fill_reset_state();
            rb_radio_tx_start(&radio);
            tx_running = true;
            tx_dma_running_shared = true;
        }

        if ((!radio_running_shared || !radio_tx_enabled_shared) && tx_running) {
            rb_radio_tx_stop(&radio);
            tx_fill_reset_state();
            tx_running = false;
            tx_dma_running_shared = false;
        }

        hpsdr_poll(&hpsdr);
        int drained = 0;
        while (drained < 16 && queue_try_remove(&tx_packet_queue, &pkt)) {
            removes++;
            drained++;
            int32_t ret = net_datagram_send(
                &hpsdr.transport,
                pkt.data,
                HPSDR_PKT_SIZE,
                &hpsdr.stream_remote
            );
        if (ret < 0) fail++; else sent++;
    }

    uint64_t now_us = time_us_64();
    if (now_us - stat_last_us >= 1000000) {
        uint32_t sent_delta = sent - sent_last;
        uint32_t q_level = queue_get_level(&tx_packet_queue);
        uint32_t iq_per_sec = sent_delta * 126;
        uint32_t tx_enabled = radio_tx_enabled_shared ? 1u : 0u;
        uint32_t tx_active = tx_dma_running_shared ? 1u : 0u;
        uint32_t tx_loop = tx_running ? 1u : 0u;
        uint32_t tx_ready_pin = gpio_get(9) ? 1u : 0u;

        rb_radio_stats_t radio_stats = rb_radio_get_stats(&radio);
        stream_rx_dma_stats_t ds = radio_stats.rx;
        stream_tx_dma_stats_t txs = radio_stats.tx;

        printf(
        "pps=%lu iq/s=%lu sent=%lu fail=%lu q=%lu "
        "dma_done=%lu dma_stall=%lu dropped=%lu invalid=%lu "
        "tx_ok=%lu tx_drop=%lu tx_bad=%lu tx_dma_done=%lu tx_dma_stall=%lu tx_udr=%lu "
        "tx_en=%lu tx_act=%lu tx_loop=%lu tx_rdy=%lu\n",
        sent_delta,
        iq_per_sec,
        sent,
        fail,
        q_level,
        ds.completed_buffers,
        ds.stalled_transfers,
        core1_dropped_shared,
        core1_invalid_shared,
        tx_submit_ok_shared,
        tx_submit_drop_shared,
        tx_extract_fail_shared,
        txs.completed_buffers,
        txs.stalled_transfers,
        txs.underruns,
        tx_enabled,
        tx_active,
        tx_loop,
        tx_ready_pin
);

    sent_last = sent;
    stat_last_us = now_us;
}
    tight_loop_contents();}
}

// end of source file radioberry.c
