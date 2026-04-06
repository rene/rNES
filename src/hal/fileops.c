/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * rNES - Nintendo Entertainment System Emulator
 * Copyright 2021-2026 Renê de Souza Pinto
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @file fileops.c
 *
 * It provides low level functions to load files on a POSIX system.
 */

#include "hal/hal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

/**
 * Load a file to the memory
 * @param [in] pathname Pathname to the file
 * @param [out] data File contents
 * @return int File size, error code otherwise (< 0)
 */
ssize_t load_file(const char *pathname, uint8_t **data)
{
	FILE *fp;
	int ret;
	struct stat finfo;
	*data = NULL;
	if ((fp = fopen(pathname, "r")) == NULL) {
		return -errno;
	}
	if ((ret = fstat(fileno(fp), &finfo)) < 0) {
		fclose(fp);
		return ret;
	}
	/* Read the entire file */
	*data = malloc(sizeof(uint8_t) * finfo.st_size);
	if (*data == NULL) {
		fclose(fp);
		return -ENOMEM;
	}
	if (fread(*data, finfo.st_size, 1, fp) != 1) {
		fclose(fp);
		return -EIO;
	}
	fclose(fp);
	return finfo.st_size;
}
