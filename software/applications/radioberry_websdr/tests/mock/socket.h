/* Host-only model of deferred SENDOK and W5500 socket states. */
#ifndef RB_TEST_SOCKET_H
#define RB_TEST_SOCKET_H
#include <stdint.h>
#define SOCK_CLOSED 0x00
#define SOCK_INIT 0x13
#define SOCK_LISTEN 0x14
#define SOCK_ESTABLISHED 0x17
#define SOCK_CLOSE_WAIT 0x1c
#define Sn_MR_TCP 1
#define SF_IO_NONBLOCK 1
#define SF_TCP_NODELAY 2
#define Sn_IR_TIMEOUT 8
#define Sn_IR_SENDOK 16
uint8_t getSn_SR(uint8_t sn);
uint8_t getSn_IR(uint8_t sn);
uint16_t getSn_RX_RSR(uint8_t sn);
uint16_t getSn_MSSR(uint8_t sn);
int8_t socket(uint8_t sn, uint8_t mode, uint16_t port, uint8_t flags);
int8_t listen(uint8_t sn);
int8_t close(uint8_t sn);
int8_t disconnect(uint8_t sn);
int32_t recv(uint8_t sn, uint8_t *data, uint16_t size);
int32_t send(uint8_t sn, uint8_t *data, uint16_t size);
#endif
