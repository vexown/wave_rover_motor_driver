idf_component_register(
    SRCS "src/motor_control.c"
    INCLUDE_DIRS "include"
    REQUIRES "driver" "esp_timer"
)
