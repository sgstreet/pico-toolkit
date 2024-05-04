/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * bench_porting_layer_cmsis_rtos2.c
 *
 *  Created on: Apr 5, 2024
 *      Author: Stephen Street (stephen@redrocketcomputing.com)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <hardware/irq.h>
#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <hardware/timer.h>

#include <pico/toolkit/cmsis.h>
#include <pico/toolkit/compiler.h>
#include <cmsis/cmsis-rtos2.h>

#include "bench_api.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define SOFT_IRQ 26
#define TIMER_ISR_VECTOR_TABLE_INDEX  15

int picolibc_putc(char c, FILE *file);
int picolibc_getc(FILE *file);
void *_rtos2_alloc(size_t size);
void _rtos2_release(void *ptr);

static osThreadId_t thread_ids[10] = { 0 };
static osMessageQueueId_t queue_ids[5] = { 0 };
static osSemaphoreId_t semaphore_ids[5] = { 0 };
static osMutexId_t mutex_ids[5] = { 0 };
static osMemoryPoolId_t pool_id;

int picolibc_putc(char c, FILE *file)
{
	if (c == '\n')
		uart_putc(UART_ID, '\r');

	uart_putc(UART_ID, c);

	return c;
}

int picolibc_getc(FILE *file)
{
	return uart_getc(UART_ID);
}

__constructor void console_init(void)
{
	/* Set up our UART with the required speed. */
	uart_init(UART_ID, BAUD_RATE);

	/*
	 * Set the TX and RX pins by using the function select on the GPIO
	 * Set datasheet for more information on function select
	 */
	gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
	gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
}

void *_rtos2_alloc(size_t size)
{
	return calloc(1, size);
}

void _rtos2_release(void *ptr)
{
	return free(ptr);
}

void bench_test_task(void *context)
{
	((void (*)(void *arg))context)(0);
}

void bench_test_init(void (*test_init_function)(void *))
{
	/* First initialize the kernel so we can add the first task */
	osStatus_t os_status = osKernelInitialize();
	if (os_status != osOK) {
		fprintf(stderr, "failed to initialize the kernel: %d\n", os_status);
		return;
	}
	/* Forward to the main task for execution since cmsis rtos2 does not wrap the main execute */
	osThreadAttr_t attr = { .name = "bench_test_task", .attr_bits = osThreadDetached, .stack_size = 2048, .priority = osPriorityNormal};
	osThreadId_t bench_test_task_id = osThreadNew(bench_test_task, test_init_function, &attr);
	if (!bench_test_task_id) {
		fprintf(stderr, "failed to create the bench test task: %d\n", errno);
		return;
	}

	/* Start the kernal */
	os_status = osKernelStart();
	if (os_status != osOK) {
		fprintf(stderr, "kernel failed to start or there was a fatal error: %d\n", os_status);
		return;
	}
}

void bench_thread_set_priority(int priority)
{
	osThreadSetPriority(osThreadGetId(), osKernelPriority(priority));
}

int bench_thread_create(int thread_id, const char *thread_name, int priority, void (*entry_function)(void *), void *args)
{
	osThreadAttr_t thread_attr = { .name = thread_name, .attr_bits = osThreadCreateSuspended, .priority = osKernelPriority(priority)};
	thread_ids[thread_id] = osThreadNew(entry_function, 0, &thread_attr);
	if (!thread_ids[thread_id]) {
		fprintf(stderr, "failed to create thread %d: %d\n", thread_id, errno);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_thread_spawn(int thread_id, const char *thread_name, int priority, void (*entry_function)(void *), void *args)
{
	osThreadAttr_t thread_attr = { .name = thread_name, .priority = osKernelPriority(priority) };
	thread_ids[thread_id] = osThreadNew(entry_function, 0, &thread_attr);
	if (!thread_ids[thread_id]) {
		fprintf(stderr, "failed to create thread %d: %d\n", thread_id, errno);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

void bench_thread_start(int thread_id)
{
	bench_thread_resume(thread_id);
}

void bench_thread_resume(int thread_id)
{
	osStatus_t os_status = osThreadResume(thread_ids[thread_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to resume thread %d: %d\n", thread_id, os_status);
		abort();
	}
}

void bench_thread_suspend(int thread_id)
{
	osStatus_t os_status = osThreadSuspend(thread_ids[thread_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to suspend thread %d: %d\n", thread_id, os_status);
		abort();
	}
}

void bench_thread_abort(int thread_id)
{
	osStatus_t os_status = osThreadTerminate(thread_ids[thread_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to terminate thread %d: %d\n", thread_id, os_status);
		abort();
	}
}

void bench_thread_exit(void)
{
	osThreadExit();
}

void bench_yield(void)
{
	osThreadYield();
}

void bench_timing_init(void)
{
}

void bench_sync_ticks(void)
{
	busy_wait_us(1);
}

void bench_timing_start(void)
{
}

void bench_timing_stop(void)
{
}

bench_time_t bench_timing_counter_get(void)
{
	return time_us_64();
}

bench_time_t bench_timing_cycles_get(bench_time_t *time_start, bench_time_t *time_end)
{
	return *time_end - *time_start;
}

bench_time_t bench_timing_cycles_to_ns(bench_time_t cycles)
{
	return cycles * 1000;
}

int bench_sem_create(int sem_id, int initial_count, int maximum_count)
{
	semaphore_ids[sem_id] = osSemaphoreNew(maximum_count, initial_count, 0);
	if (!semaphore_ids[sem_id]) {
		fprintf(stderr, "failed to create semaphore %d: %d\n", sem_id, errno);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

void bench_sem_give(int sem_id)
{
	osStatus_t os_status = osSemaphoreRelease(semaphore_ids[sem_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to release semaphore %d: %d\n", sem_id, os_status);
		abort();
	}
}

void bench_sem_give_from_isr(int sem_id)
{
	bench_sem_give(sem_id);
}

int bench_sem_take(int sem_id)
{
	osStatus_t os_status = osSemaphoreAcquire(semaphore_ids[sem_id], osWaitForever);
	if (os_status != osOK) {
		fprintf(stderr, "failed to acquire semaphore %d: %d\n", sem_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_mutex_create(int mutex_id)
{
	osMutexAttr_t mutex_attr = { .attr_bits = osMutexRecursive | osMutexPrioInherit };
	mutex_ids[mutex_id] = osMutexNew(&mutex_attr);
	if (!mutex_ids[mutex_id]) {
		fprintf(stderr, "failed to create mutex %d: %d\n", mutex_id, errno);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_mutex_lock(int mutex_id)
{
	osStatus_t os_status = osMutexAcquire(mutex_ids[mutex_id], osWaitForever);
	if (os_status != osOK) {
		fprintf(stderr, "failed to acquire mutex %d: %d\n", mutex_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_mutex_unlock(int mutex_id)
{
	osStatus_t os_status = osMutexRelease(mutex_ids[mutex_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to release mutex %d: %d\n", mutex_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

void *bench_malloc(size_t size)
{
	return malloc(size);
}

void bench_free(void *ptr)
{
	free(ptr);
}

int bench_message_queue_create(int mq_id, const char *mq_name, size_t msg_max_num, size_t msg_max_len)
{
	osMessageQueueAttr_t queue_attr = { .name = mq_name };
	queue_ids[mq_id] = osMessageQueueNew(msg_max_num, msg_max_len, &queue_attr);
	if (!queue_ids[mq_id]) {
		fprintf(stderr, "failed to create queue %d: %d\n", mq_id, errno);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_message_queue_send(int mq_id, char *msg_ptr, size_t msg_len)
{
	osStatus_t os_status = osMessageQueuePut(queue_ids[mq_id], msg_ptr, 0, osWaitForever);
	if (os_status != osOK) {
		fprintf(stderr, "failed to put a message to queue %d: %d\n", mq_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_message_queue_receive(int mq_id, char *msg_ptr, size_t msg_len)
{
	osStatus_t os_status = osMessageQueueGet(queue_ids[mq_id], msg_ptr, 0, osWaitForever);
	if (os_status != osOK) {
		fprintf(stderr, "failed to get a message to queue %d: %d\n", mq_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

int bench_message_queue_delete(int mq_id, const char *mq_name)
{
	osStatus_t os_status = osMessageQueueDelete(queue_ids[mq_id]);
	if (os_status != osOK) {
		fprintf(stderr, "failed to delete queue %d: %d\n", mq_id, os_status);
		return BENCH_ERROR;
	}
	return BENCH_SUCCESS;
}

bench_isr_handler_t bench_timer_isr_get(void)
{
	bench_isr_handler_t *table = (bench_isr_handler_t *)SCB->VTOR;
	return table[TIMER_ISR_VECTOR_TABLE_INDEX];
}

void bench_timer_isr_set(bench_isr_handler_t handler)
{
	bench_isr_handler_t *table = (bench_isr_handler_t *)SCB->VTOR;
	table[TIMER_ISR_VECTOR_TABLE_INDEX] = handler;
}

void bench_timer_isr_restore(bench_isr_handler_t handler)
{
	SysTick->LOAD = bench_timer_cycles_per_tick() - 1;
	SysTick->VAL = 0;
	SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk |	SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk);

	bench_timer_isr_set(handler);
}

bench_time_t bench_timer_isr_expiry_set(uint32_t usec)
{
	uint32_t  cycles_per_usec;
	uint32_t  cycles;

	cycles_per_usec = (bench_timer_cycles_per_second() + 999999) / 1000000;
	cycles = cycles_per_usec * usec;

	SysTick->LOAD = cycles;
	SysTick->VAL = cycles - 1;
	SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk |	SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_CLKSOURCE_Msk);

	return (bench_time_t)cycles;

}

bench_time_t bench_timer_cycles_diff(bench_time_t trigger, bench_time_t sample)
{
	return trigger - sample + 1;
}

bench_time_t bench_timer_cycles_get(void)
{
	return (bench_time_t)SysTick->VAL;
}

uint32_t bench_timer_cycles_per_second(void)
{
	return SystemCoreClock;
}

uint32_t bench_timer_cycles_per_tick(void)
{
	return SystemCoreClock / 1000;
}

void bench_collect_resources(void)
{
	osPriority_t priority = osThreadGetPriority(osThreadGetId());
	osThreadSetPriority(osThreadGetId(), osPriorityIdle);
	osThreadSetPriority(osThreadGetId(), priority);
}
