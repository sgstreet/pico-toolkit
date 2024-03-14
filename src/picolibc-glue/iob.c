#include <pico/iob.h>

#ifndef __alias
#define __alias(name) __attribute__((alias(name)))
#endif

#ifndef __weak
#define __weak __attribute__((weak))
#endif

extern int picolibc_putc(char c, FILE *file);
extern int picolibc_getc(FILE *file);
extern int picolibc_flush(FILE *file);
extern int picolibc_close(FILE *file);

struct iob _stdio = IOB_DEV_SETUP(picolibc_putc, picolibc_getc, picolibc_flush, picolibc_close, __SRD | __SWR, 0);

extern FILE *const stdin = (FILE *)&_stdio;
extern FILE *const stdout __alias("stdin");
extern FILE *const stderr __alias("stdin");

__weak int picolibc_putc(char c, FILE *file)
{
	return EOF;
}

__weak int picolibc_getc(FILE *file)
{
	return EOF;
}

__weak int picolibc_flush(FILE *file)
{
	return 0;
}

__weak int picolibc_close(FILE *file)
{
	return file->flush(file);
}

