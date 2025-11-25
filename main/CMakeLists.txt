idf_component_register(SRCS "MAP-FT8_main.c" "audio_driver.c"
                       PRIV_REQUIRES spi_flash esp_adc esp_driver_dac driver ft8_lib
                       INCLUDE_DIRS ""
                       EMBED_FILES "../test/191111_110130.wav"
                                   "../test/191111_110145.wav"
                                   "../test/191111_110200.wav"
                                   "../test/191111_110215.wav")
