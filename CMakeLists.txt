if(CONFIG_ZMK_BEHAVIOR_SENSOR_ATTR_SET)
  target_sources(app PRIVATE src/behavior_sensor_attr_set.c)
endif()

if(CONFIG_ZMK_INPUT_PROCESSOR_SCROLL_SCALER_DYN)
  target_sources(app PRIVATE src/input_processor_scroll_scaler_dyn.c)
endif()

if(CONFIG_ZMK_BEHAVIOR_SCROLL_SCALER)
  target_sources(app PRIVATE src/behavior_scroll_scaler.c)
endif()
