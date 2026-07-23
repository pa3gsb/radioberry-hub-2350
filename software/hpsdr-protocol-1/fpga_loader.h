#ifndef FPGA_LOADER_H
#define FPGA_LOADER_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    FPGA_LOADER_OK = 0,
    FPGA_LOADER_ERROR_PREPARE = -1,
    FPGA_LOADER_ERROR_NSTATUS = -2,
    FPGA_LOADER_ERROR_CONF_DONE = -3
} fpga_loader_result_t;

void fpga_loader_init(void);
fpga_loader_result_t fpga_loader_program(const uint8_t *bitstream, size_t bitstream_size);

#endif

// end of header file fpga_loader.h