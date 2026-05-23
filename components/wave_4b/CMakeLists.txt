if(CONFIG_KSIG_BOARD_WAVE_4B)
    idf_component_register(
        SRCS "wave_4b.c"
        INCLUDE_DIRS "include"
        REQUIRES driver esp_lcd esp_driver_ledc bsp_common
        PRIV_REQUIRES esp_lcd_touch_gt911
    )
else()
    idf_component_register(
        REQUIRES driver esp_lcd esp_driver_ledc bsp_common
    )
endif()
