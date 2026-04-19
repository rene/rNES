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
 * @file ringbuffer.h
 * @see ringbuffer.c
 */
#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(_POSIX_C_SOURCE)
#include <semaphore.h>
#elif defined(_WIN32)
#include <synchapi.h>
#include <windows.h>

typedef HANDLE sem_t;
#elif defined(__APPLE__)
#include <dispatch/dispatch.h>

typedef dispatch_semaphore_t sem_t;
#else
#warning Semaphore functions not available, ring buffer is not thread safe!
typedef unsigned int sem_t;
#endif

/** Ring buffer struct */
typedef struct _ring_buffer {
	/** Buffer data */
	int16_t *data;
	/** Buffer maximum capacity */
	unsigned long capacity;
	/** Buffer's current size */
	unsigned long size;
	/** Write position */
	unsigned long head;
	/** Read position */
	unsigned long tail;
	/** Mutex */
	sem_t lock;
	/** Empty semaphore */
	sem_t empty;
	/** Full semaphore */
	sem_t full;
} ringbuffer_t;

int rbuff_init(ringbuffer_t *buffer, unsigned long capacity);
void rbuff_destroy(ringbuffer_t *buffer);
int rbuff_put(ringbuffer_t *buffer, int16_t element);
int16_t rbuff_get(ringbuffer_t *buffer);

#endif /* __RINGBUFFER_H__ */
