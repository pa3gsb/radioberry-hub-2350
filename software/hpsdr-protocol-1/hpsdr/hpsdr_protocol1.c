#include "hpsdr_protocol1.h"
#include "w5500_udp.h"

#define HPSDR_CODE_DISCOVERY   0x0002feef
#define HPSDR_CODE_STOP        0x0004feef
#define HPSDR_CODE_START_1     0x0104feef
#define HPSDR_CODE_START_2     0x0304feef
#define HPSDR_CODE_DATA        0x0201feef

#define HPSDR_DATA_SIZE 1032

static const uint8_t header_hpsdrdata[4] = {
    0xEF, 0xFE, 0x01, 0x06
};

static const uint8_t sync_hpsdrdata[8] = {
    0x7F, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define HPSDR_FRAME_SIZE_BYTES         512u
#define HPSDR_FRAME_PAYLOAD_OFFSET     16u
#define HPSDR_GROUP_STRIDE_BYTES       8u
#define HPSDR_GROUPS_PER_FRAME         63u

#define HERMESLITE             0x06

volatile bool hpsdr_running_shared = false;
volatile bool hpsdr_tx_enabled_shared = false;
volatile uint32_t hpsdr_sequence_shared = 0;


void hpsdr_init(hpsdr_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->udp_sock = 0;
    ctx->running = false;
    ctx->last_sequence_number = 0;

    w5500_udp_open(ctx->udp_sock, HPSDR_PORT);
}

void hpsdr_start(hpsdr_ctx_t *ctx)
{
    if (!ctx->running) {
        ctx->last_sequence_number = 0;
        hpsdr_sequence_shared = 0;
    }

    ctx->running = true;
    hpsdr_running_shared = true;
    hpsdr_tx_enabled_shared = false;

    printf("hpsdr_start: running=%d shared=%d\n",
           ctx->running,
           hpsdr_running_shared);
}

void hpsdr_stop(hpsdr_ctx_t *ctx)
{
    ctx->running = false;
    hpsdr_running_shared = false;
    hpsdr_tx_enabled_shared = false;

    ctx->last_sequence_number = 0;
    hpsdr_sequence_shared = 0;

    printf("hpsdr_stop: running=%d shared=%d\n",
           ctx->running,
           hpsdr_running_shared);
} 

void hpsdr_send_discovery_reply(hpsdr_ctx_t *ctx)
{
    uint8_t *reply = ctx->discovery_reply;

    memset(reply, 0, 60);

    reply[0x00] = 0xEF;
    reply[0x01] = 0xFE;
    reply[0x02] = ctx->running ? 0x03 : 0x02;

    reply[0x03] = 0x00;
    reply[0x04] = 0x08;
    reply[0x05] = 0xDC;
    reply[0x06] = 0x12;
    reply[0x07] = 0x34;
    reply[0x08] = 0x56;

    reply[0x09] = (uint8_t)ctx->rb2_device.gateware.major;
    reply[0x0A] = HERMESLITE;
    reply[0x13] = (uint8_t)ctx->rb2_device.gateware.nr;
    reply[0x15] = (uint8_t)ctx->rb2_device.gateware.minor;

    int ret = w5500_udp_sendto(
        ctx->udp_sock,
        reply,
        60,
        ctx->remote.ip,
        ctx->remote.port
    );

    printf("Discovery sent: ret=%d version=%02X.%02X\n",
           ret, reply[0x09], reply[0x15]);
}

void hpsdr_poll(hpsdr_ctx_t *ctx)
{
    uint16_t available = getSn_RX_RSR(ctx->udp_sock);

    if (available == 0) {
        return;
    }

    if (available > sizeof(ctx->rx_buffer)) {
        available = sizeof(ctx->rx_buffer);
    }

    uint8_t ip[4] = {0};
    uint16_t port = 0;

    uint16_t len = w5500_udp_recvfrom(
        ctx->udp_sock,
        ctx->rx_buffer,
        available,
        ip,
        &port
    );

    if (len > 3) {
        memcpy(ctx->remote.ip, ip, 4);
        ctx->remote.port = port;
        hpsdr_handle_packet(ctx, ctx->rx_buffer, len);
    }
}

void hpsdr_handle_packet(hpsdr_ctx_t *ctx, uint8_t *buffer, uint16_t len)
{
    uint32_t code;
    memcpy(&code, buffer, 4);

    switch (code) {

    case HPSDR_CODE_DISCOVERY:
        printf("Discovery packet received\n");
        hpsdr_send_discovery_reply(ctx);
        break;

    case HPSDR_CODE_STOP:
        printf("Stop command received\n");
        hpsdr_stop(ctx);
        break;

    case HPSDR_CODE_START_1:
    case HPSDR_CODE_START_2:
        printf("Start command received\n");

        memcpy(ctx->stream_remote.ip, ctx->remote.ip, 4);
        ctx->stream_remote.port = ctx->remote.port;

        hpsdr_start(ctx);
        break;

    case HPSDR_CODE_DATA:
        hpsdr_process_packet(ctx, buffer, len);
        break;

    default:
        printf("Packet not for me: 0x%08lx\n", code);
        break;
    }
}

void hpsdr_process_packet(hpsdr_ctx_t *ctx, const uint8_t *buffer, uint16_t len)
{
    static bool prev_mox = false;

    if (len >= 12u) {
        bool current_mox = ((buffer[11] & 0x01u) != 0u);
        hpsdr_tx_enabled_shared = current_mox;

        if (current_mox != prev_mox) {
            spi_debug_window_shared = 24u;
             printf("mox_edge_pkt state=%lu spi_dbg_window=%lu c0=%02x %02x %02x %02x %02x c1=%02x %02x %02x %02x %02x\n",
                   (unsigned long)(current_mox ? 1u : 0u),
                 (unsigned long)spi_debug_window_shared,
                 (unsigned)buffer[11],
                 (unsigned)buffer[12],
                 (unsigned)buffer[13],
                 (unsigned)buffer[14],
                 (unsigned)buffer[15],
                 (unsigned)buffer[523],
                 (unsigned)buffer[524],
                 (unsigned)buffer[525],
                 (unsigned)buffer[526],
                 (unsigned)buffer[527]);
            prev_mox = current_mox;
        }
    }

    if (len >= 528u) {
        hpsdr_handle_command(&ctx->rb2_device, 11, buffer);
        hpsdr_handle_command(&ctx->rb2_device, 523, buffer);
    }

    if (len >= HPSDR_PKT_SIZE && hpsdr_tx_enabled_shared) {
        radioberry_handle_tx_data_packet(buffer, (size_t)len);
    }
}

void hpsdr_handle_command(rb2_ctx_t *ctx, int base_index, const uint8_t *buffer)
{
    uint8_t command;
    uint32_t command_data;

    command = buffer[base_index];

    command_data =
        ((uint32_t)buffer[base_index + 1] << 24) |
        ((uint32_t)buffer[base_index + 2] << 16) |
        ((uint32_t)buffer[base_index + 3] << 8)  |
        ((uint32_t)buffer[base_index + 4]);

    if (ctx->commands[command] != command_data) {
        ctx->commands[command] = command_data;

        rb_cmd_t msg = {
            .command = command,
            .data = command_data
        };

        queue_try_add(&command_queue, &msg);
    }
}

static void write_24be(uint8_t *dst, uint32_t value)
{
    dst[0] = (value >> 16) & 0xFF;
    dst[1] = (value >> 8) & 0xFF;
    dst[2] = (value >> 0) & 0xFF;
}

static uint32_t read_24be_u32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 16) |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2]);
}

static uint16_t read_16be_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) |
                      ((uint16_t)src[1]));
}

static void read_iq_sample(PIO pio, uint sm, uint8_t *dst)
{
    uint32_t i_sample = 0;
    uint32_t q_sample = 0;
    bool have_i = false;
    bool have_q = false;

    while (!have_i || !have_q) {
        uint32_t raw = pio_sm_get_blocking(pio, sm);

        uint8_t seq = (raw >> 24) & 0x0F;
        uint32_t value = raw & 0x00FFFFFF;

        if (seq == 0) {
            i_sample = value;
            have_i = true;
        } else if (seq == 1) {
            q_sample = value;
            have_q = true;
        }
    }
    write_24be(dst + 0, i_sample);
    write_24be(dst + 3, q_sample);
}

void hpsdr_fill_packet_from_pio(hpsdr_tx_packet_t *pkt, PIO pio, uint sm)
{
    uint32_t seq = hpsdr_sequence_shared++;

    memset(pkt->data, 0, HPSDR_PKT_SIZE);
    pkt->data[0] = 0xEF;
    pkt->data[1] = 0xFE;
    pkt->data[2] = 0x01;
    pkt->data[3] = 0x06;

    pkt->data[4] = (seq >> 24) & 0xFF;
    pkt->data[5] = (seq >> 16) & 0xFF;
    pkt->data[6] = (seq >> 8) & 0xFF;
    pkt->data[7] = seq & 0xFF;

    memcpy(pkt->data + 8, sync_hpsdrdata, 8);
    memcpy(pkt->data + 520, sync_hpsdrdata, 8);

    int nrx = 1;
    int stride = (6 * nrx) + 2;

    for (int frame = 0; frame < 2; frame++) {
        int coarse_pointer = frame * 512;
        int groups = 504 / stride;

        for (int i = 0; i < groups; i++) {
            int index = 16 + coarse_pointer + (i * stride);
            read_iq_sample(pio, sm, &pkt->data[index]);
        }
    }
}

void hpsdr_send_packet(hpsdr_ctx_t *ctx, PIO pio, uint sm)
{
    if (!ctx->running || ctx->stream_remote.port == 0) {
        return;
    }
    memset(ctx->tx_buffer, 0, HPSDR_PKT_SIZE);
    memcpy(ctx->tx_buffer, header_hpsdrdata, 4);
    ctx->tx_buffer[4] = (ctx->last_sequence_number >> 24) & 0xFF;
    ctx->tx_buffer[5] = (ctx->last_sequence_number >> 16) & 0xFF;
    ctx->tx_buffer[6] = (ctx->last_sequence_number >> 8) & 0xFF;
    ctx->tx_buffer[7] = ctx->last_sequence_number & 0xFF;
    ctx->last_sequence_number++;

    memcpy(ctx->tx_buffer + 8, sync_hpsdrdata, 8);
    memcpy(ctx->tx_buffer + 520, sync_hpsdrdata, 8);

    int nrx = 1;
    int stride = (6 * nrx) + 2;   // 8 bytes bij 1 receiver

    for (int frame = 0; frame < 2; frame++) {
        int coarse_pointer = frame * 512;
        int groups = 504 / stride;
        for (int i = 0; i < groups; i++) {
            int index = 16 + coarse_pointer + (i * stride);
            read_iq_sample(pio, sm, &ctx->tx_buffer[index]);
        }
    }

    int32_t ret;

    ret = w5500_udp_sendto(
        ctx->udp_sock,
        ctx->tx_buffer,
        HPSDR_PKT_SIZE,
        ctx->stream_remote.ip,
        ctx->stream_remote.port
    );

    if (ret < 0) {
        printf("Failed to send UDP packet to %u.%u.%u.%u:%u\n",
               ctx->stream_remote.ip[0],
               ctx->stream_remote.ip[1],
               ctx->stream_remote.ip[2],
               ctx->stream_remote.ip[3],
               ctx->stream_remote.port);
    }
}


bool hpsdr_fill_packet_from_dma(
    hpsdr_tx_packet_t *pkt,
    const uint32_t *samples)
{
    uint32_t seq = hpsdr_sequence_shared++;

    memset(pkt->data, 0, HPSDR_PKT_SIZE);

    pkt->data[0] = 0xEF;
    pkt->data[1] = 0xFE;
    pkt->data[2] = 0x01;
    pkt->data[3] = 0x06;

    pkt->data[4] = (seq >> 24) & 0xFF;
    pkt->data[5] = (seq >> 16) & 0xFF;
    pkt->data[6] = (seq >> 8) & 0xFF;
    pkt->data[7] = seq & 0xFF;

    memcpy(pkt->data + 8, sync_hpsdrdata, 8);
    memcpy(pkt->data + 520, sync_hpsdrdata, 8);

    unsigned sample_index = 0;

    for (int frame = 0; frame < 2; frame++) {
        for (int group = 0; group < 63; group++) {
            uint32_t i_raw = samples[sample_index++];
            uint32_t q_raw = samples[sample_index++];

            uint8_t i_seq = (i_raw >> 24) & 0x0F;
            uint8_t q_seq = (q_raw >> 24) & 0x0F;

            if (i_seq != 0 || q_seq != 1) {
                return false;
            }

            int index = 16 + frame * 512 + group * 8;

            write_24be(&pkt->data[index],
                       i_raw & 0x00FFFFFF);

            write_24be(&pkt->data[index + 3],
                       q_raw & 0x00FFFFFF);
        }
    }

    return true;
}



bool process_dma_word(
    iq_assembler_t *a,
    hpsdr_tx_packet_t *pkt,
    uint32_t raw)
{
    uint8_t tag = (raw >> 24) & 0x0F;
    uint32_t value = raw & 0x00FFFFFF;

    if (tag == 0) {
        /* Nieuwe I: hiermee synchroniseren we opnieuw. */
        a->i_sample = value;
        a->have_i = true;
        return false;
    }

    if (tag != 1 || !a->have_i) {
        /* Q zonder voorafgaande I, of onbekende tag. */
        return false;
    }

    uint16_t frame = a->pair_count / 63;
    uint16_t group = a->pair_count % 63;
    uint16_t index = 16 + frame * 512 + group * 8;

    write_24be(&pkt->data[index],     a->i_sample);
    write_24be(&pkt->data[index + 3], value);

    a->have_i = false;
    a->pair_count++;

    if (a->pair_count == 126) {
        a->pair_count = 0;
        return true; /* Compleet HPSDR-pakket */
    }

    return false;
}

void hpsdr_prepare_tx_packet(hpsdr_tx_packet_t *pkt)
{
    uint32_t seq = hpsdr_sequence_shared++;

    memset(pkt->data, 0, HPSDR_PKT_SIZE);

    memcpy(&pkt->data[0], header_hpsdrdata, 4);

    pkt->data[4] = (seq >> 24) & 0xFF;
    pkt->data[5] = (seq >> 16) & 0xFF;
    pkt->data[6] = (seq >> 8) & 0xFF;
    pkt->data[7] = seq & 0xFF;

    memcpy(&pkt->data[8],   sync_hpsdrdata, 8);
    memcpy(&pkt->data[520], sync_hpsdrdata, 8);
}

bool hpsdr_extract_tx_words_from_data_packet(const uint8_t *buffer,
                                             size_t len,
                                             uint32_t *out_words,
                                             size_t out_word_capacity,
                                             size_t *out_word_count)
{
    static uint32_t debug_packet_counter = 0u;
    static uint32_t debug_print_budget = 12u;

    if (out_word_count != NULL) {
        *out_word_count = 0u;
    }

    if (buffer == NULL || out_words == NULL ||
        out_word_count == NULL ||
        len < HPSDR_PKT_SIZE ||
        out_word_capacity < HPSDR_IQ_WORDS_PER_PACKET) {
        return false;
    }

    size_t produced = 0u;
    uint32_t first_i = 0u;
    uint32_t first_q = 0u;
    uint16_t first_i16 = 0u;
    uint16_t first_q16 = 0u;
    uint8_t first_group[HPSDR_GROUP_STRIDE_BYTES] = {0};
    bool first_sample_set = false;
    bool saw_nonzero = false;

    for (size_t frame = 0; frame < 2u; frame++) {
        size_t frame_base = frame * HPSDR_FRAME_SIZE_BYTES;

        for (size_t group = 0; group < HPSDR_GROUPS_PER_FRAME; group++) {
            size_t index = HPSDR_FRAME_PAYLOAD_OFFSET +
                           frame_base +
                           (group * HPSDR_GROUP_STRIDE_BYTES);

            /*
             * Confirmed TX group layout is 8 bytes where the last 4 bytes are I/Q:
             *   [0..3] aux/mic/control (not used for TX stream)
             *   [4..5] I sample (16-bit, big-endian, signed)
             *   [6..7] Q sample (16-bit, big-endian, signed)
             *
             * Internal TX stream format uses 24-bit signed samples, so expand 16->24
             * by sign extension and left shift for amplitude scaling.
             */
            int16_t i16 = (int16_t)read_16be_u16(&buffer[index + 4u]);
            int16_t q16 = (int16_t)read_16be_u16(&buffer[index + 6u]);
            uint32_t packed_word = ((uint32_t)(uint16_t)i16 << 16) |
                                   (uint32_t)(uint16_t)q16;
            int32_t i24s = ((int32_t)i16) << 8;
            int32_t q24s = ((int32_t)q16) << 8;
            uint32_t i_sample = ((uint32_t)i24s) & 0x00FFFFFFu;
            uint32_t q_sample = ((uint32_t)q24s) & 0x00FFFFFFu;

            if (!first_sample_set) {
                first_i = i_sample;
                first_q = q_sample;
                first_i16 = read_16be_u16(&buffer[index + 4u]);
                first_q16 = read_16be_u16(&buffer[index + 6u]);
                memcpy(first_group, &buffer[index], HPSDR_GROUP_STRIDE_BYTES);
                first_sample_set = true;
            }
            if (i_sample != 0u || q_sample != 0u) {
                saw_nonzero = true;
            }

            out_words[produced++] = packed_word;
        }
    }

    debug_packet_counter++;
    if (debug_print_budget > 0u && ((debug_packet_counter & 0x7Fu) == 0u)) {
         printf("tx_iq_dbg pkt=%lu first_i24=0x%06lx first_q24=0x%06lx first_i16=0x%04x first_q16=0x%04x raw=%02x%02x%02x%02x%02x%02x%02x%02x nz=%lu\n",
               (unsigned long)debug_packet_counter,
               (unsigned long)(first_i & 0x00FFFFFFu),
               (unsigned long)(first_q & 0x00FFFFFFu),
             (unsigned int)first_i16,
             (unsigned int)first_q16,
             (unsigned int)first_group[0],
             (unsigned int)first_group[1],
             (unsigned int)first_group[2],
             (unsigned int)first_group[3],
             (unsigned int)first_group[4],
             (unsigned int)first_group[5],
             (unsigned int)first_group[6],
             (unsigned int)first_group[7],
               (unsigned long)(saw_nonzero ? 1u : 0u));
        debug_print_budget--;
    }

    *out_word_count = produced;
    return true;
}

// end of source file hpsdr_protocol1.c