add_executable(rtos-toolkit-hog-test rtos-toolkit-hog-test.c)

pico_enable_stdio_usb(rtos-toolkit-hog-test 1)
pico_enable_stdio_uart(rtos-toolkit-hog-test 0)

pico_set_linker_script(rtos-toolkit-hog-test ${CMAKE_CURRENT_SOURCE_DIR}/rtos-toolkit-flash.ld)

target_link_libraries(rtos-toolkit-hog-test
	cmsis_core
	hardware_gpio
	hardware_uart
	hardware_divider
	pico_time
	pico_util
	pico_platform
	pico_rtos
)

pico_add_extra_outputs(rtos-toolkit-hog-test)