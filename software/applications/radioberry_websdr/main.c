#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "pico/unique_id.h"
#include <radioberry/w5500.h>
#include "wizchip_conf.h"
#include "websdr_http.h"
#include "websdr_radio.h"

#include <radioberry/gateware.h>

#include "gateware_pico.h"
#include "radioberry_rbf.h"

static fpga_loader_t gateware_loader;
static rb_web_service service;

static void load_gateware(void)
{
    printf("Loading FPGA gateware: %s (%u bytes)\n",
           radioberry_rbf_name,
           (unsigned)radioberry_rbf_size);

    const fpga_loader_io_t io = fpga_loader_pico_io();
    if (!fpga_loader_init(&gateware_loader, &io)) {
        panic("Failed to initialize FPGA loader");
    }

    const fpga_loader_result_t result = fpga_loader_program(
        &gateware_loader,
        radioberry_rbf_data,
        radioberry_rbf_size);
    if (result != FPGA_LOADER_OK) {
        panic("Failed to load FPGA gateware: %d", (int)result);
    }

    printf("FPGA gateware loaded\n");
}

int main(void)
{
    stdio_init_all();
    sleep_ms(100u);

    printf("RadioBerry WebSDR starting\n");

    load_gateware();
    sleep_ms(2000u);
    rb_web_radio_start();

    w5500_network_config_t network = {
        .mac = {2, 0, 0, 0, 0, 0}, .ip = {192, 168, 2, 43},
        .subnet = {255, 255, 255, 0}, .use_dhcp = true
    };
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    for (unsigned i = 1; i < 6; ++i) network.mac[i] = board_id.id[i + 2];
    if (!w5500_network_init(&network)) panic("Failed to initialize Ethernet");
    uint32_t now = to_ms_since_boot(get_absolute_time());
    rb_web_init_live(&service, get_rand_32(), rb_web_radio_tune, NULL);
    service.gain = rb_web_radio_gain;
    service.agc = rb_web_radio_agc;
    uint32_t next_report = now;

    while (true) {
        now = to_ms_since_boot(get_absolute_time());
        w5500_network_poll();
        rb_web_radio_poll(&service);
        rb_web_http_poll(&service, now);
        if ((int32_t)(now - next_report) >= 0) {
            wiz_NetInfo info;
            ctlnetwork(CN_GET_NETINFO, &info);
            printf("WebSDR live RX: http://%u.%u.%u.%u/ IQ=%lu FFT=%lu stalls=%lu tags=%lu DSPmax=%lu us\n",
                info.ip[0], info.ip[1], info.ip[2], info.ip[3],
                (unsigned long)service.diagnostics.rx_pairs, (unsigned long)service.diagnostics.fft_windows,
                (unsigned long)service.diagnostics.rx_stalls, (unsigned long)service.diagnostics.tag_errors,
                (unsigned long)service.diagnostics.dsp_us_max);
            next_report = now + 10000;
        }
        tight_loop_contents();
    }
}
