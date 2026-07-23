#include "tusb.h"

#if CFG_TUD_AUDIO > 0

// For microphone-only: TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR uses standard entity IDs
// We define minimal state and callbacks for enumeration to work

#define UAC2_ENTITY_INPUT_TERM_MIC      0x01
#define UAC2_ENTITY_FEATURE_UNIT_MIC    0x02
#define UAC2_ENTITY_OUTPUT_TERM_USB_MIC 0x03
#define UAC2_ENTITY_CLOCK_SOURCE        0x04

#define ITF_AUDIO_CONTROL_MIC           0x00
#define ITF_AUDIO_CONTROL_SPK           0x02

static bool mute[2];
static int16_t volume[2];
static audio_control_range_2_n_t(1) volume_range[2];
static bool mute_spk[2];
static int16_t volume_spk[2];
static audio_control_range_2_n_t(1) volume_range_spk[2];
static uint32_t sample_rate = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
static uint32_t sample_rate_spk = CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;
    (void)p_request;
    mute[0] = false;
    mute[1] = false;
    volume[0] = 0;
    volume[1] = 0;
    volume_range[0].wNumSubRanges = 1;
    volume_range[0].subrange[0].bMin = -90;
    volume_range[0].subrange[0].bMax = 0;
    volume_range[0].subrange[0].bRes = 1;
    volume_range[1] = volume_range[0];

    mute_spk[0] = false;
    mute_spk[1] = false;
    volume_spk[0] = 0;
    volume_spk[1] = 0;
    volume_range_spk[0].wNumSubRanges = 1;
    volume_range_spk[0].subrange[0].bMin = -90;
    volume_range_spk[0].subrange[0].bMax = 0;
    volume_range_spk[0].subrange[0].bRes = 1;
    volume_range_spk[1] = volume_range_spk[0];
    return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void)rhport;
    (void)p_request;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;
    (void)pBuff;
    return false;
}

bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *pBuff) {
    (void)rhport;

    uint8_t channel_num = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    uint8_t itf_num = TU_U16_LOW(p_request->wIndex);

    if (entity_id == UAC2_ENTITY_FEATURE_UNIT_MIC && p_request->bRequest == AUDIO_CS_REQ_CUR) {
        if (channel_num > 1) return false;

        if (ctrl_sel == AUDIO_FU_CTRL_MUTE && p_request->wLength == sizeof(audio_control_cur_1_t)) {
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                mute[channel_num] = ((audio_control_cur_1_t*) pBuff)->bCur;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                mute_spk[channel_num] = ((audio_control_cur_1_t*) pBuff)->bCur;
            } else {
                return false;
            }
            return true;
        }

        if (ctrl_sel == AUDIO_FU_CTRL_VOLUME && p_request->wLength == sizeof(audio_control_cur_2_t)) {
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                volume[channel_num] = ((audio_control_cur_2_t*) pBuff)->bCur;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                volume_spk[channel_num] = ((audio_control_cur_2_t*) pBuff)->bCur;
            } else {
                return false;
            }
            return true;
        }
    }

    if (entity_id == UAC2_ENTITY_CLOCK_SOURCE && p_request->bRequest == AUDIO_CS_REQ_CUR &&
        ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ && p_request->wLength == sizeof(audio_control_cur_4_t)) {
        if (itf_num == ITF_AUDIO_CONTROL_MIC) {
            sample_rate = ((audio_control_cur_4_t*) pBuff)->bCur;
        } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
            sample_rate_spk = ((audio_control_cur_4_t*) pBuff)->bCur;
        } else {
            return false;
        }
        return true;
    }

    return false;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    uint8_t channel_num = TU_U16_LOW(p_request->wValue);
    uint8_t ctrl_sel = TU_U16_HIGH(p_request->wValue);
    uint8_t entity_id = TU_U16_HIGH(p_request->wIndex);
    uint8_t itf_num = TU_U16_LOW(p_request->wIndex);

    if (entity_id == UAC2_ENTITY_FEATURE_UNIT_MIC) {
        if (channel_num > 1) return false;

        if (ctrl_sel == AUDIO_FU_CTRL_MUTE && p_request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t ret;
            if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                ret.bCur = mute[channel_num] ? 1 : 0;
            } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                ret.bCur = mute_spk[channel_num] ? 1 : 0;
            } else {
                return false;
            }
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
        }

        if (ctrl_sel == AUDIO_FU_CTRL_VOLUME) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_2_t ret;
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    ret.bCur = volume[channel_num];
                } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    ret.bCur = volume_spk[channel_num];
                } else {
                    return false;
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &volume_range[channel_num], sizeof(audio_control_range_2_n_t(1)));
                }
                if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &volume_range_spk[channel_num], sizeof(audio_control_range_2_n_t(1)));
                }
                return false;
            }
        }
    }

    if (entity_id == UAC2_ENTITY_CLOCK_SOURCE) {
        if (ctrl_sel == AUDIO_CS_CTRL_SAM_FREQ) {
            if (p_request->bRequest == AUDIO_CS_REQ_CUR) {
                audio_control_cur_4_t ret;
                if (itf_num == ITF_AUDIO_CONTROL_MIC) {
                    ret.bCur = sample_rate;
                } else if (itf_num == ITF_AUDIO_CONTROL_SPK) {
                    ret.bCur = sample_rate_spk;
                } else {
                    return false;
                }
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
            if (p_request->bRequest == AUDIO_CS_REQ_RANGE) {
                audio_control_range_4_n_t(1) ret;
                uint32_t sr = (itf_num == ITF_AUDIO_CONTROL_SPK) ? CFG_TUD_AUDIO_FUNC_2_SAMPLE_RATE : CFG_TUD_AUDIO_FUNC_1_SAMPLE_RATE;
                ret.wNumSubRanges = 1;
                ret.subrange[0].bMin = sr;
                ret.subrange[0].bMax = sr;
                ret.subrange[0].bRes = 0;
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
            }
        }

        if (ctrl_sel == AUDIO_CS_CTRL_CLK_VALID && p_request->bRequest == AUDIO_CS_REQ_CUR) {
            audio_control_cur_1_t ret = { .bCur = 1 };
            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &ret, sizeof(ret));
        }
    }

    return false;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)n_bytes_received;
    (void)ep_out;
    (void)cur_alt_setting;

    uint8_t drain[64];
    while (tud_audio_n_available(func_id) >= sizeof(drain)) {
        tud_audio_n_read(func_id, drain, sizeof(drain));
    }

    return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport;
    (void)func_id;
    (void)ep_in;
    (void)cur_alt_setting;

    if ((ep_in & 0x80u) != 0u) {
        static uint8_t silence_buf[CFG_TUD_AUDIO_EP_SZ_IN] = {0};
        tud_audio_n_write(func_id, silence_buf, sizeof(silence_buf));
    }

    return true;
}

#endif