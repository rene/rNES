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
 * @file ringbuffer.c
 *
 * A simple implementation of a ring buffer (circular buffer)
 */

#include "ringbuffer.h"

#ifdef _WIN32
static inline int s_init(sem_t *s, unsigned int value)
{
	*s = CreateSemaphore(NULL, value, LONG_MAX, NULL);
	if (s == NULL)
		return -1;
	else
		return 0;
};
static inline int s_lock(sem_t *s)
{
	DWORD ret = WaitForSingleObject(*s, INFINITE);
	if (ret == WAIT_OBJECT_0)
		return 0;
	else
		return -1;
};
static inline int s_unlock(sem_t *s)
{
	if (ReleaseSemaphore(*s, 1, NULL) == 0)
		return 0;
	else
		return -1;
};
static inline int s_destroy(sem_t *s)
{
	if (CloseHandle(*s) == 0)
		return 0;
	else
		return -1;
};
#elif defined(_POSIX_C_SOURCE)
/** Initialize semaphore */
static inline int s_init(sem_t *sem, unsigned int value)
{
	return sem_init(sem, 0, value);
}
/** Destroy semaphore */
static inline int s_destroy(sem_t *sem) { return sem_destroy(sem); };
/** Semaphore DOWN function */
static inline int s_lock(sem_t *sem) { return sem_wait(sem); };
/** Semaphore UP function */
static inline int s_unlock(sem_t *sem) { return sem_post(sem); };
#else
/** Initialize semaphore */
static inline int s_init(sem_t *sem, unsigned int value)
{
	unsigned int *s = (unsigned int *)sem;

	*s = value;
	return 0;
};
/** Destroy semaphore */
static inline int s_destroy(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;

	*s = 0;
	return 0;
};
/** Semaphore DOWN function */
static inline int s_lock(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;
	int attempts = 1000;

	while (*s == 0 && attempts-- > 0)
		;

	*s = *s - 1;
	return 0;
};
/** Semaphore UP function */
static inline int s_unlock(sem_t *sem)
{
	unsigned int *s = (unsigned int *)sem;

	*s = *s + 1;
	return 0;
};
#endif

/**
 * Initialize a new ring buffer
 * @param [in,out] buffer Ring buffer
 * @param capacity Size of the Ring buffer (maximum number of elements)
 * @return 0 on success, error code otherwise
 */
int rbuff_init(ringbuffer_t *buffer, unsigned long capacity)
{
	int ret;

	if (buffer == NULL)
		return -EINVAL;

	buffer->data = (int16_t *)malloc(sizeof(int16_t) * capacity);
	if (buffer->data == NULL)
		return -ENOMEM;

	ret = s_init(&buffer->lock, 1);
	if (ret < 0) {
		free(buffer->data);
		return ret;
	}

	ret = s_init(&buffer->empty, capacity);
	if (ret < 0) {
		free(buffer->data);
		return ret;
	}

	ret = s_init(&buffer->full, 0);
	if (ret < 0) {
		free(buffer->data);
		return ret;
	}
	buffer->capacity = capacity;
	buffer->size = 0;
	buffer->head = 0;
	buffer->tail = 0;
	return 0;
}

/**
 * Destroy an existent ring buffer
 * @param [in,out] buffer Ring buffer
 */
void rbuff_destroy(ringbuffer_t *buffer)
{
	buffer->capacity = 0;
	buffer->size = 0;
	buffer->head = 0;
	buffer->tail = 0;

	free(buffer->data);
	buffer->data = NULL;
	s_destroy(&buffer->lock);
	s_destroy(&buffer->empty);
	s_destroy(&buffer->full);
}

/**
 * Push a new element to a ring buffer
 * @param [in,out] buffer Ring buffer
 * @param element The new element
 * @return 0 on success, error code otherwise
 */
int rbuff_put(ringbuffer_t *buffer, int16_t element)
{
	if (buffer == NULL)
		return -EINVAL;

	s_lock(&buffer->empty);
	s_lock(&buffer->lock);
	buffer->data[buffer->head] = element;
	buffer->head = (buffer->head + 1) % buffer->capacity;
	buffer->size++;
	s_unlock(&buffer->lock);
	s_unlock(&buffer->full);
	return 0;
}

/**
 * Get an element from a ring buffer
 * @param [in,out] buffer Ring buffer
 * @return element The element
 */
int16_t rbuff_get(ringbuffer_t *buffer)
{
	int16_t data = 0;

	if (buffer == NULL)
		return -EINVAL;

	s_lock(&buffer->full);
	s_lock(&buffer->lock);
	data = buffer->data[buffer->tail];
	buffer->data[buffer->tail] = 0;
	if (++(buffer->tail) == buffer->capacity)
		buffer->tail = 0;
	buffer->size--;
	s_unlock(&buffer->lock);
	s_unlock(&buffer->empty);

	return data;
}
