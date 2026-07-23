#ifndef HPSDR_PROTOCOL_1_H
#define HPSDR_PROTOCOL_1_H

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include <hardware/pio.h>
#include "port_common.h"
#include "wizchip_conf.h"
#include "wizchip_spi.h"
#include "socket.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "radioberry.h"
#include "stream_rx_dma.h"

#define HPSDR_PORT          1024
#define HPSDR_PKT_SIZE      1032
#define HPSDR_REPLY_SIZE    60
#define HPSDR_IQ_PAIRS_PER_PACKET 126u
#define HPSDR_IQ_WORDS_PER_PACKET HPSDR_IQ_PAIRS_PER_PACKET

typedef struct {
    uint8_t ip[4];
    uint16_t port;
} hpsdr_remote_t;

typedef struct {

    rb2_ctx_t rb2_device;  /* Radioberry device context */
    
    uint8_t udp_sock;

    hpsdr_remote_t remote;
    hpsdr_remote_t stream_remote;

    bool running;

    uint32_t last_sequence_number;

    uint8_t rx_buffer[HPSDR_PKT_SIZE];
    uint8_t tx_buffer[HPSDR_PKT_SIZE];
    uint8_t discovery_reply[HPSDR_REPLY_SIZE];

} hpsdr_ctx_t;

void hpsdr_init(hpsdr_ctx_t *ctx);
void hpsdr_poll(hpsdr_ctx_t *ctx);
void hpsdr_handle_packet(hpsdr_ctx_t *ctx, uint8_t *buffer, uint16_t len);
void hpsdr_process_packet(hpsdr_ctx_t *ctx, const uint8_t *buffer, uint16_t len);
void hpsdr_handle_command(rb2_ctx_t *ctx, int base_index, const uint8_t *buffer);
void hpsdr_send_packet(hpsdr_ctx_t *ctx, PIO pio, uint sm);
void hpsdr_fill_packet_from_pio(hpsdr_tx_packet_t *pkt, PIO pio, uint sm);
bool hpsdr_fill_packet_from_dma(hpsdr_tx_packet_t *pkt, const uint32_t *samples);
bool process_dma_word(iq_assembler_t *a, hpsdr_tx_packet_t *pkt,uint32_t raw);
void hpsdr_prepare_tx_packet(hpsdr_tx_packet_t *pkt);

bool hpsdr_extract_tx_words_from_data_packet(const uint8_t *buffer,
                                             size_t len,
                                             uint32_t *out_words,
                                             size_t out_word_capacity,
                                             size_t *out_word_count);

#endif 

// end of header file radioberry_protocol_1.h