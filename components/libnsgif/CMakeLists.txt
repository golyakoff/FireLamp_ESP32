idf_component_register(
    SRCS
        libnsgif/src/gif.c
        libnsgif/src/lzw.c
    INCLUDE_DIRS
        libnsgif/include
    PRIV_INCLUDE_DIRS 
        .
        libnsgif/src
)

set_source_files_properties(libnsgif/src/lzw.c PROPERTIES COMPILE_FLAGS -Wno-maybe-uninitialized)


# LZW table of 1024 entries instead of 4096: 7 KB instead of 29 KB in one block, which an ESP32
# with WiFi and BLE often does not have. Enough for the frames up to GIF_MAX_PIXELS (effects/gif.c)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LZW_CODE_MAX=10)
