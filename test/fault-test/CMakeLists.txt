add_executable(fault-test fault-test.c)

pico_set_linker_script(fault-test ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(fault-test
	hardware_gpio
	hardware_uart
	cmsis_core
	pico_fault
	pico_runtime
)

pico_add_extra_outputs(fault-test)