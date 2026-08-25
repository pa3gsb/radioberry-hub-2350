#include <radioberry/w5500.h>

#include "wizchip_conf.h"
#include "wizchip_spi.h"
#include "socket.h"

#include <limits.h>
#include <string.h>

bool w5500_network_init(const w5500_network_config_t *config)
{
    if (config == NULL) {
        return false;
    }

    wiz_NetInfo net_info = {0};
    memcpy(net_info.mac, config->mac, sizeof(net_info.mac));
    memcpy(net_info.ip, config->ip, sizeof(net_info.ip));
    memcpy(net_info.sn, config->subnet, sizeof(net_info.sn));
    memcpy(net_info.gw, config->gateway, sizeof(net_info.gw));
    memcpy(net_info.dns, config->dns, sizeof(net_info.dns));
    net_info.dhcp = config->use_dhcp ? NETINFO_DHCP : NETINFO_STATIC;

    wizchip_spi_initialize();
    wizchip_cris_initialize();
    wizchip_reset();
    wizchip_initialize();
    wizchip_check();
    network_initialize(net_info);
    print_network_information(net_info);
    return true;
}


int w5500_udp_open(uint8_t sock, uint16_t port)
{
    int8_t ret;

    close(sock);

    ret = socket(sock, Sn_MR_UDP, port, 0);

    if (ret != sock) {
        return -1;
    }

    return 0;
}

uint16_t w5500_udp_recvfrom(
    uint8_t sock,
    uint8_t *buffer,
    uint16_t max_len,
    uint8_t remote_ip[4],
    uint16_t *remote_port
) {
    int32_t ret;
    ret = recvfrom(sock, buffer, max_len, remote_ip, remote_port);

    if (ret < 0) {
        return 0;
    }

    return (uint16_t)ret;
}   

int w5500_udp_sendto(
    uint8_t sock,
    const uint8_t *buffer,
    uint16_t len,
    const uint8_t remote_ip[4],
    uint16_t remote_port
) {
    int32_t ret;
    /* ioLibrary's API is not const-correct; sendto does not modify these. */
    ret = sendto(sock,
                 (uint8_t *)buffer,
                 len,
                 (uint8_t *)remote_ip,
                 remote_port);

    if (ret < 0) {
        return -1;
    }

    return 0;
}

static int transport_open(void *context, uint16_t local_port)
{
    w5500_udp_t *udp = context;
    return w5500_udp_open(udp->socket, local_port);
}

static int32_t transport_receive(void *context,
                                 uint8_t *buffer,
                                 size_t capacity,
                                 net_endpoint_t *source)
{
    w5500_udp_t *udp = context;
    if (udp == NULL || buffer == NULL || source == NULL) {
        return -1;
    }
    uint16_t available = getSn_RX_RSR(udp->socket);
    if (available == 0u) {
        return 0;
    }
    if (capacity > UINT16_MAX) {
        capacity = UINT16_MAX;
    }
    if (available > capacity) {
        available = (uint16_t)capacity;
    }

    return (int32_t)w5500_udp_recvfrom(udp->socket,
                                       buffer,
                                       available,
                                       source->ip,
                                       &source->port);
}

static int32_t transport_send(void *context,
                              const uint8_t *buffer,
                              size_t length,
                              const net_endpoint_t *destination)
{
    w5500_udp_t *udp = context;
    if (udp == NULL || buffer == NULL || destination == NULL ||
        length > UINT16_MAX) {
        return -1;
    }
    return w5500_udp_sendto(udp->socket,
                            buffer,
                            (uint16_t)length,
                            destination->ip,
                            destination->port);
}

void w5500_udp_transport_init(w5500_udp_t *udp,
                              uint8_t socket,
                              net_datagram_t *transport)
{
    if (udp == NULL || transport == NULL) {
        return;
    }

    udp->socket = socket;
    transport->context = udp;
    transport->open = transport_open;
    transport->receive = transport_receive;
    transport->send = transport_send;
}

// end of source file w5500_udp.c
