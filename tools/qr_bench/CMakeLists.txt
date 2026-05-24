cmake_minimum_required(VERSION 3.16)
project(qr_bench C)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(K_QUIRC_DIR "${REPO_ROOT}/components/k_quirc")
set(ZBAR_QR_DIR "${REPO_ROOT}/components/zbar_qr")

add_executable(qr_bench
    qr_bench.c
    ${K_QUIRC_DIR}/src/k_quirc.c
    ${K_QUIRC_DIR}/src/k_quirc_version.c
    ${K_QUIRC_DIR}/src/k_quirc_identify.c
    ${K_QUIRC_DIR}/src/k_quirc_decode.c
    ${ZBAR_QR_DIR}/src/zbar_qr.c
    ${ZBAR_QR_DIR}/zbar/bch15_5.c
    ${ZBAR_QR_DIR}/zbar/binarize.c
    ${ZBAR_QR_DIR}/zbar/decoder.c
    ${ZBAR_QR_DIR}/zbar/error.c
    ${ZBAR_QR_DIR}/zbar/image.c
    ${ZBAR_QR_DIR}/zbar/img_scanner.c
    ${ZBAR_QR_DIR}/zbar/isaac.c
    ${ZBAR_QR_DIR}/zbar/qr_finder.c
    ${ZBAR_QR_DIR}/zbar/qrdec.c
    ${ZBAR_QR_DIR}/zbar/qrdectxt.c
    ${ZBAR_QR_DIR}/zbar/refcnt.c
    ${ZBAR_QR_DIR}/zbar/rs.c
    ${ZBAR_QR_DIR}/zbar/scanner.c
    ${ZBAR_QR_DIR}/zbar/symbol.c
    ${ZBAR_QR_DIR}/zbar/util.c
)

target_include_directories(qr_bench PRIVATE
    ${K_QUIRC_DIR}/include
    ${K_QUIRC_DIR}/src
    ${K_QUIRC_DIR}/test
    ${K_QUIRC_DIR}/test/stubs
    ${ZBAR_QR_DIR}/include
    ${ZBAR_QR_DIR}/zbar
    ${ZBAR_QR_DIR}/zbar/decoder
)

target_compile_definitions(qr_bench PRIVATE
    K_QUIRC_ADAPTIVE_THRESHOLD
    K_QUIRC_BILINEAR_THRESHOLD
    K_QUIRC_SOFT_CELL_SAMPLE
    K_QUIRC_GRID_SIZE_RETRY
    ENABLE_QRCODE=1
    ENABLE_EAN=0
    ENABLE_I25=0
    ENABLE_DATABAR=0
    ENABLE_CODABAR=0
    ENABLE_CODE39=0
    ENABLE_CODE93=0
    ENABLE_CODE128=0
    ENABLE_PDF417=0
    ENABLE_SQCODE=0
)

target_compile_options(qr_bench PRIVATE
    -Wall
    -Wextra
    -Wno-format
    -Wno-unused-function
    -O2
)

target_link_libraries(qr_bench m)
