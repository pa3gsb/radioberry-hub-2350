#include "websdr_radio_config.h"
#include <stddef.h>

bool rb_web_radio_apply_gain(rb_web_radio_send_fn send, void *context, int32_t gain_db)
{
    if (!send || gain_db < RB_WEB_RX_GAIN_MIN_DB || gain_db > RB_WEB_RX_GAIN_MAX_DB)
        return false;
    const uint32_t word = (RB_WEB_RX_GAIN_WORD & ~0x7fu) |
        0x40u | (uint32_t)(gain_db + 12);
    return send(context, 0x14u, word);
}

bool rb_web_radio_configure(rb_web_radio_send_fn send, void *context, uint32_t frequency)
{
    static const struct { uint8_t command; uint32_t data; } initial[] = {
        {0x12u, 0x70000000u},
        {0x14u, RB_WEB_RX_GAIN_WORD},
        {0x16u, 0x00005032u},
        {0x1cu, 0x0000c000u},
        {0x1eu, 0x00321e00u},
        {0x20u, 0x7d003200u},
        {0x22u, 0x19006400u},
        {0x2eu, 0x00001428u},
        {0xfau, 0x07c10000u},
        {0x00u, RB_WEB_RX_CONTROL_WORD},
        {0x00u, RB_WEB_RX_CONTROL_WORD},
    };
    if (!send) return false;
    for (size_t i = 0; i < sizeof(initial) / sizeof(initial[0]); ++i)
        if (!send(context, initial[i].command, initial[i].data)) return false;
    return send(context, 0x02u, frequency) && send(context, 0x04u, frequency);
}
