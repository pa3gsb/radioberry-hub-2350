#ifndef RADIOBERRY_PROTOCOL1_W5500_APP_H
#define RADIOBERRY_PROTOCOL1_W5500_APP_H

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"
#include <hardware/pio.h>

#include "hardware/clocks.h"
#include "hardware/vreg.h"

#include <radioberry/gateware.h>
#include "gateware_pico.h"
#include "radioberry_rbf.h"
#include <radioberry/radio.h>


#define RB_CMD_COUNT 256

extern queue_t command_queue;
extern volatile uint32_t spi_debug_window_shared;

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
