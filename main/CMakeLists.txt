set(SOURCES
    "main.c"
    "core/base43.c"
    "core/btc_derivation.c"
    "core/crypto_utils.c"
    "core/custom_derivation.c"
    "core/descriptor_validator.c"
    "core/error.c"
    "core/eip712.c"
    "core/evm.c"
    "core/kef.c"
    "core/key.c"
    "core/message_sign.c"
    "core/mnemonic_slots.c"
    "core/mnemonic_tools.c"
    "core/pin.c"
    "core/psbt.c"
    "core/registry.c"
    "core/session.c"
    "core/settings.c"
    "core/ss_whitelist.c"
    "core/storage.c"
    "core/wallet.c"
    "qr/encoder.c"
    "qr/parser.c"
    "qr/scanner.c"
    "qr/viewer.c"
    "smartcard/smartcard_ccid.c"
    "smartcard/smartcard_satochip.c"
    "ui/battery.c"
    "ui/dialog.c"
    "ui/font_policy.c"
    "ui/input_helpers.c"
    "ui/key_info.c"
    "ui/keyboard.c"
    "ui/menu.c"
    "ui/nav.c"
    "ui/sankey.c"
    "ui/settings_row.c"
    "ui/theme.c"
    "ui/word_selector.c"
    "ui/assets/icons_16.c"
    "ui/assets/icons_24.c"
    "ui/assets/icons_36.c"
    "ui/assets/kern_logo_font.c"
    "ui/assets/kern_logo_lvgl.c"
    "ui/assets/krux_cn_20.c"
    "ui/assets/krux_cn_28.c"
    "krux_port/krux_camera_preview.c"
    "krux_port/krux_feature_catalog.c"
    "krux_port/krux_hardware_probe.c"
    "krux_port/krux_qr_decoder.c"
    "krux_port/krux_services.c"
    "krux_port/krux_storage_browser.c"
    "pages/capture_entropy.c"
    "pages/load_descriptor_storage.c"
    "pages/passphrase.c"
    "pages/screensaver.c"
    "pages/store_descriptor.c"
    "pages/store_mnemonic.c"
    "pages/home/addresses.c"
    "pages/home/home.c"
    "pages/home/public_key.c"
    "pages/home/backup/backup_menu.c"
    "pages/home/backup/mnemonic_entropy.c"
    "pages/home/backup/mnemonic_1248.c"
    "pages/home/backup/mnemonic_grid.c"
    "pages/home/backup/mnemonic_qr.c"
    "pages/home/backup/mnemonic_steel.c"
    "pages/home/backup/mnemonic_tinyseed.c"
    "pages/home/backup/mnemonic_words.c"
    "pages/krux_shell/krux_shell.c"
    "pages/load_mnemonic/load_menu.c"
    "pages/load_mnemonic/load_punch_grid.c"
    "pages/load_mnemonic/load_storage.c"
    "pages/load_mnemonic/manual_input.c"
    "pages/load_mnemonic/stackbit_restore.c"
    "pages/load_mnemonic/tinyseed_restore.c"
    "pages/login/about.c"
    "pages/login/login.c"
    "pages/login/login_settings.c"
    "pages/new_mnemonic/dice_rolls.c"
    "pages/new_mnemonic/entropy_from_camera.c"
    "pages/new_mnemonic/new_mnemonic_menu.c"
    "pages/pin/pin_page.c"
    "pages/pin/pin_settings.c"
    "pages/scan/scan.c"
    "pages/settings/descriptor_manager.c"
    "pages/settings/wallet_settings.c"
    "pages/shared/address_checker.c"
    "pages/shared/descriptor_loader.c"
    "pages/shared/kef_decrypt_page.c"
    "pages/shared/kef_encrypt_page.c"
    "pages/shared/key_confirmation.c"
    "pages/shared/bip39_check_page.c"
    "pages/shared/custom_derivation_page.c"
    "pages/shared/loaded_mnemonic_menu.c"
    "pages/shared/mnemonic_editor.c"
    "pages/shared/mnemonic_slots_page.c"
    "pages/shared/mnemonic_tool_page.c"
    "pages/shared/sensitive_pin.c"
    "pages/shared/storage_browser.c"
    "pages/shared/wipe_flash_dialog.c"
    "utils/bip39_filter.c"
)

# Select touch driver based on board
if(CONFIG_KERN_BOARD_WAVE_4B)
    set(TOUCH_DRIVER esp_lcd_touch_gt911)
    set(FONT_POLICY_WIDTH 720)
    set(FONT_POLICY_HEIGHT 720)
elseif(CONFIG_KERN_BOARD_WAVE_35)
    set(TOUCH_DRIVER esp_lcd_touch_ft5x06)
    set(FONT_POLICY_WIDTH 320)
    set(FONT_POLICY_HEIGHT 480)
elseif(CONFIG_KERN_BOARD_WAVE_5)
    set(TOUCH_DRIVER esp_lcd_touch_gt911)
    set(FONT_POLICY_WIDTH 720)
    set(FONT_POLICY_HEIGHT 1280)
elseif(CONFIG_KERN_BOARD_WAVE_43)
    set(TOUCH_DRIVER esp_lcd_touch_gt911)
    set(FONT_POLICY_WIDTH 480)
    set(FONT_POLICY_HEIGHT 800)
endif()

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS .
    EMBED_TXTFILES
        "smartcard/cert/ca.cert"
        "smartcard/cert/subca-satochip.cert"
        "smartcard/cert/test-ca.cert"
        "smartcard/cert/test-subca-satochip.cert"
    PRIV_REQUIRES lvgl ${TOUCH_DRIVER} wave_4b wave_35 wave_5 wave_43
                  nvs_flash sd_card spiffs esp_partition video k_quirc
                  esp_driver_ppa esp_app_format efuse esp_security esp_timer
                  heap mbedtls usb bbqr libwally-core cUR
                  json
)

if(DEFINED FONT_POLICY_WIDTH AND DEFINED FONT_POLICY_HEIGHT)
    find_program(PYTHON3_EXECUTABLE python3 REQUIRED)
    execute_process(
        COMMAND ${PYTHON3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/../tools/ui_font_policy.py
                --width ${FONT_POLICY_WIDTH}
                --height ${FONT_POLICY_HEIGHT}
                --emit cmake-icon-defs
        OUTPUT_VARIABLE ICON_FONT_DEFINITIONS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    if(ICON_FONT_DEFINITIONS)
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${ICON_FONT_DEFINITIONS})
    endif()
endif()

# Uncomment to enable Developer Tools menu (snapshot capture, decode testing)
# target_compile_definitions(${COMPONENT_LIB} PRIVATE DEV_TOOLS_ENABLED)
