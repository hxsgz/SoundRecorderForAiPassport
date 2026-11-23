set(root "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/webrtcvad/cbits")
set(audio "${root}/webrtc/common_audio")
idf_component_register(SRCS
    "${audio}/vad/vad_core.c" "${audio}/vad/vad_filterbank.c"
    "${audio}/vad/vad_gmm.c" "${audio}/vad/vad_sp.c"
    "${audio}/signal_processing/division_operations.c"
    "${audio}/signal_processing/energy.c" "${audio}/signal_processing/get_scaling_square.c"
    "${audio}/signal_processing/resample_48khz.c"
    "${audio}/signal_processing/resample_fractional.c"
    "${audio}/signal_processing/resample_by_2_internal.c"
    INCLUDE_DIRS "${root}")
target_compile_options(${COMPONENT_LIB} PRIVATE -fwrapv)
