idf_component_register(
    SRCS
        "src/zbar_qr.c"
        "zbar/bch15_5.c"
        "zbar/binarize.c"
        "zbar/decoder.c"
        "zbar/error.c"
        "zbar/image.c"
        "zbar/img_scanner.c"
        "zbar/isaac.c"
        "zbar/qr_finder.c"
        "zbar/qrdec.c"
        "zbar/qrdectxt.c"
        "zbar/refcnt.c"
        "zbar/rs.c"
        "zbar/scanner.c"
        "zbar/symbol.c"
        "zbar/util.c"
    INCLUDE_DIRS "include" "zbar"
    PRIV_INCLUDE_DIRS "zbar/decoder"
    REQUIRES k_quirc
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
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

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-format
    -Wno-implicit-fallthrough
    -Wno-parentheses
    -Wno-sign-compare
    -Wno-unused-but-set-variable
    -Wno-unused-function
    -Wno-unused-parameter
    -Wno-unused-variable
    -Wno-error=implicit-fallthrough
    -Wno-error=parentheses
    -Wno-error=sign-compare
    -Wno-error=unused-but-set-variable
    -Wno-error=unused-parameter
    -Wno-error=unused-variable
)
