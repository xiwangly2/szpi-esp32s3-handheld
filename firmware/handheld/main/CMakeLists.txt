idf_component_register(SRCS "esp32_s3_szp.c" "main.c" "app_ui.c" "random_image_app.c"
                       SRCS "device_storage.c" "sdcard_layout.c"
                       SRCS "text_preview_encoding.c"
                       SRCS "wifi_qr_payload.c" "wifi_qr_scanner.c"
                       SRCS "video_player.c" "extractor_log_compat.c"
                       SRCS "ip_camera_app.c" "ip_camera_stream.c"
                       SRCS "information_app.c" "information_payload.c"
                       SRCS "information_setup.c" "information_setup_protocol.c"
                       SRCS "assets/img_att_icon.c"
                       SRCS "assets/img_btset_icon.c"
                       SRCS "assets/img_camera_icon.c"
                       SRCS "assets/img_music_icon.c"
                       SRCS "assets/img_sd_icon.c"
                       SRCS "assets/img_wifiset_icon.c"
                       SRCS "assets/font_alipuhui20.c"
                       SRCS "bt/ble_hidd_demo.c" "bt/esp_hidd_prf_api.c" "bt/hid_dev.c" "bt/hid_device_le_prf.c"
                       INCLUDE_DIRS "."
                       INCLUDE_DIRS "bt"
                       EMBED_TXTFILES "web/info_setup.html" "web/info_setup.css" "web/info_setup.js")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-const-variable)

# The prebuilt extractor archive is linked after the component archive group.
# Keep its logging entry point (the IDF 5.4 adapter or the native newer API).
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=esp_log")
