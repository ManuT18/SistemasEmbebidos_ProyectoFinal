idf_component_register(SRCS "ft8/constants.c" "ft8/encode.c" "ft8/message.c" "ft8/text.c" "ft8/ldpc.c" "ft8/decode.c" "ft8/crc.c" "fft/kiss_fft.c" "fft/kiss_fftr.c" "common/monitor.c"
                       INCLUDE_DIRS "." "ft8" "fft" "common")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-unused-variable -Wno-unused-function -Wno-unused-const-variable)
