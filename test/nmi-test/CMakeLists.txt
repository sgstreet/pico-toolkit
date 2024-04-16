add_executable(nmi-test nmi-test.c)

pico_set_linker_script(nmi-test $ENV{PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(nmi-test
	hardware_gpio
	hardware_uart
	picolibc_glue
	cmsis_core
	pico_nmi
	pico_runtime
)

pico_add_extra_outputs(nmi-test)
