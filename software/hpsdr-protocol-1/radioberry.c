#include "radioberry.h"
#include "hpsdr_protocol1.h"
#include "stream_rx_dma.h"

static stream_rx_dma_t iq_dma;
static stream_tx_dma_t tx_dma;
static volatile bool tx_dma_running_shared = false;
static uint32_t *tx_fill_words = NULL;
static uint8_t tx_fill_buffer_index = 0u;
static size_t tx_fill_used_words = 0u;
static uint32_t tx_pending_words[HPSDR_IQ_WORDS_PER_PACKET] = {0};
static size_t tx_pending_word_count = 0u;
volatile uint32_t spi_debug_window_shared = 0u;

#define RB_COMMAND 0x05

volatile uint32_t core1_made_shared = 0;
volatile uint32_t core1_wait_shared = 0;
volatile uint32_t core1_running_shared = 0;
volatile uint32_t core1_alive_shared = 0;
volatile uint32_t core1_seen_running_shared = 0;

static hpsdr_ctx_t hpsdr;
static rb2_ctx_t rb2_device;
static hpsdr_tx_packet_t pkt;
static hpsdr_tx_packet_t core1_pkt;

queue_t tx_packet_queue;
queue_t command_queue;

uint8_t radioberry_spi_tx[6] = {0};
uint8_t radioberry_spi_rx[6] = {0};

void radioberry_process_command_queue()
{
    rb_cmd_t msg;

    if (!queue_try_remove(&command_queue, &msg)) {
        return;
    }

    radioberry_spi_tx[0] = RB_COMMAND;  
    radioberry_spi_tx[1] = msg.command;
    radioberry_spi_tx[2] = (msg.data >> 24) & 0xFF;
    radioberry_spi_tx[3] = (msg.data >> 16) & 0xFF;
    radioberry_spi_tx[4] = (msg.data >> 8) & 0xFF;
    radioberry_spi_tx[5] = msg.data & 0xFF;

    radioberry_spi_write_read_blocking(radioberry_spi_tx, radioberry_spi_rx, 6);

    if (spi_debug_window_shared > 0u) {
        printf(
            "spi_cmd cmd=0x%02x data=0x%08lx tx=%02x %02x %02x %02x %02x %02x rx=%02x %02x %02x %02x %02x %02x\n",
            (unsigned)msg.command,
            (unsigned long)msg.data,
            (unsigned)radioberry_spi_tx[0],
            (unsigned)radioberry_spi_tx[1],
            (unsigned)radioberry_spi_tx[2],
            (unsigned)radioberry_spi_tx[3],
            (unsigned)radioberry_spi_tx[4],
            (unsigned)radioberry_spi_tx[5],
            (unsigned)radioberry_spi_rx[0],
            (unsigned)radioberry_spi_rx[1],
            (unsigned)radioberry_spi_rx[2],
            (unsigned)radioberry_spi_rx[3],
            (unsigned)radioberry_spi_rx[4],
            (unsigned)radioberry_spi_rx[5]);
        spi_debug_window_shared--;
    }
}

static wiz_NetInfo g_net_info = {
    // pay attention in disccovery send same...
    .mac = {0x00, 0x08, 0xDC, 0x12, 0x34, 0x56},

    // IP-adres van RP2350/W5500
    .ip = {169, 254, 46, 43},

    // Zelfde subnet als PC
    .sn = {255, 255, 0, 0},

    .gw = {0, 0, 0, 0},
    .dns = {0, 0, 0, 0},

    .dhcp = NETINFO_STATIC
};

void w5500_init(void) {
    wizchip_spi_initialize();
    wizchip_cris_initialize();

    wizchip_reset();
    wizchip_initialize();
    wizchip_check();

    network_initialize(g_net_info);

    print_network_information(g_net_info);
}


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
            if (!stream_tx_dma_try_acquire(&tx_dma,
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
            stream_tx_dma_submit(&tx_dma, tx_fill_buffer_index);
            tx_submit_ok_shared++;
            tx_fill_words = NULL;
            tx_fill_used_words = 0u;
        }
    }

    return src_offset;
}

void radioberry_handle_tx_data_packet(const uint8_t *buffer, size_t len)
{
    uint32_t temp_words[HPSDR_IQ_WORDS_PER_PACKET] = {0};
    size_t word_count = 0u;

    if (!hpsdr_running_shared || !hpsdr_tx_enabled_shared ||
        !tx_dma_running_shared) {
        return;
    }

    if (!hpsdr_extract_tx_words_from_data_packet(
            buffer,
            len,
            temp_words,
            HPSDR_IQ_WORDS_PER_PACKET,
            &word_count)) {
        tx_extract_fail_shared++;
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

    size_t consumed = tx_accumulate_words(temp_words, word_count);
    if (consumed < word_count) {
        tx_pending_word_count = word_count - consumed;
        memcpy(tx_pending_words,
               &temp_words[consumed],
               tx_pending_word_count * sizeof(uint32_t));
    }
}

void core1_main(void)
{
    bool dma_running = false;
    static iq_assembler_t assembler = {0};

    while (true) {
        core1_alive_shared++;
        core1_running_shared = hpsdr_running_shared;

        radioberry_process_command_queue();

        if (hpsdr_running_shared && !dma_running) {
            memset(&assembler, 0, sizeof(assembler));

            hpsdr_prepare_tx_packet(&core1_pkt);
            stream_rx_dma_start(&iq_dma);

            dma_running = true;
        }

        if (!hpsdr_running_shared && dma_running) {
            stream_rx_dma_stop(&iq_dma);
            dma_running = false;
        }

        if (dma_running) {
            const uint32_t *samples;
            uint8_t buffer_index;

            if (stream_rx_dma_try_acquire(
                    &iq_dma,
                    &samples,
                    &buffer_index)) {

                for (uint32_t n = 0;
                     n < STREAM_RX_DMA_WORDS;
                     n++) {

                    bool packet_ready = process_dma_word(
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
                        hpsdr_prepare_tx_packet(&core1_pkt);
                    }
                }
                stream_rx_dma_release(
                    &iq_dma,
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
    fpga_loader_init();
    const fpga_loader_result_t result = fpga_loader_program(radioberry_rbf_data, radioberry_rbf_size);
    if (result != FPGA_LOADER_OK) {
        printf("FPGA configuration failed with code %d\n", (int)result);
    } else printf("FPGA configuration successfully loaded\n");
    
    radioberry_spi_init();
    sleep_ms(2000);
 
    uint8_t last_major = 0;
    uint8_t last_minor = 0;
    unsigned stable = 0;
    memset(radioberry_spi_tx, 0, sizeof(radioberry_spi_tx));
    memset(radioberry_spi_rx, 0, sizeof(radioberry_spi_rx));
    for (unsigned attempt = 0; attempt < 50 && stable < 3; attempt++) {
        radioberry_spi_write_read_blocking(
            radioberry_spi_tx,
            radioberry_spi_rx,
            sizeof(radioberry_spi_tx)
        );
        uint8_t major = radioberry_spi_rx[4];
        uint8_t minor = radioberry_spi_rx[5];
        if (major != 0 && major != 0xFF &&
            major == last_major && minor == last_minor) {
            stable++;
        } else {
            stable = 0;
            last_major = major;
            last_minor = minor;
        }
        sleep_ms(100);
    }

    if (stable < 3) {
        printf("Geen stabiele gatewareversie ontvangen\n");
        last_major = 0;
        last_minor = 0;
    }
    memset(&rb2_device, 0, sizeof(rb2_device));
    rb2_device.gateware.major = last_major;
    rb2_device.gateware.minor = last_minor;
    rb2_device.gateware.fpga = radioberry_spi_rx[3] & 0x03; 
	rb2_device.gateware.nr   = ((radioberry_spi_rx[3] & 0x3C) >> 2);
	rb2_device.gateware.nt 	 = ((radioberry_spi_rx[3] & 0xC0) >> 6); 

    // PIO setup for IQ sampling.
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &rx_iq_sample_program);
    rx_iq_sample_program_init(pio, sm, offset);
    if (!stream_rx_dma_init(&iq_dma, pio, sm)) {
        panic("Initialiseren PIO RX DMA mislukt");
    }

    uint tx_sm = 1;
    uint tx_offset = pio_add_program(pio, &tx_iq_sample_program);
    tx_iq_sample_program_init(pio, tx_sm, tx_offset);
    if (!stream_tx_dma_init(&tx_dma, pio, tx_sm)) {
        panic("Initialiseren PIO TX DMA mislukt");
    }

    printf("clk_sys  = %lu Hz\n", clock_get_hz(clk_sys));
    printf("clk_peri = %lu Hz\n", clock_get_hz(clk_peri));
    printf("clk_usb  = %lu Hz\n", clock_get_hz(clk_usb));
    printf("clk_adc  = %lu Hz\n", clock_get_hz(clk_adc));

    w5500_init();
    hpsdr_init(&hpsdr);
    hpsdr.rb2_device = rb2_device;  /* Set the Radioberry device context */

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

        if (hpsdr_running_shared && hpsdr_tx_enabled_shared && !tx_running) {
            tx_fill_reset_state();
            stream_tx_dma_start(&tx_dma);
            tx_running = true;
            tx_dma_running_shared = true;
        }

        if ((!hpsdr_running_shared || !hpsdr_tx_enabled_shared) && tx_running) {
            stream_tx_dma_stop(&tx_dma);
            tx_fill_reset_state();
            tx_running = false;
            tx_dma_running_shared = false;
        }

        hpsdr_poll(&hpsdr);
        int drained = 0;
        while (drained < 16 && queue_try_remove(&tx_packet_queue, &pkt)) {
            removes++;
            drained++;
            int ret = w5500_udp_sendto(
                hpsdr.udp_sock,
                pkt.data,
                HPSDR_PKT_SIZE,
                hpsdr.stream_remote.ip,
                hpsdr.stream_remote.port
            );
        if (ret < 0) fail++; else sent++;
    }

    uint64_t now_us = time_us_64();
    if (now_us - stat_last_us >= 1000000) {
        uint32_t sent_delta = sent - sent_last;
        uint32_t q_level = queue_get_level(&tx_packet_queue);
        uint32_t iq_per_sec = sent_delta * 126;
        uint32_t tx_enabled = hpsdr_tx_enabled_shared ? 1u : 0u;
        uint32_t tx_active = tx_dma_running_shared ? 1u : 0u;
        uint32_t tx_loop = tx_running ? 1u : 0u;
        uint32_t tx_ready_pin = gpio_get(9) ? 1u : 0u;

        stream_rx_dma_stats_t ds =
            stream_rx_dma_get_stats(&iq_dma);
        stream_tx_dma_stats_t txs =
            stream_tx_dma_get_stats(&tx_dma);

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