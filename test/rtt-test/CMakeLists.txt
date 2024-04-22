add_executable(rtt_test
	rtt-test.c
)

pico_set_linker_script(rtt_test $ENV{PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(rtt_test
	picolibc_glue
	pico_rtt
	pico_unique_id
	pico_runtime
)

pico_add_extra_outputs(rtt_test)
