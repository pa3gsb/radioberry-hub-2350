#ifndef RADIOBERRY_NET_DATAGRAM_H
#define RADIOBERRY_NET_DATAGRAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t ip[4];
    uint16_t port;
} net_endpoint_t;

typedef struct {
    void *context;
    int (*open)(void *context, uint16_t local_port);
    int32_t (*receive)(void *context,
                       uint8_t *buffer,
                       size_t capacity,
                       net_endpoint_t *source);
    int32_t (*send)(void *context,
                    const uint8_t *buffer,
                    size_t length,
                    const net_endpoint_t *destination);
} net_datagram_t;

static inline int net_datagram_open(const net_datagram_t *transport,
                                    uint16_t local_port)
{
    if (transport == NULL || transport->open == NULL) {
        return -1;
    }
    return transport->open(transport->context, local_port);
}

static inline int32_t net_datagram_receive(const net_datagram_t *transport,
                                           uint8_t *buffer,
                                           size_t capacity,
                                           net_endpoint_t *source)
{
    if (transport == NULL || transport->receive == NULL) {
        return -1;
    }
    return transport->receive(transport->context, buffer, capacity, source);
}

static inline int32_t net_datagram_send(const net_datagram_t *transport,
                                        const uint8_t *buffer,
                                        size_t length,
                                        const net_endpoint_t *destination)
{
    if (transport == NULL || transport->send == NULL) {
        return -1;
    }
    return transport->send(transport->context, buffer, length, destination);
}

#endif /* RADIOBERRY_NET_DATAGRAM_H */
