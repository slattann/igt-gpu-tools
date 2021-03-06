/*
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "drm.h"

#define MiB(x) ((x) * 1024 * 1024)

typedef void *(*mmap_fn_t)(int, uint32_t, uint64_t, uint64_t, unsigned int);

static void *wrap_gem_mmap__gtt(int i915, uint32_t handle,
				uint64_t offset, uint64_t length,
				unsigned int prot)
{
	return gem_mmap__gtt(i915, handle, length, prot);
}

static void pread_self(int i915)
{
	int start = gem_has_mappable_ggtt(i915) ? 0 : 1;
	static const mmap_fn_t mmap_fn[] = {
		wrap_gem_mmap__gtt,
		gem_mmap__cpu,
		gem_mmap__wc,
		NULL
	};
	for (const mmap_fn_t *fn = mmap_fn + start; *fn; fn++) {
		uint32_t handle = gem_create(i915, MiB(4));
		void *ptr = (*fn)(i915, handle, 0, MiB(4), PROT_WRITE);

		gem_read(i915, handle, 0, ptr + MiB(3), MiB(1));
		gem_read(i915, handle, MiB(3), ptr, MiB(1));
		gem_read(i915, handle, MiB(1), ptr + MiB(1), MiB(2));

		munmap(ptr, MiB(4));
		gem_close(i915, handle);
	}
}

#define OBJECT_SIZE 16384
#define KGRN "\x1B[32m"
#define KRED "\x1B[31m"
#define KNRM "\x1B[0m"

static void do_gem_read(int fd, uint32_t handle, void *buf, int len, int loops)
{
	while (loops--)
		gem_read(fd, handle, 0, buf, len);
}

static double elapsed(const struct timeval *start,
		      const struct timeval *end,
		      int loop)
{
	return (1e6*(end->tv_sec - start->tv_sec) + (end->tv_usec - start->tv_usec))/loop;
}

static const char *bytes_per_sec(char *buf, double v)
{
	const char *order[] = {
		"",
		"KiB",
		"MiB",
		"GiB",
		"TiB",
		NULL,
	}, **o = order;

	while (v > 1000 && o[1]) {
		v /= 1000;
		o++;
	}
	sprintf(buf, "%.1f%s/s", v, *o);
	return buf;
}

uint32_t *src, dst;
int fd, count;
int object_size = 0;

static int opt_handler(int opt, int opt_index, void *data)
{
	switch (opt) {
	case 's':
		object_size = atoi(optarg);
		break;
	default:
		return IGT_OPT_HANDLER_ERROR;
	}

	return IGT_OPT_HANDLER_SUCCESS;
}

const char *help_str = "  -s\tObject size in bytes\n";

igt_main_args("s:", NULL, help_str, opt_handler, NULL)
{
	double usecs;
	char buf[100];
	const char* bps;
	const struct {
		int level;
		const char *name;
	} cache[] = {
		{ 0, "uncached" },
		{ 1, "snoop" },
		{ 2, "display" },
		{ -1 },
	}, *c;

	if (object_size == 0)
		object_size = OBJECT_SIZE;
	object_size = (object_size + 3) & -4;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);

		dst = gem_create(fd, object_size);
		src = malloc(object_size);
	}

	igt_subtest("bench") {
		for (count = 1; count <= 1<<17; count <<= 1) {
			struct timeval start, end;

			gettimeofday(&start, NULL);
			do_gem_read(fd, dst, src, object_size, count);
			gettimeofday(&end, NULL);
			usecs = elapsed(&start, &end, count);
			bps = bytes_per_sec(buf, object_size/usecs*1e6);
			igt_info("Time to pread %d bytes x %6d:	%7.3fµs, %s\n",
				 object_size, count, usecs, bps);
			fflush(stdout);
		}
	}

	igt_subtest("self")
		pread_self(fd);

	for (c = cache; c->level != -1; c++) {
		igt_subtest(c->name) {
			gem_set_caching(fd, dst, c->level);

			for (count = 1; count <= 1<<17; count <<= 1) {
				struct timeval start, end;

				gettimeofday(&start, NULL);
				do_gem_read(fd, dst, src, object_size, count);
				gettimeofday(&end, NULL);
				usecs = elapsed(&start, &end, count);
				bps = bytes_per_sec(buf, object_size/usecs*1e6);
				igt_info("Time to %s pread %d bytes x %6d:	%7.3fµs, %s\n",
					 c->name, object_size, count, usecs, bps);
				fflush(stdout);
			}
		}
	}

	igt_fixture {
		free(src);
		gem_close(fd, dst);

		close(fd);
	}
}
