set(root "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/speex")
set(names
    cb_search.c exc_10_32_table.c exc_8_128_table.c filters.c gain_table.c
    hexc_table.c high_lsp_tables.c lsp.c ltp.c speex.c
    stereo.c vbr.c vq.c bits.c exc_10_16_table.c
    exc_20_32_table.c exc_5_256_table.c exc_5_64_table.c gain_table_lbr.c hexc_10_32_table.c
    lpc.c lsp_tables_nb.c modes.c modes_wb.c nb_celp.c
    quant_lsp.c sb_celp.c speex_callbacks.c speex_header.c window.c
)
list(TRANSFORM names PREPEND "${root}/libspeex/")
idf_component_register(SRCS ${names} INCLUDE_DIRS "${root}/include" ".")
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIXED_POINT DISABLE_VBR DISABLE_FLOAT_API EXPORT=)
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -fwrapv -Wno-error)
