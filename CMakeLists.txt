# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(DEFINED SDKCONFIG_DEFAULTS AND SDKCONFIG_DEFAULTS MATCHES "sdkconfig\\.defaults\\.high_value")
    idf_build_set_property(MINIMAL_BUILD ON)
endif()

add_compile_options(-Wno-format)
add_compile_options(-Wno-ignored-qualifiers)
add_compile_options(-Wno-attributes)
add_compile_options(-Wno-unused-function)

set(PROJECT_VER "0.0.7-rc1")
project(kernsigner)
