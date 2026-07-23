#include "w5500_udp.h"

#include "wizchip_conf.h"
#include "socket.h"


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
    ret = sendto(sock, buffer, len, remote_ip, remote_port);

    if (ret < 0) {
        return -1;
    }

    return 0;
}

// end of source file w5500_udp.c