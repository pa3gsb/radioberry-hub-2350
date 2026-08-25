#ifndef HPSDR_PROTOCOL_1_H
#define HPSDR_PROTOCOL_1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <radioberry/net_datagram.h>

#define HPSDR_PORT                    1024u
#define HPSDR_PKT_SIZE                1032u
#define HPSDR_REPLY_SIZE              60u
#define HPSDR_COMMAND_COUNT           256u
#define HPSDR_IQ_PAIRS_PER_PACKET     126u
#define HPSDR_IQ_WORDS_PER_PACKET     HPSDR_IQ_PAIRS_PER_PACKET

typedef struct {
    uint8_t data[HPSDR_PKT_SIZE];
} hpsdr_tx_packet_t;

typedef struct {
    bool have_i;
    uint32_t i_sample;
    uint16_t pair_count;
} hpsdr_iq_assembler_t;

typedef struct {
    uint8_t mac[6];
    uint8_t gateware_major;
    uint8_t gateware_minor;
    uint8_t receiver_count;
} hpsdr_device_info_t;

typedef struct {
    void *user;
    void (*on_state_changed)(void *user, bool running, bool tx_enabled);
    void (*on_command)(void *user, uint8_t command, uint32_t data);
    void (*on_tx_iq_words)(void *user,
                           const uint32_t *words,
                           size_t word_count);
} hpsdr_callbacks_t;

typedef struct {
    net_datagram_t transport;
    hpsdr_device_info_t device;
    hpsdr_callbacks_t callbacks;
} hpsdr_config_t;

typedef struct {
    net_datagram_t transport;
    hpsdr_device_info_t device;
    hpsdr_callbacks_t callbacks;
    net_endpoint_t remote;
    net_endpoint_t stream_remote;
    bool running;
    bool tx_enabled;
    bool previous_mox;
    volatile uint32_t sequence_number;
    uint32_t commands[HPSDR_COMMAND_COUNT];
    uint8_t rx_buffer[HPSDR_PKT_SIZE];
    uint8_t tx_buffer[HPSDR_PKT_SIZE];
    uint8_t discovery_reply[HPSDR_REPLY_SIZE];
    uint32_t tx_iq_words[HPSDR_IQ_WORDS_PER_PACKET];
} hpsdr_ctx_t;

bool hpsdr_init(hpsdr_ctx_t *ctx, const hpsdr_config_t *config);
void hpsdr_poll(hpsdr_ctx_t *ctx);
void hpsdr_handle_packet(hpsdr_ctx_t *ctx, uint8_t *buffer, uint16_t len);
void hpsdr_process_packet(hpsdr_ctx_t *ctx,
                          const uint8_t *buffer,
                          uint16_t len);
void hpsdr_prepare_tx_packet(hpsdr_ctx_t *ctx, hpsdr_tx_packet_t *packet);
bool hpsdr_process_rx_dma_word(hpsdr_iq_assembler_t *assembler,
                               hpsdr_tx_packet_t *packet,
                               uint32_t raw);
bool hpsdr_extract_tx_words_from_data_packet(const uint8_t *buffer,
                                             size_t len,
                                             uint32_t *out_words,
                                             size_t out_word_capacity,
                                             size_t *out_word_count);

#endif /* HPSDR_PROTOCOL_1_H */
