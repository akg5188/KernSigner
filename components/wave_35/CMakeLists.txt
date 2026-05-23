if(CONFIG_KSIG_BOARD_WAVE_35)
    idf_component_register(
        SRCS "wave_35.c" "pmic.c"
        INCLUDE_DIRS "include"
        REQUIRES driver esp_lcd esp_driver_ledc bsp_common
        PRIV_REQUIRES esp_lcd_touch_ft5x06
    )
else()
    idf_component_register(
        REQUIRES driver esp_lcd esp_driver_ledc bsp_common
    )
endif()
