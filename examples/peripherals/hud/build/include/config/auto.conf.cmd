deps_config := \
	/home/sparky/esp/esp-idf/components/app_trace/Kconfig \
	/home/sparky/esp/esp-idf/components/aws_iot/Kconfig \
	/home/sparky/esp/esp-idf/components/bt/Kconfig \
	/home/sparky/esp/esp-idf/components/esp32/Kconfig \
	/home/sparky/esp/esp-idf/components/esp_adc_cal/Kconfig \
	/home/sparky/esp/esp-idf/components/ethernet/Kconfig \
	/home/sparky/esp/esp-idf/components/fatfs/Kconfig \
	/home/sparky/esp/esp-idf/components/freertos/Kconfig \
	/home/sparky/esp/esp-idf/components/heap/Kconfig \
	/home/sparky/esp/esp-idf/components/libsodium/Kconfig \
	/home/sparky/esp/esp-idf/components/log/Kconfig \
	/home/sparky/esp/esp-idf/components/lwip/Kconfig \
	/home/sparky/esp/esp-idf/components/mbedtls/Kconfig \
	/home/sparky/esp/esp-idf/components/openssl/Kconfig \
	/home/sparky/esp/esp-idf/components/pthread/Kconfig \
	/home/sparky/esp/esp-idf/components/spi_flash/Kconfig \
	/home/sparky/esp/esp-idf/components/spiffs/Kconfig \
	/home/sparky/esp/esp-idf/components/tcpip_adapter/Kconfig \
	/home/sparky/esp/esp-idf/components/wear_levelling/Kconfig \
	/home/sparky/esp/esp-idf/components/bootloader/Kconfig.projbuild \
	/home/sparky/esp/esp-idf/components/esptool_py/Kconfig.projbuild \
	/home/sparky/esp/esp-idf/components/partition_table/Kconfig.projbuild \
	/home/sparky/esp/esp-idf/Kconfig

include/config/auto.conf: \
	$(deps_config)


$(deps_config): ;
