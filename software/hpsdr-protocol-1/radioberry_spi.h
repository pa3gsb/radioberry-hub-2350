#ifndef RADIOBERRY_SPI_H
#define RADIOBERRY_SPI_H    

#include <stdint.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

extern uint8_t radioberry_spi_tx[6];
extern uint8_t radioberry_spi_rx[6];

void radioberry_spi_init(void);
int  radioberry_spi_write_read_blocking(const uint8_t *src, uint8_t *dst, size_t len);

#endif

//  end of header file radioberry_spi.h 