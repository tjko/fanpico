/* flash.c
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
#include <malloc.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico_hal.h"

#include "fanpico.h"


int flash_read_file(char **bufptr, uint32_t *sizeptr, const char *filename, int init_flash)
{
	int res, fd;

	if (!bufptr || !sizeptr || !filename)
		return -42;

	*bufptr = NULL;
	*sizeptr = 0;

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		log_msg(LOG_NOTICE, "pico_mount() failed: %d (%s)", res, pico_errmsg(res));
		if (init_flash) {
			log_msg(LOG_NOTICE, "Trying to initialize a new filesystem...");
			if ((res = pico_mount(true)) < 0) {
				log_msg(LOG_ERR, "Unable to initialize flash filesystem!");
				return -2;
			}
			log_msg(LOG_NOTICE, "Filesystem successfully initialized. (%d)", res);
			pico_unmount();
		}
		return -1;
	}
	log_msg(LOG_DEBUG, "Filesystem mounted OK");

	res = 0;

	/* Open file */
	if ((fd = pico_open(filename, LFS_O_RDONLY)) < 0) {
		log_msg(LOG_NOTICE, "Cannot open file \"%s\": %d (%s)",
			filename, fd, pico_errmsg(fd));
		res = -3;
	} else {
		/* Check file size */
		uint32_t file_size = pico_size(fd);
		log_msg(LOG_INFO, "File \"%s\" opened ok: %li bytes", filename, file_size);
		if (file_size > 0) {
			*bufptr = malloc(file_size);
			if (!*bufptr) {
				log_msg(LOG_ALERT, "Not enough memory to read file \"%s\": %lu",
					filename, file_size);
				res = -4;
			} else {
				/* Read file... */
				log_msg(LOG_INFO, "Reading file \"%s\"...", filename);
				*sizeptr = pico_read(fd, *bufptr, file_size);
				if (*sizeptr < file_size) {
					log_msg(LOG_ERR, "Error reading file \"%s\": %lu",
						filename, *sizeptr);
					res = -5;
				}
			}
		}
		pico_close(fd);
	}
	pico_unmount();

	return res;
}


int flash_write_file(const char *buf, uint32_t size, const char *filename)
{
	int res, fd;

	if (!buf || !filename)
		return -42;

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		log_msg(LOG_ERR, "pico_mount() failed: %d (%s)", res, pico_errmsg(res));
		return -1;
	}

	/* Create file */
	if ((fd = pico_open(filename, LFS_O_WRONLY | LFS_O_CREAT)) < 0) {
		log_msg(LOG_ERR, "Failed to create file \"%s\": %d (%s)",
			filename, fd, pico_errmsg(fd));
		res = -2;
	} else {
		/* Write to file */
		lfs_size_t wrote = pico_write(fd, buf, size);
		if (wrote < size) {
			log_msg(LOG_ERR, "Failed to write to file \"%s\": %li",
				filename, wrote);
			res = -3;
		} else {
			log_msg(LOG_NOTICE, "File \"%s\" sucfessfully created: %li bytes",
				filename, wrote);
			res = 0;
		}
		pico_close(fd);
	}

	/* Unmount flash filesystem */
	pico_unmount();

	return res;
}


int flash_delete_file(const char *filename)
{
	int ret = 0;
	int res;
	struct lfs_info stat;

	if (!filename)
		return -42;

	/* Mount flash filesystem... */
	if ((res = pico_mount(false)) < 0) {
		log_msg(LOG_ERR, "pico_mount() failed: %d (%s)", res, pico_errmsg(res));
		return -1;
	}

	/* Check if file exists... */
	if ((res = pico_stat(filename, &stat)) < 0) {
		log_msg(LOG_ERR, "File \"%s\" not found: %d (%s)",
			filename, res, pico_errmsg(res));
		ret = -2;
	} else {
		/* Remove configuration file...*/
		log_msg(LOG_NOTICE, "Removing file \"%s\" (%lu bytes)",
			filename, stat.size);
		if ((res = pico_remove(filename)) < 0) {
			log_msg(LOG_ERR, "Failed to remove file \"%s\": %d (%s)",
				filename, res, pico_errmsg(res));
			ret = -3;
		}
	}
	pico_unmount();

	return ret;
}

