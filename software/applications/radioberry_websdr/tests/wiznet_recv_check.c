/* Exercise the actual ioLibrary socket.c, with only register/buffer IO modelled.
 * The HTTP transport mock alone could not catch its IPv4 nonblocking recv bug. */
#include "socket.h"
#include <string.h>
#include <stdio.h>

static uint8_t registers[32][256];
static const uint8_t *incoming[8];
static uint16_t available[8];
static unsigned reads;
uint8_t WIZCHIP_READ(uint32_t address) {
    return registers[(address >> 3) & 31][(address >> 8) & 255];
}
void WIZCHIP_WRITE(uint32_t address, uint8_t value) {
    unsigned block = (address >> 3) & 31, offset = (address >> 8) & 255;
    if (block % 4 == 1 && offset == 1) {
        if (value == Sn_CR_OPEN) registers[block][3] = SOCK_INIT;
        if (value == Sn_CR_CLOSE) registers[block][3] = SOCK_CLOSED;
        if (value == Sn_CR_LISTEN) registers[block][3] = SOCK_LISTEN;
        registers[block][offset] = 0; /* Hardware consumes commands. */
    } else if (block % 4 == 1 && offset == 2) registers[block][offset] &= ~value;
    else registers[block][offset] = value;
}
void WIZCHIP_READ_BUF(uint32_t address, uint8_t *data, uint16_t size) {
    for (unsigned i = 0; i < size; ++i) data[i] = WIZCHIP_READ(address + (i << 8));
}
void WIZCHIP_WRITE_BUF(uint32_t address, uint8_t *data, uint16_t size) {
    for (unsigned i = 0; i < size; ++i) WIZCHIP_WRITE(address + (i << 8), data[i]);
}
uint16_t getSn_RX_RSR(uint8_t sn) { return available[sn]; }
uint16_t getSn_TX_FSR(uint8_t sn) { (void)sn; return 4096; }
void wiz_recv_data(uint8_t sn, uint8_t *data, uint16_t size) {
    memcpy(data, incoming[sn], size); incoming[sn] += size; available[sn] -= size; ++reads;
}
void wiz_send_data(uint8_t sn, uint8_t *data, uint16_t size) { (void)sn; (void)data; (void)size; }
void wiz_recv_ignore(uint8_t sn, uint16_t size) { incoming[sn] += size; available[sn] -= size; }

int rb_test_wiznet_recv(void) {
    const char request[] = "GET /api/status HTTP/1.1\r\n\r\n";
    uint8_t buffer[128];
    memset(registers, 0, sizeof(registers));
    memset(available, 0, sizeof(available)); reads = 0;
    uint8_t ip[] = {192, 168, 2, 31};
    setSIPR(ip);
    for (unsigned sn = 0; sn <= 2; sn += 2) {
        setSn_RXBUF_SIZE(sn, 4); setSn_TXBUF_SIZE(sn, 4);
        if (socket(sn, Sn_MR_TCP, 80, SF_IO_NONBLOCK | SF_TCP_NODELAY) != (int)sn) return 1;
        WIZCHIP_WRITE(Sn_SR(sn), SOCK_ESTABLISHED);
        if (recv(sn, buffer, sizeof(buffer)) != SOCK_BUSY) return 2;
        incoming[sn] = (const uint8_t *)request; available[sn] = sizeof(request) - 1;
        if (recv(sn, buffer, 5) != 5 || memcmp(buffer, request, 5)) return 3;
        if (recv(sn, buffer, sizeof(buffer)) != (int)sizeof(request) - 6 ||
            memcmp(buffer, request + 5, sizeof(request) - 6)) return 4;
        if (available[sn] || recv(sn, buffer, sizeof(buffer)) != SOCK_BUSY) return 5;
        incoming[sn] = (const uint8_t *)request; available[sn] = 5;
        WIZCHIP_WRITE(Sn_SR(sn), SOCK_CLOSE_WAIT);
        if (recv(sn, buffer, sizeof(buffer)) != 5 || memcmp(buffer, request, 5)) return 6;
        if (recv(sn, buffer, sizeof(buffer)) != SOCKERR_SOCKSTATUS || getSn_SR(sn) != SOCK_CLOSED) return 7;
    }
    if (reads != 6) return 8;
    puts("Actual WIZnet IPv4 recv: empty/buffered/partial/drained/half-closed nonblocking sockets passed.");
    return 0;
}
