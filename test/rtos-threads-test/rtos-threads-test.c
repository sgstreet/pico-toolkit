#include <unistd.h>
#include <string.h>
#include <threads.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>

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
mtx_t lock;
volatile bool exiting = false;
volatile int nn = 0;
volatile int spins = 0;
int work[NUM_WORKERS] = { 0 };
int waits[NUM_WORKERS] = { 0 };

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

static __attribute__((constructor)) void console_init(void)
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

	mtx_lock(&lock);

	while(!exiting) {

		while(!nn && !exiting) {
			++waits[id];
			cnd_wait(&cv, &lock);
		}

		++work[id];
		--nn;
		mtx_unlock(&lock);
		thrd_yield();
		mtx_lock(&lock);
	}

	mtx_unlock(&lock);
	printf("worker thread %d exiting\n", id);
	return 0;
}

static int server_thread(void *context)
{
	int njobs;

	mtx_lock(&lock);

	while (!exiting) {

		if ((spins++ % 1000) == 0)
			putchar('.');

		mtx_unlock(&lock);
		thrd_yield();
		mtx_lock(&lock);

		njobs = rand() % (NUM_WORKERS + 1);
		nn = njobs;
		if ((rand() % 30) == 0)
			cnd_broadcast(&cv);
		else
			while (njobs--)
				cnd_signal(&cv);
	}

	cnd_broadcast(&cv);
	printf("\nserver thread exiting\n");
	mtx_unlock(&lock);

	return 0;
}

int main(int argc, char **argv)
{
	thrd_t server;
	thrd_t workers[NUM_WORKERS];

	struct timespec duration = { .tv_sec = 20, .tv_nsec = 0 };

	if (cnd_init(&cv) != thrd_success) {
		printf("failed to initialize cnd: %d\n", errno);
		goto error_exit;
	}

	if (mtx_init(&lock, mtx_prio_inherit) != thrd_success) {
		printf("failed to initialize mtx: %d\n", errno);
		goto error_destroy_cnd;
	}

	if (thrd_create(&server, server_thread, 0) != thrd_success) {
		printf("could not create server thread: %d\n", errno);
		goto error_destroy_mtx;
	}

	memset(workers, 0, sizeof(workers));
	for (int i = 0; i < NUM_WORKERS; ++i)
		if (thrd_create(&workers[i], worker_thread, (void *)i) != thrd_success) {
			printf("could not create worker thread %d: %d\n", i, errno);
			goto error_destroy_thrds;
		}

	printf("working for %llu seconds\n", duration.tv_sec);
	thrd_sleep(&duration, 0);

	mtx_lock(&lock);
	exiting = true;
	mtx_unlock(&lock);

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
	for (int i = 0; i < NUM_WORKERS; ++i)
		printf("work[%d]=%d ", i, work[i]);
	for (int i = 0; i < NUM_WORKERS; ++i)
		printf("waits[%d]=%d ", i, waits[i]);
	printf("\n");

	return 0;

error_destroy_thrds:
	mtx_lock(&lock);
	exiting = true;
	mtx_unlock(&lock);
	for (int i = 0; i < NUM_WORKERS; ++i)
		if (workers[i] != 0)
			thrd_join(workers[i], 0);
	thrd_join(server, 0);

error_destroy_mtx:
	mtx_destroy(&lock);

error_destroy_cnd:
	cnd_destroy(&cv);

error_exit:
	return -1;
}
