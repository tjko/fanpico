/* ringbuffer.c
   Copyright (C) 2025 Timo Kokkonen <tjko@iki.fi>

   SPDX-License-Identifier: GPL-3.0-or-later

   This file is part of FanPico.

   FanPico is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   FanPico is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with FanPico. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdio.h>

#include "ssh/ssh_server.h"


int ssh_ringbuffer_init(ssh_ringbuffer_t *rb, void *buf, size_t size)
{
	if (!rb)
		return -1;

	if (!buf) {
		if (!(rb->buf = calloc(1, size)))
			return -2;
		rb->free_buf = true;
	} else {
		rb->buf = buf;
		rb->free_buf = false;
	}

	rb->size = size;
	rb->free = size;
	rb->head = 0;
	rb->tail = 0;

	return 0;
}


int ssh_ringbuffer_destroy(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;

	if (rb->free_buf && rb->buf)
		free(rb->buf);

	rb->buf = NULL;
	rb->size = 0;
	rb->free = 0;
	rb->head = 0;
	rb->tail = 0;

	return 0;
}


int ssh_ringbuffer_flush(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;

	rb->head = 0;
	rb->tail = 0;
	rb->free = rb->size;

	return 0;
}


inline size_t ssh_ringbuffer_used(const ssh_ringbuffer_t *rb)
{
	return (rb ? rb->size - rb->free : 0);
}


inline size_t ssh_ringbuffer_free(const ssh_ringbuffer_t *rb)
{
	return (rb ? rb->free : 0);
}


inline size_t ssh_ringbuffer_offset(ssh_ringbuffer_t *rb, size_t offset, size_t delta, int direction)
{
	size_t o = offset % rb->size;
	size_t d = delta % rb->size;

	if (!rb)
		return 0;
	if (d == 0)
		return o;

	if (direction >= 0) {
		o = (o + d) % rb->size;
	} else {
		if (o < d) {
			o = rb->size - (d - o);
		} else {
			o -= d;
		}
	}

	return o;
}


int ssh_ringbuffer_add(ssh_ringbuffer_t *rb, const void *data, size_t len, bool overwrite)
{
	if (!rb || !data)
		return -1;

	if (len == 0)
		return 0;

	if (len > rb->size)
		return -2;

	if (overwrite && rb->free < len) {
		size_t needed = len - rb->free;
		rb->head = ssh_ringbuffer_offset(rb, rb->head, needed, 1);
		rb->free += needed;
	}
	if (rb->free < len)
		return -3;

	size_t new_tail = ssh_ringbuffer_offset(rb, rb->tail, len, 1);

	if (new_tail > rb->tail) {
		memcpy(rb->buf + rb->tail, data, len);
	} else {
		size_t part1 = rb->size - rb->tail;
		size_t part2 = len - part1;
		memcpy(rb->buf + rb->tail, data, part1);
		memcpy(rb->buf, data + part1, part2);
	}

	rb->tail = new_tail;
	rb->free -= len;

	return 0;
}


int ssh_ringbuffer_read(ssh_ringbuffer_t *rb, uint8_t *ptr, size_t size)
{
	if (!rb || size < 1)
		return -1;

	size_t used = rb->size - rb->free;
	if (used < size)
		return -2;

	size_t new_head = ssh_ringbuffer_offset(rb, rb->head, size, 1);

	if (ptr) {
		if (new_head > rb->head) {
			memcpy(ptr, rb->buf + rb->head, size);
		} else {
			size_t part1 = rb->size - rb->head;
			size_t part2 = size - part1;
			memcpy(ptr, rb->buf + rb->head, part1);
			memcpy(ptr + part1, rb->buf, part2);
		}
	}

	rb->head = new_head;
	rb->free += size;

	return 0;
}


inline int ssh_ringbuffer_read_char(ssh_ringbuffer_t *rb)
{
	if (!rb)
		return -1;
	if (rb->head == rb->tail)
		return -2;

	int val = rb->buf[rb->head];
	rb->head = ssh_ringbuffer_offset(rb, rb->head, 1, 1);
	rb->free++;

	return val;
}


inline int ssh_ringbuffer_add_char(ssh_ringbuffer_t *rb, uint8_t ch, bool overwrite)
{
	if (!rb)
		return -1;

	if (overwrite && rb->free < 1) {
		rb->head = ssh_ringbuffer_offset(rb, rb->head, 1, 1);
		rb->free += 1;
	}
	if (rb->free < 1)
		return -2;

	rb->buf[rb->tail] = ch;
	rb->tail = ssh_ringbuffer_offset(rb, rb->tail, 1, 1);
	rb->free--;

	return 0;
}


size_t ssh_ringbuffer_peek(ssh_ringbuffer_t *rb, void **ptr, size_t size)
{
	if (!rb || !ptr || size < 1)
		return 0;

	*ptr = NULL;
	size_t used = rb->size - rb->free;
	size_t toread = (size < used ? size : used);
	size_t len = rb->size - rb->head;

	if (used < 1)
		return 0;

	*ptr = rb->buf + rb->head;

	return (len < toread ? len : toread);
}

