# Compatibility shims for mbedTLS v4 (ESP-IDF 6.x).
#
# The KernSigner migration shell currently builds on ESP-IDF 5.5.x for ESP32-P4,
# where mbedtls/private/*.h does not exist. Keep this component visible so
# libwally can depend on it later, but only compile the shim on IDF 6+.
if(DEFINED IDF_VERSION_MAJOR AND IDF_VERSION_MAJOR LESS 6)
    message(STATUS "mbedtls_compat: ESP-IDF ${IDF_VERSION}; using empty shim on IDF 5.x")
    idf_component_register(INCLUDE_DIRS ".")
    return()
endif()

# mbedTLS v4 moved crypto headers (sha256.h, aes.h, etc.) from mbedtls/ to mbedtls/private/
# These shims redirect #include <mbedtls/sha256.h> to <mbedtls/private/sha256.h>
# Also provides legacy mbedtls_sha512_* symbols removed by PSA hardware acceleration.
idf_component_register(
    SRCS "sha512_compat.c"
    INCLUDE_DIRS "."
    REQUIRES mbedtls
)
