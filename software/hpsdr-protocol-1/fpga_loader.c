#include "fpga_loader.h"

#include <stdbool.h>
#include <stdio.h>

#include "pico/stdlib.h"

/*
 * Update these GPIO numbers to match the RP2350 wiring to the FPGA.
 */
#define FPGA_PIN_CONF_DONE 22  
#define FPGA_PIN_NSTATUS 26
#define FPGA_PIN_NCONFIG 27
#define FPGA_PIN_DATA 13
#define FPGA_PIN_DCLK 24


#define FPGA_RESET_HOLD_US 1000
#define FPGA_STATUS_POLL_DELAY_US 10000
#define FPGA_STATUS_POLL_LIMIT 255
#define FPGA_CLOCK_DELAY_US 1
#define FPGA_FINAL_CLOCK_COUNT 2

static inline void fpga_loader_delay(void) {
    sleep_us(FPGA_CLOCK_DELAY_US);
}

static inline void fpga_loader_clock_pulse(void) {
    gpio_put(FPGA_PIN_DCLK, 1);
    fpga_loader_delay();
    gpio_put(FPGA_PIN_DCLK, 0);
    fpga_loader_delay();
}

static void fpga_loader_program_byte(uint8_t value) {
    for (uint bit_index = 0; bit_index < 8; ++bit_index) {
        const bool bit = ((value >> bit_index) & 0x1u) != 0;
        gpio_put(FPGA_PIN_DATA, bit);
        fpga_loader_delay();
        fpga_loader_clock_pulse();
    }
}

static bool fpga_loader_prepare(void) {
    gpio_put(FPGA_PIN_NCONFIG, 0);
    gpio_put(FPGA_PIN_DATA, 0);
    gpio_put(FPGA_PIN_DCLK, 0);
    sleep_us(FPGA_RESET_HOLD_US);
    gpio_put(FPGA_PIN_NCONFIG, 1);

    for (uint attempt = 0; attempt < FPGA_STATUS_POLL_LIMIT; ++attempt) {
        if (gpio_get(FPGA_PIN_NSTATUS)) {
            return true;
        }
        sleep_us(FPGA_STATUS_POLL_DELAY_US);
    }

    return false;
}

void fpga_loader_init(void) {
    gpio_init(FPGA_PIN_CONF_DONE);
    gpio_set_dir(FPGA_PIN_CONF_DONE, GPIO_IN);
    gpio_disable_pulls(FPGA_PIN_CONF_DONE);

    gpio_init(FPGA_PIN_NSTATUS);
    gpio_set_dir(FPGA_PIN_NSTATUS, GPIO_IN);
    gpio_disable_pulls(FPGA_PIN_NSTATUS);

    gpio_init(FPGA_PIN_NCONFIG);
    gpio_set_dir(FPGA_PIN_NCONFIG, GPIO_OUT);
    gpio_put(FPGA_PIN_NCONFIG, 1);

    gpio_init(FPGA_PIN_DATA);
    gpio_set_dir(FPGA_PIN_DATA, GPIO_OUT);
    gpio_put(FPGA_PIN_DATA, 0);

    gpio_init(FPGA_PIN_DCLK);
    gpio_set_dir(FPGA_PIN_DCLK, GPIO_OUT);
    gpio_put(FPGA_PIN_DCLK, 0);
}

fpga_loader_result_t fpga_loader_program(const uint8_t *bitstream, size_t bitstream_size) {
    if (!fpga_loader_prepare()) {
        printf("Error: FPGA prepare failed, nSTATUS stayed low.\n");
        return FPGA_LOADER_ERROR_PREPARE;
    }

    for (size_t index = 0; index < bitstream_size; ++index) {
        fpga_loader_program_byte(bitstream[index]);
    }

    if (!gpio_get(FPGA_PIN_NSTATUS)) {
        printf("Error: FPGA programming failed, nSTATUS is low.\n");
        return FPGA_LOADER_ERROR_NSTATUS;
    }

    if (!gpio_get(FPGA_PIN_CONF_DONE)) {
        printf("Error: FPGA programming failed, CONF_DONE is low.\n");
        return FPGA_LOADER_ERROR_CONF_DONE;
    }

    for (uint clock_count = 0; clock_count < FPGA_FINAL_CLOCK_COUNT; ++clock_count) {
        fpga_loader_clock_pulse();
    }

    printf("Info: FPGA programming succeeded.\n");
    return FPGA_LOADER_OK;
}


// end of source file fpga_loader.c