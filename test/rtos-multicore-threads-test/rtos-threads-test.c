#include <unistd.h>
#include <string.h>
#include <threads.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>

#include <pico/toolkit/compiler.h>

#include <pico/platform.h>

#include <hardware/gpio.h>
#include <hardware/uart.h>

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define NUM_WORKERS 7

int picolibc_putc(char c, FILE *file);
int picolibc_getc(FILE *file);

cnd_t cv;
mtx_t mtx;
volatile bool exiting = false;
volatile int nn = 0;
volatile int spins = 0;
int work[NUM_WORKERS] = { 0 };
int waits[NUM_WORKERS] = { 0 };
int cores[NUM_CORES][NUM_WORKERS] = { 0 };

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

static int worker_thread(void *context)
{
	int id = (int)context;

	mtx_lock(&mtx);

	while(!exiting) {

		++cores[get_core_num()][id];

		while(!nn && !exiting) {
			++waits[id];
			cnd_wait(&cv, &mtx);
		}

		++work[id];
		--nn;
		mtx_unlock(&mtx);
		thrd_yield();
		mtx_lock(&mtx);
	}

	mtx_unlock(&mtx);
	return 0;
}

static int server_thread(void *context)
{
	int njobs;

	mtx_lock(&mtx);

	while (!exiting) {

		if ((spins++ % 1000) == 0)
			putchar('.');

		mtx_unlock(&mtx);
		thrd_yield();
		mtx_lock(&mtx);

		njobs = rand() % (NUM_WORKERS + 1);
		nn = njobs;
		if ((rand() % 30) == 0)
			cnd_broadcast(&cv);
		else
			while (njobs--)
				cnd_signal(&cv);
	}

	cnd_broadcast(&cv);
	mtx_unlock(&mtx);

	return 0;
}

static int run_test(void)
{
	thrd_t server;
	thrd_t workers[NUM_WORKERS];
	thrd_attr_t attr_core_any;
	thrd_attr_t attr_core_0;
	thrd_attr_t attr_core_1;

	_thdr_attr_init(&attr_core_any, 0, __THRD_PRIORITY, __THRD_STACK_SIZE, UINT32_MAX);
	_thdr_attr_init(&attr_core_0, SCHEDULER_CORE_AFFINITY, __THRD_PRIORITY, __THRD_STACK_SIZE, 0);
	_thdr_attr_init(&attr_core_1, SCHEDULER_CORE_AFFINITY, __THRD_PRIORITY, __THRD_STACK_SIZE, 1);

	struct timespec duration = { .tv_sec = 5, .tv_nsec = 0 };

	if (cnd_init(&cv) != thrd_success) {
		printf("failed to initialize cnd: %d\n", errno);
		goto error_exit;
	}

	if (mtx_init(&mtx, mtx_prio_inherit) != thrd_success) {
		printf("failed to initialize mtx: %d\n", errno);
		goto error_destroy_cnd;
	}

	if (_thrd_create(&server, server_thread, 0, &attr_core_any) != thrd_success) {
		printf("could not create server thread: %d\n", errno);
		goto error_destroy_mtx;
	}

	memset(workers, 0, sizeof(workers));
	for (int i = 0; i < NUM_WORKERS; ++i) {
		if (_thrd_create(&workers[i], worker_thread, (void *)i, &attr_core_any) != thrd_success) {
			printf("could not create worker thread %d: %d\n", i, errno);
			goto error_destroy_thrds;
		}
	}

	printf("working for %llu seconds\n", duration.tv_sec);
	thrd_sleep(&duration, 0);

	mtx_lock(&mtx);
	exiting = true;
	mtx_unlock(&mtx);

	printf("\n");
	for (int i = 0; i < NUM_WORKERS; ++i) {
		printf("waiting for worker %d\n", i);
		if (thrd_join(workers[i], 0) == thrd_success)
			printf("joined with worker %d\n", i);
		else
			printf("failed to join worker %d\n", i);
	}
	printf("waiting for server\n");
	if (thrd_join(server, 0) == thrd_success)
		printf("joined with server\n");
	else
		printf("joined with server\n");
	printf("done: spins=%d ", spins);
	printf("\n");
	for (int i = 0; i < NUM_WORKERS; ++i)
		printf("work[%d]=%d ", i, work[i]);
	printf("\n");
	for (int i = 0; i < NUM_WORKERS; ++i)
		printf("waits[%d]=%d ", i, waits[i]);
	printf("\n");

	return 0;

error_destroy_thrds:
	mtx_lock(&mtx);
	exiting = true;
	mtx_unlock(&mtx);
	for (int i = 0; i < NUM_WORKERS; ++i)
		if (workers[i] != 0)
			thrd_join(workers[i], 0);
	thrd_join(server, 0);

error_destroy_mtx:
	mtx_destroy(&mtx);

error_destroy_cnd:
	cnd_destroy(&cv);

error_exit:
	return -1;
}

static __unused int main_task(void *context)
{
	return run_test();
}

int main(int argc, char **argv)
{
#if 0
	thrd_t main;
	int result;
	thrd_create(&main,  main_task, 0);
	thrd_join(main, &result);
	return result;
#else
	return run_test();
#endif
}
