idf_component_register(SRCS "MAP-FT8_main.c" "audio_driver.c" "ft8_decode_task.c" "ft8_encode_task.c"
                       PRIV_REQUIRES spi_flash esp_adc esp_driver_dac driver ft8_lib nvs_flash web_interface esp_timer
                       INCLUDE_DIRS "include"
                       EMBED_FILES "../test/websdr_test1.wav"
                                   "../test/websdr_test2.wav"
                                   "../test/websdr_test3.wav"
                                   "../test/websdr_test4.wav"
                                   "../test/websdr_test5.wav")
