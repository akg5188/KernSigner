cmake_minimum_required(VERSION 3.16)
project(signer_simulator C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Configurable display resolution (default 720x720)
if(NOT DEFINED SIM_LCD_H_RES)
  set(SIM_LCD_H_RES 720)
endif()
if(NOT DEFINED SIM_LCD_V_RES)
  set(SIM_LCD_V_RES 720)
endif()

# Board identity (passed from justfile, default wave_4b)
if(NOT DEFINED SIM_BOARD)
  set(SIM_BOARD "wave_4b")
endif()

# Board feature flags
if(SIM_BOARD STREQUAL "wave_35")
  set(BSP_HAS_PMIC TRUE)
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

# The migration simulator intentionally excludes KernSigner's old wallet/PIN/crypto
# stack.  We only keep the board/UI shell needed to validate the KernSigner port.
set(MBEDTLS_INCLUDE_DIRS "")

# --- LVGL ---
include(FetchContent)
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/lvgl__lvgl)
if(NOT EXISTS "${LVGL_DIR}/src/lvgl.h")
  FetchContent_Declare(
    lvgl_upstream
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.3.0
  )
  FetchContent_GetProperties(lvgl_upstream)
  if(NOT lvgl_upstream_POPULATED)
    FetchContent_Populate(lvgl_upstream)
  endif()
  set(LVGL_DIR ${lvgl_upstream_SOURCE_DIR})
endif()
file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

set(APP_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main/ui)

# --- Simulator entry point and theme ---
set(SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/esp_idf_stubs/stubs.c
    ${APP_UI_DIR}/font_policy.c
    ${APP_UI_DIR}/i18n_text.c
    ${APP_UI_DIR}/menu.c
    ${APP_UI_DIR}/theme.c
    ${APP_UI_DIR}/word_selector.c
    ${APP_UI_DIR}/assets/signer_logo_lvgl.c
    ${APP_UI_DIR}/assets/signer_logo_font.c
    ${APP_UI_DIR}/assets/icons_16.c
    ${APP_UI_DIR}/assets/icons_24.c
    ${APP_UI_DIR}/assets/icons_36.c
    ${APP_UI_DIR}/assets/signer_cn_20.c
    ${APP_UI_DIR}/assets/signer_cn_28.c
)

# --- ESP-IDF stubs (NVS, FreeRTOS, eFuse, storage) ---
set(STUB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/esp_idf_stubs/freertos_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/esp_idf_stubs/nvs_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/esp_idf_stubs/app_menu_stubs.c
)

# --- Application utilities ---
set(APP_UTIL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/utils/bip39_filter.c
)

# --- Core application modules ---
set(APP_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/core/crypto_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/core/settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/i18n/i18n.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_camera_preview.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_feature_catalog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_hardware_probe.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_qr_decoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_services.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/signer_port/signer_storage_browser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/smartcard/smartcard_ccid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/smartcard/smartcard_pn5180.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/smartcard/smartcard_pn532.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../main/smartcard/smartcard_transport.c
)

# --- UI components ---
set(APP_UI_SOURCES)

# --- PIN pages ---
set(PIN_PAGE_SOURCES)

# --- Platform simulators (BSP, SD card, video/camera) ---
option(SIM_WEBCAM "Enable webcam capture" OFF)

set(PLATFORM_SIM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/bsp_sim/bsp_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/bsp_sim/pmic_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/sd_card_sim/sd_card_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/video_sim.c
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/stb_image_impl.c
)

if(SIM_WEBCAM)
    # v4l2_capture uses real kernel V4L2 headers (not the ESP-IDF stubs),
    # so it is compiled as a separate library without the stubs include path.
    if(APPLE)
        enable_language(OBJCXX)
        add_library(v4l2_capture STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/v4l2_capture_macos.mm
        )
        find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
        find_library(COREMEDIA_FRAMEWORK CoreMedia)
        find_library(COREVIDEO_FRAMEWORK CoreVideo)
        find_library(FOUNDATION_FRAMEWORK Foundation)
        target_link_libraries(v4l2_capture PRIVATE
            ${AVFOUNDATION_FRAMEWORK}
            ${COREMEDIA_FRAMEWORK}
            ${COREVIDEO_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
        )
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/v4l2_capture_macos.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
    else()
        add_library(v4l2_capture STATIC
            ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/v4l2_capture.c
        )
    endif()
    target_include_directories(v4l2_capture PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/include
        ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim
    )
    target_compile_options(v4l2_capture PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Application pages ---
set(APP_PAGES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main/pages)
set(APP_PAGE_SOURCES
    # KernSigner migration shell
    ${APP_PAGES_DIR}/signer_shell/signer_shell.c
    ${APP_PAGES_DIR}/home/backup/mnemonic_grid.c
    ${APP_PAGES_DIR}/shared/custom_derivation_page.c
    ${APP_PAGES_DIR}/shared/mnemonic_slots_page.c
)

# --- QR + k_quirc + cUR ---
set(QR_SOURCES)
set(KQUIRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/src/k_quirc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/src/k_quirc_version.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/src/k_quirc_identify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/src/k_quirc_decode.c
)
set(CUR_SOURCES)

add_library(lvgl STATIC
    ${LVGL_SOURCES}
)
target_include_directories(lvgl PUBLIC
    ${LVGL_DIR}
    ${LVGL_DIR}/src
)
target_include_directories(lvgl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(lvgl PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    LV_USE_SNAPSHOT=1
    SIM_LCD_H_RES=${SIM_LCD_H_RES}
    SIM_LCD_V_RES=${SIM_LCD_V_RES}
    lv_refr_now=signer_lv_refr_now_real
)
target_link_libraries(lvgl PUBLIC
    SDL2
)

add_executable(signer_simulator
    ${SIM_SOURCES}
    ${STUB_SOURCES}
    ${APP_UTIL_SOURCES}
    ${APP_CORE_SOURCES}
    ${APP_UI_SOURCES}
    ${PIN_PAGE_SOURCES}
    ${APP_PAGE_SOURCES}
    ${QR_SOURCES}
    ${PLATFORM_SIM_SOURCES}
    ${KQUIRC_SOURCES}
    ${CUR_SOURCES}
)

# Include paths — order matters: simulator headers shadow ESP-IDF
target_include_directories(signer_simulator PRIVATE
    # Simulator stubs (highest priority — shadow ESP-IDF headers)
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/esp_idf_stubs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/bsp_sim/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/video_sim/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform/sd_card_sim/include
    ${CMAKE_CURRENT_SOURCE_DIR}/platform               # sdkconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}                         # lv_conf.h
    ${MBEDTLS_INCLUDE_DIRS}
    # Application source tree
    ${CMAKE_CURRENT_SOURCE_DIR}/../main                 # ui/theme.h, core/, utils/, pages/
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/libwally-core/upstream/include
    # Component headers (for kef.c -> bbqr/miniz, cUR, k_quirc, video)
    ${CMAKE_CURRENT_SOURCE_DIR}/../components
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/bbqr/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/k_quirc/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/cUR/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/video
    # LVGL
    ${LVGL_DIR}
    ${LVGL_DIR}/src
)

target_compile_definitions(signer_simulator PRIVATE
    SIMULATOR=1
    LV_CONF_INCLUDE_SIMPLE
    LV_USE_SNAPSHOT=1
    SIM_LCD_H_RES=${SIM_LCD_H_RES}
    SIM_LCD_V_RES=${SIM_LCD_V_RES}
    K_QUIRC_ADAPTIVE_THRESHOLD
    K_QUIRC_BILINEAR_THRESHOLD
    $<$<BOOL:${SIM_WEBCAM}>:SIM_WEBCAM=1>
    $<$<BOOL:${BSP_HAS_PMIC}>:BSP_HAS_PMIC=1>
    $<$<STREQUAL:${SIM_BOARD},wave_4b>:CONFIG_KSIG_BOARD_WAVE_4B=1>
    $<$<STREQUAL:${SIM_BOARD},wave_35>:CONFIG_KSIG_BOARD_WAVE_35=1>
    $<$<STREQUAL:${SIM_BOARD},wave_5>:CONFIG_KSIG_BOARD_WAVE_5=1>
    $<$<STREQUAL:${SIM_BOARD},wave_43>:CONFIG_KSIG_BOARD_WAVE_43=1>
)

target_compile_options(signer_simulator PRIVATE
    -Wall -Wextra
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wno-type-limits
    -include mbedtls_compat.h
    # Hardening — the simulator parses untrusted input (image files, NVS
    # files, V4L2 frames). These flags are cheap and standard for any host
    # binary in that situation.
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
    -fPIE
)
set_target_properties(signer_simulator PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
else()
    target_link_options(signer_simulator PRIVATE
        # Hardening linker flags
        -pie
        -Wl,-z,relro
        -Wl,-z,now
        -Wl,-z,noexecstack
    )
endif()

target_link_libraries(signer_simulator PRIVATE
    lvgl
    SDL2
    Threads::Threads
    mbedcrypto
    m
    $<$<BOOL:${SIM_WEBCAM}>:v4l2_capture>
)
