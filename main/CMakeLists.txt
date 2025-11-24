idf_component_register(SRCS "MAP-FT8_main.c" "audio_driver.c"
                       PRIV_REQUIRES spi_flash esp_adc esp_driver_dac driver ft8_lib
                       INCLUDE_DIRS "")
