add_executable(cmsis-rtos2-validation)

target_sources(cmsis-rtos2-validation PRIVATE
	cmsis-rtos2-validation.c
	cmsis_rv2.c
	RV2_Config.c
	RV2_Common.c
	RV2_EventFlags.c
	RV2_GenWait.c
	RV2_Kernel.c
	RV2_MemoryPool.c
	RV2_MessageQueue.c
	RV2_Mutex.c
	RV2_Semaphore.c
	RV2_Thread.c
	RV2_ThreadFlags.c
	RV2_Timer.c
	tf_main.c
	tf_report.c
)

pico_set_linker_script(cmsis-rtos2-validation ${PICO_TOOLKIT_PATH}/src/picolibc-glue/pico-toolkit-flash.ld)

target_link_libraries(cmsis-rtos2-validation
	hardware_gpio
	hardware_uart
	pico_cmsis_rtos2
	pico_runtime
)

pico_add_extra_outputs(cmsis-rtos2-validation)