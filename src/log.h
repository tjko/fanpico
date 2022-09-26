/* log.h
   Copyright (C) 2022 Timo Kokkonen <tjko@iki.fi>

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

#ifndef _FANPICO_LOG_H_
#define _FANPICO_LOG_H_

/* Syslog Priorities */

#define LOG_EMERG     0
#define LOG_ALERT     1
#define LOG_CRIT      2
#define LOG_ERR       3
#define LOG_WARNING   4
#define LOG_NOTICE    5
#define LOG_INFO      6
#define LOG_DEBUG     7


/* Syslog Facilities */
#define LOG_KERN      0
#define LOG_USER      1
#define LOG_MAIL      2
#define LOG_DAEMON    3
#define LOG_AUTH      4
#define LOG_SYSLOG    5
#define LOG_LPR       6
#define LOG_NEWS      7
#define LOG_UUCP      8
#define LOG_CRON      9
#define LOG_AUTHPRIV  10
#define LOG_FTP       11
#define LOG_LOCAL0    16
#define LOG_LOCAL1    17
#define LOG_LOCAL2    18
#define LOG_LOCAL3    19
#define LOG_LOCAL4    20
#define LOG_LOCAL5    21
#define LOG_LOCAL6    22
#define LOG_LOCAL7    23


#endif /* _FANPICO_LOG_H_ */
