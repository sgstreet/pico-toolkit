
#include <stdio.h>
#include <stdlib.h>

#include <cmsis/cmsis-rtos2.h>

osStatus_t osDelay (uint32_t ticks)
{
	/* No zero parameters */
	if (ticks == 0)
		return osErrorParameter;

	/* Kernel must we running */
	if (osKernelGetState() != osKernelRunning)
		return osError;

	/* Can not be isr context */
	osStatus_t os_status = osKernelContextIsValid(false, ticks);
	if (os_status != osOK)
		return os_status;

	/* Forward, tick and msec are treated the same */
	int status = scheduler_sleep(ticks);
	if (status < 0)
		return osError;

	/* All good */
	return osOK;
}

osStatus_t osDelayUntil (uint32_t ticks)
{
	/* No zero parameters */
	if (ticks == 0)
		return osErrorParameter;

	/* Kernel must we running */
	if (osKernelGetState() != osKernelRunning)
		return osError;

	/* Can not be isr context */
	osStatus_t os_status = osKernelContextIsValid(false, ticks);
	if (os_status != osOK)
		return os_status;

	/* Get calculate the number of ticks to sleep */
	uint32_t msecs = ticks - scheduler_get_ticks();

	/* Forward, tick and msec are treated the same */
	int status = scheduler_sleep(msecs);
	if (status < 0)
		return osError;

	/* All good */
	return osOK;

}
