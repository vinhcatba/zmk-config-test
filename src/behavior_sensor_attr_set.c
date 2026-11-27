/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sensor_attr_set

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define DPI_MIN 200
#define DPI_MAX 3200
#define DPI_STEP 200
#define MAX_SETTINGS_LENGTH 16
#define SETTINGS_PREFIX "dpi_set"

struct behavior_sensor_attr_set_config {
    const struct device *sensor_device;
    const char settings_key[MAX_SETTINGS_LENGTH];
    int32_t attr;
    int32_t save_delay;
    int32_t load_delay;
    bool persistant;
    uint8_t length;
    int32_t values[];
};

struct behavior_sensor_attr_set_persistant_state {
    uint8_t index;
    int32_t cpi;
    int32_t last_studio_cpi;
};

struct behavior_sensor_attr_set_data {
    const struct device *dev;
#if IS_ENABLED(CONFIG_SETTINGS)
    struct k_work_delayable load_work;
    struct k_work_delayable save_work;
#endif
    struct behavior_sensor_attr_set_persistant_state state;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "200 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 200},
    {.display_name = "400 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 400},
    {.display_name = "600 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 600},
    {.display_name = "800 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 800},
    {.display_name = "1000 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1000},
    {.display_name = "1200 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1200},
    {.display_name = "1400 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1400},
    {.display_name = "1600 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1600},
    {.display_name = "1800 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 1800},
    {.display_name = "2000 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2000},
    {.display_name = "2200 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2200},
    {.display_name = "2400 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2400},
    {.display_name = "2600 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2600},
    {.display_name = "2800 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 2800},
    {.display_name = "3000 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 3000},
    {.display_name = "3200 DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 3200},
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

static int32_t sanitize_cpi(int32_t cpi) {
    if (cpi < DPI_MIN) {
        cpi = DPI_MIN;
    } else if (cpi > DPI_MAX) {
        cpi = DPI_MAX;
    }
    return ((cpi + (DPI_STEP / 2)) / DPI_STEP) * DPI_STEP;
}

static int apply_cpi(const struct device *dev, int32_t cpi) {
    struct behavior_sensor_attr_set_data *data = dev->data;
    const struct behavior_sensor_attr_set_config *config = dev->config;

    data->state.cpi = cpi;

    struct sensor_value val = {.val1 = cpi, .val2 = 0};
    int err = sensor_attr_set(config->sensor_device, SENSOR_CHAN_ALL, config->attr, &val);
    if (err < 0) {
        LOG_ERR("Failed to set sensor attr %d to %d (%d)", config->attr, cpi, err);
        return err;
    }

#if IS_ENABLED(CONFIG_SETTINGS)
    if (config->persistant) {
        k_work_reschedule(&data->save_work, K_MSEC(config->save_delay));
    }
#endif

    return 0;
}

#if IS_ENABLED(CONFIG_SETTINGS)

static void save_work_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_sensor_attr_set_data *data =
        CONTAINER_OF(dwork, struct behavior_sensor_attr_set_data, save_work);
    const struct device *dev = data->dev;
    const struct behavior_sensor_attr_set_config *config = dev->config;

    int err = settings_save_one(config->settings_key, &data->state,
                                sizeof(struct behavior_sensor_attr_set_persistant_state));
    if (err < 0) {
        LOG_ERR("Failed to save DPI settings (%d)", err);
    }
}

static void load_work_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_sensor_attr_set_data *data =
        CONTAINER_OF(dwork, struct behavior_sensor_attr_set_data, load_work);
    const struct device *dev = data->dev;
    const struct behavior_sensor_attr_set_config *config = dev->config;

    if (data->state.cpi >= DPI_MIN && data->state.cpi <= DPI_MAX) {
        struct sensor_value val = {.val1 = data->state.cpi, .val2 = 0};
        int err = sensor_attr_set(config->sensor_device, SENSOR_CHAN_ALL, config->attr, &val);
        if (err == -EBUSY) {
            k_work_reschedule(&data->load_work, K_MSEC(500));
        }
    }
}

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static int behavior_sensor_attr_set_init(const struct device *dev) {
    struct behavior_sensor_attr_set_data *data = dev->data;
    const struct behavior_sensor_attr_set_config *config = dev->config;

    data->dev = dev;
    data->state.last_studio_cpi = config->values[0];

#if IS_ENABLED(CONFIG_SETTINGS)
    if (config->persistant) {
        k_work_init_delayable(&data->load_work, load_work_callback);
        k_work_init_delayable(&data->save_work, save_work_callback);
    }
#endif

    return 0;
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sensor_attr_set_data *data = dev->data;
    const struct behavior_sensor_attr_set_config *config = dev->config;

    data->state.index = (data->state.index + 1) % config->length;
    data->state.last_studio_cpi = 0;
    int err = apply_cpi(dev, config->values[data->state.index]);
    if (err < 0) {
        return err;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_sensor_attr_set_driver_api = {
    .locality = BEHAVIOR_LOCALITY_EVENT_SOURCE,
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define SET_INST(n)                                                                                \
    static struct behavior_sensor_attr_set_data data##n = {};                                      \
    static const struct behavior_sensor_attr_set_config config##n = {                              \
        .sensor_device = DEVICE_DT_GET(DT_INST_PHANDLE(n, sensor_device)),                         \
        .length = DT_PROP_LEN(DT_DRV_INST(n), values),                                             \
        .values = DT_PROP(DT_DRV_INST(n), values),                                                 \
        .attr = DT_PROP(DT_DRV_INST(n), attr),                                                     \
        .save_delay = DT_PROP(DT_DRV_INST(n), save_delay),                                         \
        .load_delay = DT_PROP(DT_DRV_INST(n), load_delay),                                         \
        .persistant = DT_PROP(DT_DRV_INST(n), persistant),                                         \
        .settings_key = SETTINGS_PREFIX "/" #n,                                                    \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_sensor_attr_set_init, NULL, &data##n, &config##n,          \
                            POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,                               \
                            &behavior_sensor_attr_set_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SET_INST)

#if IS_ENABLED(CONFIG_ZMK_STUDIO)

#define GET_INST_DEV(n) DEVICE_DT_INST_GET(n),
static const struct device *const attr_set_devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_INST_DEV)};

static bool find_saved_studio_cpi(const struct device *dev, int32_t *out_cpi) {
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
                *out_cpi = sanitize_cpi((int32_t)b->param1);
                return true;
            }
#if DT_NODE_EXISTS(DT_NODELABEL(fn_dpi))
            if (strcmp(b->behavior_dev, DEVICE_DT_NAME(DT_NODELABEL(fn_dpi))) == 0) {
                *out_cpi = sanitize_cpi((int32_t)b->param2);
                return true;
            }
#endif
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
        for (size_t i = 0; i < ARRAY_SIZE(attr_set_devs); i++) {
            const struct device *dev = attr_set_devs[i];
            struct behavior_sensor_attr_set_data *data = dev->data;
            int32_t new_cpi;
            if (find_saved_studio_cpi(dev, &new_cpi) && new_cpi != data->state.last_studio_cpi) {
                data->state.last_studio_cpi = new_cpi;
                apply_cpi(dev, new_cpi);
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_sensor_attr_set_studio, studio_save_listener);
ZMK_SUBSCRIPTION(behavior_sensor_attr_set_studio, zmk_studio_rpc_notification);

#endif /* IS_ENABLED(CONFIG_ZMK_STUDIO) */

#if IS_ENABLED(CONFIG_SETTINGS)
#define SETTINGS_INST(n)                                                                           \
    case n: {                                                                                      \
        data = &data##n;                                                                           \
        config = &config##n;                                                                       \
        break;                                                                                     \
    }

static int sensor_attr_set_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                            void *cb_arg) {
    struct behavior_sensor_attr_set_data *data = NULL;
    const struct behavior_sensor_attr_set_config *config = NULL;
    char *endptr;
    long identifier = strtol(name, &endptr, 10);

    if (endptr == name) {
        return -ENOENT;
    }

    switch (identifier) {
        DT_INST_FOREACH_STATUS_OKAY(SETTINGS_INST)
    default:
        return -ENOENT;
    }

    if (config->persistant && len == sizeof(struct behavior_sensor_attr_set_persistant_state)) {
        int err = read_cb(cb_arg, &data->state,
                          sizeof(struct behavior_sensor_attr_set_persistant_state));
        if (err >= 0) {
            if (data->state.index >= config->length) {
                data->state.index = 0;
            }
            if (data->state.last_studio_cpi != 0 &&
                (data->state.last_studio_cpi < DPI_MIN || data->state.last_studio_cpi > DPI_MAX)) {
                data->state.last_studio_cpi = config->values[0];
            }
            if (data->state.cpi >= DPI_MIN && data->state.cpi <= DPI_MAX) {
                k_work_schedule(&data->load_work, K_MSEC(config->load_delay));
            }
        } else {
            LOG_ERR("Failed to load DPI settings (%d)", err);
        }
        return MIN(err, 0);
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(sensor_attr_set, SETTINGS_PREFIX, NULL,
                               sensor_attr_set_settings_load_cb, NULL, NULL);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
