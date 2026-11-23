idf_component_register(SRCS "main.c" "storage.c" "usb.c" "gate.c" "screen.c" "screen_view.c" "ui_fonts.c" "clip_clock.c"
    EMBED_FILES "assets/charging.rgb565"
    INCLUDE_DIRS "." REQUIRES bsp speex bcg729 webrtcvad nvs_flash esp_timer esp_hw_support esp_driver_usb_serial_jtag mbedtls esp_partition esp_lcd esp_pm)
