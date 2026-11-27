/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_scroll_scaler_dyn

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>
#include <zephyr/logging/log.h>

#include "scroll_scaler_dyn.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SCROLL_DIV_MIN 1
#define SCROLL_DIV_MAX 128

static int32_t scroll_divisor = DT_INST_PROP_OR(0, default_divisor, 16);

void zmk_scroll_scaler_dyn_set_divisor(int32_t div) {
    if (div < SCROLL_DIV_MIN) {
        div = SCROLL_DIV_MIN;
    } else if (div > SCROLL_DIV_MAX) {
        div = SCROLL_DIV_MAX;
    }
    scroll_divisor = div;
}

int32_t zmk_scroll_scaler_dyn_get_divisor(void) { return scroll_divisor; }

struct scroll_scaler_dyn_config {
    uint8_t type;
    size_t codes_len;
    uint16_t codes[];
};

static int scale_val(struct input_event *event, int32_t div,
                     struct zmk_input_processor_state *state) {
    int16_t value_mul = event->value;

    if (state && state->remainder) {
        value_mul += *state->remainder;
    }

    int16_t scaled = value_mul / (int16_t)div;

    if (state && state->remainder) {
        *state->remainder = value_mul - (scaled * (int16_t)div);
    }

    event->value = scaled;
    return 0;
}

static int scroll_scaler_dyn_handle_event(const struct device *dev, struct input_event *event,
                                          uint32_t param1, uint32_t param2,
                                          struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);

    const struct scroll_scaler_dyn_config *cfg = dev->config;

    if (event->type != cfg->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (size_t i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] == event->code) {
            return scale_val(event, scroll_divisor, state);
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api scroll_scaler_dyn_driver_api = {
    .handle_event = scroll_scaler_dyn_handle_event,
};

#define SCROLL_SCALER_DYN_INST(n)                                                                  \
    static const struct scroll_scaler_dyn_config scroll_scaler_dyn_config_##n = {                  \
        .type = DT_INST_PROP_OR(n, type, INPUT_EV_REL),                                            \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                                   \
        .codes = DT_INST_PROP(n, codes),                                                           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &scroll_scaler_dyn_config_##n, POST_KERNEL,         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &scroll_scaler_dyn_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SCROLL_SCALER_DYN_INST)
