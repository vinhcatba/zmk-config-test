/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_scroll_scaler

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_STUDIO)
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>
#include <zmk/studio/rpc.h>
#endif

#include "scroll_scaler_dyn.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define SCROLL_DIV_MIN 1
#define SCROLL_DIV_MAX 128
#define SETTINGS_PREFIX "scroll_div"

struct behavior_scroll_scaler_state {
    int32_t divisor;
    int32_t last_studio_divisor;
};

struct behavior_scroll_scaler_data {
    const struct device *dev;
#if IS_ENABLED(CONFIG_SETTINGS)
    struct k_work_delayable save_work;
#endif
    struct behavior_scroll_scaler_state state;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "1/4 (Fastest)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 4},
    {.display_name = "1/8 (Very Fast)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 8},
    {.display_name = "1/12 (Fast)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 12},
    {.display_name = "1/16 (Default)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 16},
    {.display_name = "1/24 (Medium)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 24},
    {.display_name = "1/32 (Slow)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 32},
    {.display_name = "1/48 (Slower)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 48},
    {.display_name = "1/64 (Very Slow)", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 64},
};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif /* IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA) */

static int32_t clamp_divisor(int32_t div) {
    if (div < SCROLL_DIV_MIN) {
        return SCROLL_DIV_MIN;
    }
    if (div > SCROLL_DIV_MAX) {
        return SCROLL_DIV_MAX;
    }
    return div;
}

#if IS_ENABLED(CONFIG_SETTINGS)

static void save_work_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_scroll_scaler_data *data =
        CONTAINER_OF(dwork, struct behavior_scroll_scaler_data, save_work);

    int err = settings_save_one(SETTINGS_PREFIX "/0", &data->state,
                                sizeof(struct behavior_scroll_scaler_state));
    if (err < 0) {
        LOG_ERR("Failed to save scroll divisor (%d)", err);
    }
}

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static int behavior_scroll_scaler_init(const struct device *dev) {
    struct behavior_scroll_scaler_data *data = dev->data;
    data->dev = dev;
    data->state.divisor = zmk_scroll_scaler_dyn_get_divisor();
    data->state.last_studio_divisor = data->state.divisor;

#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_init_delayable(&data->save_work, save_work_callback);
#endif

    return 0;
}

/* Pressing this key on the Scroll layer intentionally does nothing. */
static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_scroll_scaler_driver_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

static struct behavior_scroll_scaler_data scroll_scaler_data_0 = {};
BEHAVIOR_DT_INST_DEFINE(0, behavior_scroll_scaler_init, NULL, &scroll_scaler_data_0, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_scroll_scaler_driver_api);

#if IS_ENABLED(CONFIG_ZMK_STUDIO)

static bool find_saved_studio_divisor(const struct device *dev, int32_t *out_div) {
    for (zmk_keymap_layer_index_t l = 0; l < ZMK_KEYMAP_LAYERS_LEN; l++) {
        zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(l);
        if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL) {
            break;
        }
        for (uint16_t p = 0; p < ZMK_KEYMAP_LEN; p++) {
            const struct zmk_behavior_binding *b =
                zmk_keymap_get_layer_binding_at_idx(layer_id, p);
            if (!b || !b->behavior_dev) {
                continue;
            }
            if (strcmp(b->behavior_dev, dev->name) == 0) {
                *out_div = clamp_divisor((int32_t)b->param1);
                return true;
            }
        }
    }
    return false;
}

static int studio_save_listener(const zmk_event_t *eh) {
    const struct zmk_studio_rpc_notification *ev = as_zmk_studio_rpc_notification(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->notification.which_subsystem == zmk_studio_Notification_keymap_tag &&
        ev->notification.subsystem.keymap.which_notification_type ==
            zmk_keymap_Notification_unsaved_changes_status_changed_tag &&
        !ev->notification.subsystem.keymap.notification_type.unsaved_changes_status_changed) {
        const struct device *dev = DEVICE_DT_INST_GET(0);
        struct behavior_scroll_scaler_data *data = dev->data;
        int32_t new_div;
        if (find_saved_studio_divisor(dev, &new_div) &&
            new_div != data->state.last_studio_divisor) {
            data->state.last_studio_divisor = new_div;
            data->state.divisor = new_div;
            zmk_scroll_scaler_dyn_set_divisor(new_div);
#if IS_ENABLED(CONFIG_SETTINGS)
            k_work_reschedule(&data->save_work, K_MSEC(2000));
#endif
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_scroll_scaler_studio, studio_save_listener);
ZMK_SUBSCRIPTION(behavior_scroll_scaler_studio, zmk_studio_rpc_notification);

#endif /* IS_ENABLED(CONFIG_ZMK_STUDIO) */

#if IS_ENABLED(CONFIG_SETTINGS)

static int scroll_scaler_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                          void *cb_arg) {
    if (strcmp(name, "0") != 0) {
        return -ENOENT;
    }

    if (len == sizeof(struct behavior_scroll_scaler_state)) {
        struct behavior_scroll_scaler_data *data = &scroll_scaler_data_0;
        int err = read_cb(cb_arg, &data->state, sizeof(struct behavior_scroll_scaler_state));
        if (err >= 0) {
            data->state.divisor = clamp_divisor(data->state.divisor);
            data->state.last_studio_divisor = clamp_divisor(data->state.last_studio_divisor);
            zmk_scroll_scaler_dyn_set_divisor(data->state.divisor);
        } else {
            LOG_ERR("Failed to load scroll divisor (%d)", err);
        }
        return MIN(err, 0);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(scroll_scaler, SETTINGS_PREFIX, NULL,
                               scroll_scaler_settings_load_cb, NULL, NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
