#include <radioberry/protocol1.h>

#include <stdio.h>
#include <string.h>

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
#define HPSDR_FRAME_PAYLOAD_BYTES      504u
#define HPSDR_RECEIVER_BYTES           6u
#define HPSDR_AUDIO_BYTES              2u
#define HPSDR_TX_GROUP_STRIDE_BYTES    8u
#define HPSDR_TX_GROUPS_PER_FRAME      63u

#define HERMESLITE             0x06

static void hpsdr_handle_command(hpsdr_ctx_t *ctx,
                                 int base_index,
                                 const uint8_t *buffer);

bool hpsdr_init(hpsdr_ctx_t *ctx, const hpsdr_config_t *config)
{
    if (ctx == NULL || config == NULL) {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->transport = config->transport;
    ctx->device = config->device;
    ctx->callbacks = config->callbacks;

    return net_datagram_open(&ctx->transport, HPSDR_PORT) == 0;
}

static void notify_state(hpsdr_ctx_t *ctx)
{
    if (ctx->callbacks.on_state_changed != NULL) {
        ctx->callbacks.on_state_changed(ctx->callbacks.user,
                                        ctx->running,
                                        ctx->tx_enabled);
    }
}

void hpsdr_start(hpsdr_ctx_t *ctx)
{
    if (!ctx->running) {
        ctx->sequence_number = 0;
    }

    ctx->running = true;
    ctx->tx_enabled = false;
    notify_state(ctx);

    printf("hpsdr_start: running=%d\n", ctx->running);
}

void hpsdr_stop(hpsdr_ctx_t *ctx)
{
    ctx->running = false;
    ctx->tx_enabled = false;
    ctx->sequence_number = 0;
    notify_state(ctx);

    printf("hpsdr_stop: running=%d\n", ctx->running);
} 

void hpsdr_send_discovery_reply(hpsdr_ctx_t *ctx)
{
    uint8_t *reply = ctx->discovery_reply;

    memset(reply, 0, 60);

    reply[0x00] = 0xEF;
    reply[0x01] = 0xFE;
    reply[0x02] = ctx->running ? 0x03 : 0x02;

    memcpy(&reply[0x03], ctx->device.mac, sizeof(ctx->device.mac));

    reply[0x09] = ctx->device.gateware_major;
    reply[0x0A] = HERMESLITE;
    reply[0x13] = ctx->device.receiver_count;
    reply[0x15] = ctx->device.gateware_minor;

    int32_t ret = net_datagram_send(
        &ctx->transport,
        reply,
        60,
        &ctx->remote
    );

    printf("Discovery sent: ret=%d version=%02X.%02X\n",
           ret, reply[0x09], reply[0x15]);
}

void hpsdr_poll(hpsdr_ctx_t *ctx)
{
    net_endpoint_t source = {0};
    int32_t received = net_datagram_receive(
        &ctx->transport,
        ctx->rx_buffer,
        sizeof(ctx->rx_buffer),
        &source
    );

    if (received > 3) {
        ctx->remote = source;
        hpsdr_handle_packet(ctx, ctx->rx_buffer, (uint16_t)received);
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
    if (len >= 12u) {
        bool current_mox = ((buffer[11] & 0x01u) != 0u);
        ctx->tx_enabled = current_mox;

        if (current_mox != ctx->previous_mox) {
             printf("mox_edge_pkt state=%lu spi_dbg_window=%lu c0=%02x %02x %02x %02x %02x c1=%02x %02x %02x %02x %02x\n",
                   (unsigned long)(current_mox ? 1u : 0u),
                 0ul,
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
            ctx->previous_mox = current_mox;
            notify_state(ctx);
        }
    }

    if (len >= 528u) {
        hpsdr_handle_command(ctx, 11, buffer);
        hpsdr_handle_command(ctx, 523, buffer);
    }

    if (len >= HPSDR_PKT_SIZE && ctx->tx_enabled &&
        ctx->callbacks.on_tx_iq_words != NULL) {
        size_t word_count = 0u;
        if (hpsdr_extract_tx_words_from_data_packet(
                buffer,
                len,
                ctx->tx_iq_words,
                HPSDR_IQ_WORDS_PER_PACKET,
                &word_count)) {
            ctx->callbacks.on_tx_iq_words(ctx->callbacks.user,
                                          ctx->tx_iq_words,
                                          word_count);
        }
    }
}

static void hpsdr_handle_command(hpsdr_ctx_t *ctx,
                                 int base_index,
                                 const uint8_t *buffer)
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

        if (command == 0u &&
            ctx->callbacks.on_receiver_count_changed != NULL) {
            uint8_t receiver_count =
                (uint8_t)(((command_data >> 3u) & 0x07u) + 1u);
            if (receiver_count > ctx->device.receiver_count) {
                receiver_count = ctx->device.receiver_count;
            }
            ctx->callbacks.on_receiver_count_changed(ctx->callbacks.user,
                                                     receiver_count);
        }

        if (ctx->callbacks.on_command != NULL) {
            ctx->callbacks.on_command(ctx->callbacks.user,
                                      command,
                                      command_data);
        }
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

bool hpsdr_iq_assembler_init(hpsdr_iq_assembler_t *a,
                             uint8_t receiver_count)
{
    if (a == NULL || receiver_count == 0u ||
        receiver_count > HPSDR_MAX_RECEIVERS) {
        return false;
    }

    memset(a, 0, sizeof(*a));
    a->receiver_count = receiver_count;
    a->groups_per_frame = (uint8_t)(HPSDR_FRAME_PAYLOAD_BYTES /
        ((uint16_t)receiver_count * HPSDR_RECEIVER_BYTES + HPSDR_AUDIO_BYTES));
    return a->groups_per_frame != 0u;
}

bool hpsdr_process_rx_dma_word(
    hpsdr_iq_assembler_t *a,
    hpsdr_tx_packet_t *pkt,
    uint32_t raw)
{
    if (a == NULL || pkt == NULL || a->receiver_count == 0u) {
        return false;
    }

    uint8_t tag = (raw >> 24) & 0x0F;
    uint32_t value = raw & 0x00FFFFFF;
    uint8_t receiver = tag >> 1u;

    /* Match the Linux driver: lock on meta 0, then require 0..2*nrx-1. */
    if (!a->meta_synced) {
        if (tag != 0u) {
            return false;
        }
        a->meta_synced = true;
        a->expected_tag = 0u;
    }
    if (tag != a->expected_tag || receiver >= a->receiver_count) {
        a->meta_synced = false;
        a->have_i = false;
        return false;
    }

    if ((tag & 1u) == 0u) {
        /* Even tags are I; tags 0/1 belong to RX0, 2/3 to RX1, etc. */
        a->i_sample = value;
        a->i_receiver = receiver;
        a->have_i = true;
        a->expected_tag = (uint8_t)(tag + 1u);
        return false;
    }

    if (!a->have_i || receiver != a->i_receiver || tag != a->expected_tag) {
        a->meta_synced = false;
        a->have_i = false;
        return false;
    }

    uint16_t frame = a->group_count / a->groups_per_frame;
    uint16_t group = a->group_count % a->groups_per_frame;
    uint16_t stride = (uint16_t)a->receiver_count * HPSDR_RECEIVER_BYTES +
                      HPSDR_AUDIO_BYTES;
    uint16_t index = HPSDR_FRAME_PAYLOAD_OFFSET +
                     frame * HPSDR_FRAME_SIZE_BYTES + group * stride +
                     receiver * HPSDR_RECEIVER_BYTES;

    write_24be(&pkt->data[index],     a->i_sample);
    write_24be(&pkt->data[index + 3], value);

    a->have_i = false;
    a->expected_tag = (uint8_t)((tag + 1u) % (a->receiver_count * 2u));

    if (receiver == (uint8_t)(a->receiver_count - 1u)) {
        a->group_count++;
        if (a->group_count == (uint16_t)a->groups_per_frame * 2u) {
            a->group_count = 0u;
            return true; /* Complete HPSDR packet with all receivers. */
        }
    }

    return false;
}

void hpsdr_prepare_tx_packet(hpsdr_ctx_t *ctx, hpsdr_tx_packet_t *pkt)
{
    uint32_t seq = ctx->sequence_number++;

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
    uint8_t first_group[HPSDR_TX_GROUP_STRIDE_BYTES] = {0};
    bool first_sample_set = false;
    bool saw_nonzero = false;

    for (size_t frame = 0; frame < 2u; frame++) {
        size_t frame_base = frame * HPSDR_FRAME_SIZE_BYTES;

        for (size_t group = 0; group < HPSDR_TX_GROUPS_PER_FRAME; group++) {
            size_t index = HPSDR_FRAME_PAYLOAD_OFFSET +
                           frame_base +
                           (group * HPSDR_TX_GROUP_STRIDE_BYTES);

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
                memcpy(first_group, &buffer[index], HPSDR_TX_GROUP_STRIDE_BYTES);
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
