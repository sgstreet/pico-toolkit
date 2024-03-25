add_executable(backtrace-test backtrace-test.c)

pico_set_linker_script(backtrace-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(backtrace-test
	hardware_gpio
	hardware_uart
	cmsis_core
	pico_fault
	pico_runtime
)

pico_add_extra_outputs(backtrace-test)