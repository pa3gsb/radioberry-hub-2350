#ifndef RADIOBERRY_H
#define RADIOBERRY_H    

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"
#include <hardware/pio.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include "fpga_loader.h"
#include "radioberry_rbf.h"
#include "radioberry_spi.h"

#include "rb-rx.pio.h"
#include "rb-tx.pio.h"

#include "w5500_udp.h"

#include "stream_rx_dma.h"
#include "stream_tx_dma.h"

#define RB_CMD_COUNT 256

#define HPSDR_PKT_SIZE      1032

extern queue_t command_queue;
extern volatile uint32_t spi_debug_window_shared;
extern volatile bool hpsdr_running_shared;
extern volatile bool hpsdr_tx_enabled_shared;
extern volatile uint32_t hpsdr_sequence_shared;

void radioberry_handle_tx_data_packet(const uint8_t *buffer, size_t len);
    
typedef struct {
    uint8_t data[HPSDR_PKT_SIZE];
} hpsdr_tx_packet_t;

typedef struct {
    uint8_t command;
    uint32_t data;
} rb_cmd_t;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t fpga;
    uint8_t nr;
    uint8_t nt;
} rb_gateware_t;

typedef struct {
    rb_gateware_t gateware;

    uint32_t commands[RB_CMD_COUNT];

} rb2_ctx_t;


#endif