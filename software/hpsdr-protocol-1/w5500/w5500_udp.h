/*
 * w5500_udp.h
 */

#pragma once

#include <stdint.h>

void w5500_init(void);

int w5500_udp_open(uint8_t sock, uint16_t port);

uint16_t w5500_udp_recvfrom(
    uint8_t sock,
    uint8_t *buffer,
    uint16_t max_len,
    uint8_t remote_ip[4],
    uint16_t *remote_port
);

int w5500_udp_sendto(
    uint8_t sock,
    const uint8_t *buffer,
    uint16_t len,
    const uint8_t remote_ip[4],
    uint16_t remote_port
);