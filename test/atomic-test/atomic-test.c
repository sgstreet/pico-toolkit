#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pico/toolkit/spinlock.h>

static bool atomic_flag_test(void)
{
	atomic_flag flag = {0};

	bool result = atomic_flag_test_and_set(&flag);

	printf("result: %s\n", result ? "true" : "false");

	atomic_flag_clear(&flag);

	return result;
}

int main(int argc, char **argv)
{

	atomic_flag_test();

	spinlock_t locks[16];

	for (size_t i = 0; i < 16; ++i)
		locks[i] = 0;

	spin_lock(&locks[0]);
	spin_unlock(&locks[0]);
	__unused bool locked = spin_try_lock(&locks[0]);
	locked = spin_try_lock(&locks[0]);
	spin_unlock(&locks[0]);

	for (size_t i = 0; i < 16; ++i)
		spin_lock(&locks[i]);

	for (size_t i = 0; i < 16; ++i)
		spin_unlock(&locks[i]);

}

