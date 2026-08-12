idf_component_register(
    SRCS
        "main.c"
        "app_state.c"
        "audio_engine.c"
        "ble_transport.c"
        "button_input.c"
        "face_ui.c"
        "voice_link.c"
        "voice_runtime.c"
        "wifi_transport.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        driver
        espressif__esp_mmap_assets
        esp_event
        esp_timer
        esp_netif
        esp_wifi
        freertos
        nvs_flash
        esp_codec_dev
        waveshare__esp32_s3_touch_amoled_1_75c
        esphome__micro-opus
)

# The orbital loop lives in the dedicated flash asset partition.  It is kept
# outside the app binary so the animation can retain its full display detail.
spiffs_create_partition_assets(
    orbit_frames
    "${CMAKE_CURRENT_LIST_DIR}/../../animations/orbital/esp32-frames"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".bin"
)
