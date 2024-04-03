add_executable(multicore-irq-test multicore-irq-test.c)

pico_set_linker_script(multicore-irq-test $ENV{PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(multicore-irq-test
	hardware_gpio
	hardware_uart
	cmsis_core
	multicore_support
	pico_runtime
)

pico_add_extra_outputs(multicore-irq-test)
