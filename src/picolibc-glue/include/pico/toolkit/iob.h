/*
 * Copyright (C) 2024 Stephen Street
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * iob.h
 *
 * Created on: Mar 14, 2024
 *     Author: Stephen Street (stephen@redrocketcomputing.com)
 *
 */

#ifndef _IOB_H_
#define _IOB_H_

#include <stdio.h>

#include <pico/toolkit/compiler.h>

#ifndef __always_inline
#define __always_inline __attribute__((always_inline))
#endif

#define __SPLATFORM 0x80

extern struct iob _stdio;

struct iob
{
	struct __file_close file_close;
	void *platform;
};

static __always_inline inline void *iob_get_platform(const struct iob *iob)
{
	return iob->platform;
}

static __always_inline inline void iob_set_platform(struct iob *iob, void *platform)
{
	iob->platform = platform;
}

static __always_inline inline struct iob *iob_from(FILE* file)
{
	return (struct iob *)file;
}

static __always_inline inline FILE *file_from(struct iob *iob)
{
	return (FILE *)iob;
}

static __always_inline inline void *file_get_platform(const FILE* file)
{
	const struct iob *iob = (const struct iob *)file;
	return iob->platform;
}

static __always_inline inline void file_set_platform(FILE* file, void *platform)
{
	struct iob *iob = (struct iob *)file;
	iob->platform = platform;
}

static __always_inline inline void iob_setup_stream(struct iob *iob, int (*put)(char, struct __file *file), int (*get)(struct __file *file), int (*flush)(struct __file *file), int (*close)(struct __file * file), uint8_t flags, void *platform)
{
	iob->file_close.file.put = put;	
	iob->file_close.file.get = get;	
	iob->file_close.file.flush = flush;
	iob->file_close.file.flags = flags;
	iob->platform = platform;
}

#define IOB_DEV_SETUP(PUT, GET, FLUSH, CLOSE, FLAGS, PLATFORM) { .file_close.file.put = PUT, .file_close.file.get = GET, .file_close.file.flush = FLUSH, .file_close.file.flags = (FLAGS | __SCLOSE | __SPLATFORM), .file_close.close = CLOSE, .platform = PLATFORM };

#endif
