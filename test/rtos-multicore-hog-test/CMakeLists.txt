add_executable(rtos-multicore-hog-test rtos-hog-test.c)

pico_set_linker_script(rtos-multicore-hog-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtos-multicore-hog-test
	hardware_gpio
	hardware_uart
	cmsis_core
	pico_unique_id
	pico_fault
	pico_scheduler
	multicore_support
	pico_runtime
)

pico_add_extra_outputs(rtos-multicore-hog-test)