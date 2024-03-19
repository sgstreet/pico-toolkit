add_executable(rtos-toolkit-hog-test rtos-toolkit-hog-test.c)

pico_enable_stdio_usb(rtos-toolkit-hog-test 1)
pico_enable_stdio_uart(rtos-toolkit-hog-test 0)

pico_set_linker_script(rtos-toolkit-hog-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtos-toolkit-hog-test
	hardware_gpio
	hardware_uart
	cmsis_core
	pico_atomic
	pico_tls
	picolibc_glue
	pico_unique_id
	pico_rtos
	pico_runtime
)

pico_add_extra_outputs(rtos-toolkit-hog-test)