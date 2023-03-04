/* log.c
   Copyright (C) 2021-2023 Timo Kokkonen <tjko@iki.fi>

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
int global_log_level = LOG_ERR;
int global_syslog_level = LOG_ERR;
auto_init_mutex(log_mutex);

struct log_priority {
	uint8_t  priority;
	const char *name;
};

struct log_facility {
	uint8_t facility;
	const char *name;
};

struct log_priority log_priorities[] = {
	{ LOG_EMERG,   "EMERG" },
	{ LOG_ALERT,   "ALERT" },
	{ LOG_CRIT,    "CRIT" },
	{ LOG_ERR,     "ERR" },
	{ LOG_WARNING, "WARNING" },
	{ LOG_NOTICE,  "NOTICE" },
	{ LOG_INFO,    "INFO" },
	{ LOG_DEBUG,   "DEBUG" },
	{ 0, NULL }
};

struct log_facility log_facilities[] = {
	{ LOG_KERN,     "KERN" },
	{ LOG_USER,     "USER" },
	{ LOG_MAIL,     "MAIL" },
	{ LOG_DAEMON,   "DAEMON" },
	{ LOG_AUTH,     "AUTH" },
	{ LOG_SYSLOG,   "SYSLOG" },
	{ LOG_LPR,      "LPR" },
	{ LOG_NEWS,     "NEWS" },
	{ LOG_UUCP,     "UUCP" },
	{ LOG_CRON,     "CRON" },
	{ LOG_AUTHPRIV, "AUTHPRIV" },
	{ LOG_FTP,      "FTP" },
	{ LOG_LOCAL0,   "LOCAL0" },
	{ LOG_LOCAL1,   "LOCAL1" },
	{ LOG_LOCAL2,   "LOCAL2" },
	{ LOG_LOCAL3,   "LOCAL3" },
	{ LOG_LOCAL4,   "LOCAL4" },
	{ LOG_LOCAL5,   "LOCAL5" },
	{ LOG_LOCAL6,   "LOCAL6" },
	{ LOG_LOCAL7,   "LOCAL7" },
	{ 0, NULL }
};


int str2log_priority(const char *pri)
{
	int i = 0;

	if (!pri)
		return -2;

	while(log_priorities[i].name) {
		if (!strcasecmp(log_priorities[i].name, pri))
			return log_priorities[i].priority;
		i++;
	}

	return -1;
}

const char* log_priority2str(int pri)
{
	int i = 0;

	while(log_priorities[i].name) {
		if (log_priorities[i].priority == pri)
			return log_priorities[i].name;
		i++;
	}

	return NULL;
}

int str2log_facility(const char *facility)
{
	int i = 0;

	if (!facility)
		return -2;

	while(log_facilities[i].name) {
		if (!strcasecmp(log_facilities[i].name, facility))
			return log_facilities[i].facility;
		i++;
	}

	return -1;
}

const char* log_facility2str(int facility)
{
	int i = 0;

	while(log_facilities[i].name) {
		if (log_facilities[i].facility == facility)
			return log_facilities[i].name;
		i++;
	}

	return NULL;
}



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


/* eof */
