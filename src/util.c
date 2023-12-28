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


const char *mac_address_str(const uint8_t *mac)
{
	static char buf[32];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
		mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return buf;
}


int valid_wifi_country(const char *country)
{
	if (!country)
		return 0;

	if (strlen(country) < 2)
		return 0;

	if (!(country[0] >= 'A' && country[0] <= 'Z'))
		return 0;
	if (!(country[1] >= 'A' && country[1] <= 'Z'))
		return 0;

	if (strlen(country) == 2)
		return 1;

	if (country[2] >= '1' && country[2] <= '9')
		return 1;

	return 0;
}


int check_for_change(double oldval, double newval, double threshold)
{
	double delta = fabs(oldval - newval);

	if (delta >= threshold)
		return 1;

	return 0;
}


#define SQRT_INT64_MAX 3037000499  // sqrt(INT64_MAX)

int64_t pow_i64(int64_t x, uint8_t y)
{
	int64_t res;

	/* Handle edge cases. */
	if (x == 1 || y == 0)
		return 1;
	if (x == 0)
		return 0;

	/* Calculate x^y by squaring method. */
	res = (y & 1 ? x : 1);
	y >>= 1;
	while (y) {
		/* Check for overflow. */
		if (x > SQRT_INT64_MAX)
			return 0;
		x *= x;
		if (y & 1)
			res *= x; /* This may still overflow... */
		y >>= 1;
	}

	return res;
}


double round_decimal(double val, unsigned int decimal)
{
	double f = pow_i64(10, decimal);
	return round(val * f) / f;
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


int clamp_int(int val, int min, int max)
{
	int res = val;

	if (res < min)
		res = min;
	if (res > max)
		res = max;

	return res;
}


void* memmem(const void *haystack, size_t haystacklen,
	const void *needle, size_t needlelen)
{
	const uint8_t *h = (const uint8_t *) haystack;
	const uint8_t *n = (const uint8_t *) needle;

	if (haystacklen == 0 || needlelen == 0 || haystacklen < needlelen)
		return NULL;

	if (needlelen == 1)
		return memchr(haystack, (int)n[0], haystacklen);

	const uint8_t *search_end = h + haystacklen - needlelen;

	for (const uint8_t *p = h; p <= search_end; p++) {
		if (*p == *n) {
			if (!memcmp(p, n, needlelen)) {
				return (void *)p;
			}
		}
	}

	return NULL;
}


/* eof */
