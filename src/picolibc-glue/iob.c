/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * iob.c
 *
 * Created on: Mar 14, 2024
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 *
 */

#include <pico/toolkit/compiler.h>

#include <pico/toolkit/iob.h>

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

