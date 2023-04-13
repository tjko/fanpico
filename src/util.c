/* util.c
   Copyright (C) 2021-2022 Timo Kokkonen <tjko@iki.fi>

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
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <wctype.h>
#include <assert.h>
#include <malloc.h>
#include <time.h>
#include "pico/stdlib.h"
#include "pico/mutex.h"
#include "pico/unique_id.h"
#include "pico/util/datetime.h"
#include "hardware/watchdog.h"
#include "b64/cencode.h"
#include "b64/cdecode.h"

#include "fanpico.h"


void print_mallinfo()
{
	struct mallinfo mi = mallinfo();

	printf("Total non-mmapped bytes (arena):       %d\n", mi.arena);
	printf("# of free chunks (ordblks):            %d\n", mi.ordblks);
	printf("# of free fastbin blocks (smblks):     %d\n", mi.smblks);
	printf("# of mapped regions (hblks):           %d\n", mi.hblks);
	printf("Bytes in mapped regions (hblkhd):      %d\n", mi.hblkhd);
	printf("Max. total allocated space (usmblks):  %d\n", mi.usmblks);
	printf("Free bytes held in fastbins (fsmblks): %d\n", mi.fsmblks);
	printf("Total allocated space (uordblks):      %d\n", mi.uordblks);
	printf("Total free space (fordblks):           %d\n", mi.fordblks);
	printf("Topmost releasable block (keepcost):   %d\n", mi.keepcost);
}

void print_irqinfo()
{
	uint core = get_core_num();
	uint enabled, shared;

	for(uint i = 0; i < 32; i++) {
		enabled = irq_is_enabled(i);
		shared = (enabled ? irq_has_shared_handler(i) : 0);
		printf("core%u: IRQ%-2u: enabled=%u, shared=%u\n", core, i, enabled, shared);
	}
}

char *trim_str(char *s)
{
	char *e;

	if (!s) return NULL;

	while (iswspace(*s))
		s++;
	e = s + strlen(s) - 1;
	while (e >= s && iswspace(*e))
		*e-- = 0;

	return s;
}


int str_to_int(const char *str, int *val, int base)
{
	char *endptr;

	if (!str || !val)
		return 0;

	*val = strtol(str, &endptr, base);

	return (str == endptr ? 0 : 1);
}


int str_to_float(const char *str, float *val)
{
	char *endptr;

	if (!str || !val)
		return 0;

	*val = strtof(str, &endptr);

	return (str == endptr ? 0 : 1);
}

#define GET_DIGITS(buf, n, s, dest) {		\
		count = 0;			\
		while (count < n) {			\
			if (*s >= '0' && *s <= '9') {	\
				buf[count++] = *s++;	\
			} else {			\
				return 0;		\
			}				\
		}					\
		buf[count] = 0;				\
		int val;				\
		if (!str_to_int(buf, &val, 10))		\
			return 0;			\
		dest = val;				\
	}

int str_to_datetime(const char *str, datetime_t *t)
{
	const char *s = str;
	char buf[8];
	int count;

	if (!str || !t)
		return 0;

	/* Expecting string in format: "YYYY-MM-DD HH:MM:SS" */

	/* Year */
	GET_DIGITS(buf, 4, s, t->year);
	if (*s++ != '-')
		return 0;
	/* Month */
	GET_DIGITS(buf, 2, s, t->month);
	if (*s++ != '-')
		return 0;
	/* Day */
	GET_DIGITS(buf, 2, s, t->day);
	if (*s++ != ' ')
		return 0;
	/* Hour */
	GET_DIGITS(buf, 2, s, t->hour);
	if (*s++ != ':')
		return 0;
	/* Minutes */
	GET_DIGITS(buf, 2, s, t->min);
	if (*s++ != ':')
		return 0;
	/* Seconds */
	GET_DIGITS(buf, 2, s, t->sec);

	/* No support for day of the week. */
	t->dotw = 0;

	return 1;
}

const char *rp2040_model_str()
{
	static char buf[32];
	uint8_t version = 0;
	uint8_t known_chip = 0;
	uint8_t chip_version = rp2040_chip_version();
	uint8_t rom_version = rp2040_rom_version();


	if (chip_version <= 2 && rom_version <= 3)
		known_chip = 1;

	version = rom_version - 1;

	snprintf(buf, sizeof(buf), "RP2040-B%d%s",
		version,
		(known_chip ? "" : " (?)"));

	return buf;
}


const char *pico_serial_str()
{
	static char buf[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
	pico_unique_board_id_t board_id;

	memset(&board_id, 0, sizeof(board_id));
	pico_get_unique_board_id(&board_id);
	for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
		snprintf(&buf[i*2], 3,"%02x", board_id.id[i]);

	return buf;
}


const char *mac_address_str(const uint8_t *mac)
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return buf;
}

int check_for_change(double oldval, double newval, double threshold)
{
	double delta = fabs(oldval - newval);

	if (delta >= threshold)
		return 1;

	return 0;
}


int time_passed(absolute_time_t *t, uint32_t ms)
{
	absolute_time_t t_now = get_absolute_time();

	if (t == NULL)
		return -1;

	if (to_us_since_boot(*t) == 0 ||
	    to_us_since_boot(delayed_by_ms(*t, ms)) < to_us_since_boot(t_now)) {
		*t = t_now;
		return 1;
	}

	return 0;
}


char* base64encode(const char *input)
{
	base64_encodestate ctx;
	size_t buf_len, input_len, count;
	char *buf, *c;

	if (!input)
		return NULL;

	base64_init_encodestate(&ctx);

	input_len = strlen(input);
	buf_len = base64_encode_length(input_len, &ctx);
	if (!(buf = malloc(buf_len + 1)))
		return NULL;

	c = buf;
	count = base64_encode_block(input, input_len, c, &ctx);
	c += count;
	count = base64_encode_blockend(c, &ctx);
	c += count;
	*c =  0;

	return buf;
}


char* base64decode(const char *input)
{
	base64_decodestate ctx;
	size_t buf_len, input_len, count;
	char *buf, *c;

	if (!input)
		return NULL;

	base64_init_decodestate(&ctx);

	input_len = strlen(input);
	buf_len = base64_decode_maxlength(input_len);
	if (!(buf = malloc(buf_len + 1)))
		return NULL;

	c = buf;
	count = base64_decode_block(input, input_len, c, &ctx);
	c += count;
	*c = 0;

	return buf;
}


char *strncopy(char *dst, const char *src, size_t size)
{
	if (!dst || !src || size < 1)
		return dst;

	if (size > 1)
		strncpy(dst, src, size - 1);
	dst[size - 1] = 0;

	return dst;
}


char *strncatenate(char *dst, const char *src, size_t size)
{
	int used, free;

	if (!dst || !src || size < 1)
		return dst;

	/* Check if dst string is already "full" ... */
	used = strnlen(dst, size);
	if ((free = size - used) <= 1)
		return dst;

	return strncat(dst + used, src, free - 1);
}


datetime_t *tm_to_datetime(const struct tm *tm, datetime_t *t)
{
	if (!tm || !t)
		return NULL;

	t->year = tm->tm_year + 1900;
	t->month = tm->tm_mon + 1;
	t->day = tm->tm_mday;
	t->dotw = tm->tm_wday;
	t->hour = tm->tm_hour;
	t->min = tm->tm_min;
	t->sec = tm->tm_sec;

	return t;
}


struct tm *datetime_to_tm(const datetime_t *t, struct tm *tm)
{
	if (!t || !tm)
		return NULL;

	tm->tm_year = t->year - 1900;
	tm->tm_mon = t->month -1;
	tm->tm_mday = t->day;
	tm->tm_wday = t->dotw;
	tm->tm_hour = t->hour;
	tm->tm_min = t->min;
	tm->tm_sec = t->sec;
	tm->tm_isdst = -1;

	return tm;
}


time_t datetime_to_time(const datetime_t *datetime)
{
	struct tm tm;

	datetime_to_tm(datetime, &tm);
	return mktime(&tm);
}


void watchdog_disable()
{
	hw_clear_bits(&watchdog_hw->ctrl, WATCHDOG_CTRL_ENABLE_BITS);
}


int getstring_timeout_ms(char *str, uint32_t maxlen, uint32_t timeout)
{
	absolute_time_t t_timeout = get_absolute_time();
	char *p;
	int res = 0;
	int len;

	if (!str || maxlen < 2)
		return -1;

	len = strnlen(str, maxlen);
	if (len >= maxlen)
		return -2;
	p = str + len;

	while ((p - str) < (maxlen - 1) ) {
		if (time_passed(&t_timeout, timeout)) {
			break;
		}
		int c = getchar_timeout_us(1);
		if (c == PICO_ERROR_TIMEOUT)
			continue;
		if (c == 10 || c == 13) {
			res = 1;
			break;
		}
		*p++ = c;
	}
	*p = 0;

	return res;
}

/* eof */
