add_executable(rtos-hog-test rtos-hog-test.c)

pico_set_linker_script(rtos-hog-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtos-hog-test
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

pico_add_extra_outputs(rtos-hog-test)