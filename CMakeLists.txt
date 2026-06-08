# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only explicitly requested components. This keeps air-gapped builds from
# compiling dormant ESP-IDF network components when LWIP/ETH are disabled.
idf_build_set_property(MINIMAL_BUILD ON)
idf_build_set_property(BOOTLOADER_EXTRA_COMPONENT_DIRS
                       "${CMAKE_CURRENT_LIST_DIR}/bootloader_components"
                       APPEND)

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)
add_compile_options(-msmall-data-limit=0)

set(PROJECT_VER "0.0.7-rc1")
project(kernsigner)

idf_build_get_property(KSIG_BUILD_COMPONENTS BUILD_COMPONENTS)
set(KSIG_FORBIDDEN_CONNECTIVITY_COMPONENTS
    bt
    esp_wifi
    esp_eth
    esp_http_client
    esp_http_server
    esp_https_ota
    esp_https_server
    esp_coex
    esp_hal_ieee802154
    esp_local_ctrl
    esp_netif
    esp_netif_stack
    esp_phy
    esp-tls
    ieee802154
    lwip
    mqtt
    openthread
    protocomm
    tcp_transport
    wifi_provisioning
    wpa_supplicant
)
foreach(component IN LISTS KSIG_FORBIDDEN_CONNECTIVITY_COMPONENTS)
    if(component IN_LIST KSIG_BUILD_COMPONENTS)
        message(FATAL_ERROR
            "Forbidden connectivity component in air-gapped build: ${component}")
    endif()
endforeach()
