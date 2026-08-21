set(WEBP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../vendor/libwebp")

set(WEBP_SOURCES
    ${WEBP_ROOT}/src/dec/alpha_dec.c
    ${WEBP_ROOT}/src/dec/buffer_dec.c
    ${WEBP_ROOT}/src/dec/frame_dec.c
    ${WEBP_ROOT}/src/dec/idec_dec.c
    ${WEBP_ROOT}/src/dec/io_dec.c
    ${WEBP_ROOT}/src/dec/quant_dec.c
    ${WEBP_ROOT}/src/dec/tree_dec.c
    ${WEBP_ROOT}/src/dec/vp8_dec.c
    ${WEBP_ROOT}/src/dec/vp8l_dec.c
    ${WEBP_ROOT}/src/dec/webp_dec.c
    ${WEBP_ROOT}/src/demux/anim_decode.c
    ${WEBP_ROOT}/src/demux/demux.c
    ${WEBP_ROOT}/src/dsp/alpha_processing.c
    ${WEBP_ROOT}/src/dsp/cpu.c
    ${WEBP_ROOT}/src/dsp/dec.c
    ${WEBP_ROOT}/src/dsp/dec_clip_tables.c
    ${WEBP_ROOT}/src/dsp/filters.c
    ${WEBP_ROOT}/src/dsp/lossless.c
    ${WEBP_ROOT}/src/dsp/rescaler.c
    ${WEBP_ROOT}/src/dsp/upsampling.c
    ${WEBP_ROOT}/src/dsp/yuv.c
    ${WEBP_ROOT}/src/utils/bit_reader_utils.c
    ${WEBP_ROOT}/src/utils/color_cache_utils.c
    ${WEBP_ROOT}/src/utils/filters_utils.c
    ${WEBP_ROOT}/src/utils/huffman_utils.c
    ${WEBP_ROOT}/src/utils/palette.c
    ${WEBP_ROOT}/src/utils/quant_levels_dec_utils.c
    ${WEBP_ROOT}/src/utils/random_utils.c
    ${WEBP_ROOT}/src/utils/rescaler_utils.c
    ${WEBP_ROOT}/src/utils/thread_utils.c
    ${WEBP_ROOT}/src/utils/utils.c
)

idf_component_register(
    SRCS ${WEBP_SOURCES}
    INCLUDE_DIRS ${WEBP_ROOT}/src
    PRIV_INCLUDE_DIRS ${WEBP_ROOT}
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC WEBP_REDUCE_SIZE)
