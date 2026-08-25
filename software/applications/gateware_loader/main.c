#include <stdio.h>

#include "pico/stdlib.h"

#include <radioberry/gateware.h>
#include "gateware_pico.h"
#include "radioberry_rbf.h"

int main(void)
{
    stdio_init_all();
    sleep_ms(100u);

    fpga_loader_t loader;
    const fpga_loader_io_t io = fpga_loader_pico_io();
    if (!fpga_loader_init(&loader, &io)) {
        panic("FPGA loader initialization failed");
    }

    printf("Loading %s (%u bytes)\n",
           radioberry_rbf_name,
           (unsigned)radioberry_rbf_size);
    const fpga_loader_result_t result = fpga_loader_program(
        &loader, radioberry_rbf_data, radioberry_rbf_size);
    printf("Gateware result: %d\n", (int)result);

    while (true) {
        tight_loop_contents();
    }
}
