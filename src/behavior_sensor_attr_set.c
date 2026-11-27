/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sensor_attr_set

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

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
    {.display_name = "Cycle DPI", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = 0},
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

    int32_t cpi;
    if (binding->param1 == 0) {
        data->state.index = (data->state.index + 1) % config->length;
        cpi = config->values[data->state.index];
    } else {
        cpi = (int32_t)binding->param1;
        if (cpi < DPI_MIN) {
            cpi = DPI_MIN;
        } else if (cpi > DPI_MAX) {
            cpi = DPI_MAX;
        }
        /* Snap to nearest multiple of 200 */
        cpi = ((cpi + (DPI_STEP / 2)) / DPI_STEP) * DPI_STEP;
    }

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
