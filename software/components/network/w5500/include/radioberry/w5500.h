/*
 * w5500_udp.h
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <radioberry/net_datagram.h>

typedef struct {
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t subnet[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    bool use_dhcp;
} w5500_network_config_t;

typedef struct {
    uint8_t socket;
} w5500_udp_t;

bool w5500_network_init(const w5500_network_config_t *config);

void w5500_udp_transport_init(w5500_udp_t *udp,
                              uint8_t socket,
                              net_datagram_t *transport);

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
