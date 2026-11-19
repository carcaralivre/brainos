if("$ENV{STICKY_ENGINE}" STREQUAL "oido")
  set(ENGINE_REQ tinyasr)
else()
  set(ENGINE_REQ stt_engine sram_pool)
endif()
idf_component_register(SRCS "main.c" "mic.c" INCLUDE_DIRS "."
    REQUIRES ${ENGINE_REQ} esp_partition spi_flash esp_timer esp_psram esp_driver_i2s esp_driver_gpio esp_driver_uart heap)
