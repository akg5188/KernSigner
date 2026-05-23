cmake_minimum_required(VERSION 3.16)
project(zbar_qr_test C)

set(ZBAR_QR_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(REPO_ROOT "${ZBAR_QR_DIR}/../..")

add_executable(zbar_qr_test
    test_zbar_qr.c
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

target_include_directories(zbar_qr_test PRIVATE
    ${ZBAR_QR_DIR}/include
    ${ZBAR_QR_DIR}/zbar
    ${ZBAR_QR_DIR}/zbar/decoder
    ${REPO_ROOT}/components/k_quirc/include
    ${REPO_ROOT}/components/k_quirc/test
)

target_compile_definitions(zbar_qr_test PRIVATE
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

target_compile_options(zbar_qr_test PRIVATE
    -Wall
    -Wextra
    -Wno-format
    -Wno-unused-function
    -O2
)

target_link_libraries(zbar_qr_test m)
