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
#ifdef WIFI_SUPPORT
#include "syslog.h"
#endif

int global_debug_level = 0;
int global_log_level = LOG_NOTICE;
int global_syslog_level = LOG_ERR;

auto_init_mutex(log_mutex);


int get_log_level()
{
	return global_log_level;
}

void set_log_level(int level)
{
	global_log_level = level;
}

int get_syslog_level()
{
	return global_syslog_level;
}

void set_syslog_level(int level)
{
	global_syslog_level = level;
}

int get_debug_level()
{
	return global_debug_level;
}

void set_debug_level(int level)
{
	global_debug_level = level;
}


void log_msg(int priority, const char *format, ...)
{
	va_list ap;
	char buf[256];
	int len;

	if ((priority > global_log_level) && (priority > global_syslog_level))
		return;


	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if ((len = strlen(buf)) > 0) {
		/* If string ends with \n, remove it. */
		if (buf[len - 1] == '\n')
			buf[len - 1] = 0;
	}

	mutex_enter_blocking(&log_mutex);
	if (priority <= global_log_level) {
		uint64_t t = to_us_since_boot(get_absolute_time());
		printf("[%6llu.%06llu] %s\n", (t / 1000000), (t % 1000000), buf);
	}
#ifdef WIFI_SUPPORT
	if (priority <= global_syslog_level) {
		syslog_msg(priority, "%s", buf);
	}
#endif
	mutex_exit(&log_mutex);
}

void debug(int debug_level, const char *fmt, ...)
{
	va_list ap;

	if (debug_level > global_debug_level)
		return;

	printf("[DEBUG] ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}


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


int time_passed(absolute_time_t *t, uint32_t us)
{
	absolute_time_t t_now = get_absolute_time();

	if (t == NULL)
		return -1;

	if (to_us_since_boot(*t) == 0 ||
	    to_us_since_boot(delayed_by_ms(*t, us)) < to_us_since_boot(t_now)) {
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

/* eof */
