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
#include "pico/mutex.h"
#include "pico_lfs.h"

#include "fanpico.h"

extern char __flash_binary_start;
extern char __flash_binary_end;

static struct lfs_config *lfs_cfg = NULL;
static lfs_t lfs;


void lfs_setup(bool multicore)
{
	int err;

	//printf("lfs_setup\n");
	lfs_cfg = pico_lfs_init(FANPICO_FS_OFFSET, FANPICO_FS_SIZE);
	if (!lfs_cfg)
		panic("lfs_setup: not enough memory!");

	/* Check if we need to initialize/format filesystem... */
	err = lfs_mount(&lfs, lfs_cfg);
	if (err != LFS_ERR_OK) {
		log_msg(LOG_ERR, "Trying to initialize a new filesystem...");
		if (flash_format(false))
			return;
		log_msg(LOG_ERR, "Filesystem successfully initialized.");
	} else {
		lfs_unmount(&lfs);
	}
}

int flash_format(bool multicore)
{
	struct pico_lfs_context *ctx = (struct pico_lfs_context*)lfs_cfg;
	int err;
	bool saved;

	saved = ctx->multicore_lockout_enabled;
	ctx->multicore_lockout_enabled = (multicore ? true : false);

	if ((err = lfs_format(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "Unable to format flash filesystem: %d", err);
	}

	ctx->multicore_lockout_enabled = saved;

	return  (err == LFS_ERR_OK ? 0 : 1);
}

int flash_read_file(char **bufptr, uint32_t *sizeptr, const char *filename)
{
	lfs_file_t lfs_file;
	int res;

	if (!bufptr || !sizeptr || !filename)
		return -42;

	*bufptr = NULL;
	*sizeptr = 0;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_NOTICE, "lfs_mount() failed: %d", res);
		return -1;
	}
	log_msg(LOG_DEBUG, "Filesystem mounted OK");

	res = 0;

	/* Open file */
	if ((res = lfs_file_open(&lfs, &lfs_file, filename, LFS_O_RDONLY)) != LFS_ERR_OK) {
		log_msg(LOG_DEBUG, "Cannot open file \"%s\": %d", filename, res);
		res = -3;
	} else {
		/* Check file size */
		uint32_t file_size = lfs_file_size(&lfs, &lfs_file);
		log_msg(LOG_DEBUG, "File \"%s\" opened ok: %u bytes", filename, file_size);
		if (file_size > 0) {
			*bufptr = malloc(file_size);
			if (!*bufptr) {
				log_msg(LOG_ALERT, "Not enough memory to read file \"%s\": %lu",
					filename, file_size);
				res = -4;
			} else {
				/* Read file... */
				log_msg(LOG_DEBUG, "Reading file \"%s\"...", filename);
				*sizeptr = lfs_file_read(&lfs, &lfs_file, *bufptr, file_size);
				if (*sizeptr < file_size) {
					log_msg(LOG_ERR, "Error reading file \"%s\": %lu",
						filename, *sizeptr);
					res = -5;
				}
			}
		}
		lfs_file_close(&lfs, &lfs_file);
	}
	lfs_unmount(&lfs);

	return res;
}


int flash_write_file(const char *buf, uint32_t size, const char *filename)
{
	lfs_file_t lfs_file;
	int res;

	if (!buf || !filename)
		return -42;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -1;
	}

	/* Create file */
	if ((res = lfs_file_open(&lfs, &lfs_file, filename, LFS_O_WRONLY | LFS_O_CREAT)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "Failed to create file \"%s\": %d", filename, res);
		res = -2;
	} else {
		/* Write to file */
		lfs_size_t wrote = lfs_file_write(&lfs, &lfs_file, buf, size);
		if (wrote < size) {
			log_msg(LOG_ERR, "Failed to write to file \"%s\": %li",
				filename, wrote);
			res = -3;
		} else {
			log_msg(LOG_INFO, "File \"%s\" successfully created: %li bytes",
				filename, wrote);
			res = 0;
		}
		lfs_file_close(&lfs, &lfs_file);
	}

	/* Unmount flash filesystem */
	lfs_unmount(&lfs);

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
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -1;
	}

	/* Check if file exists... */
	if ((res = lfs_stat(&lfs, filename, &stat)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "File \"%s\" not found: %d", filename, res);
		ret = -2;
	} else {
		/* Remove the file...*/
		log_msg(LOG_INFO, "Removing file \"%s\" (%lu bytes)",
			filename, stat.size);
		if ((res = lfs_remove(&lfs, filename)) != LFS_ERR_OK) {
			log_msg(LOG_ERR, "Failed to remove file \"%s\": %d", filename, res);
			ret = -3;
		}
	}
	lfs_unmount(&lfs);

	return ret;
}


int flash_rename_file(const char *oldname, const char *newname)
{
	int ret = 0;
	int res;
	struct lfs_info stat;

	if (!oldname || !newname)
		return -42;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -1;
	}

	/* Check if file exists... */
	if ((res = lfs_stat(&lfs, oldname, &stat)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "File \"%s\" not found: %d", oldname, res);
		ret = -2;
	} else {
		/* Rename file...*/
		log_msg(LOG_INFO, "Renaming file \"%s\" --> \"%s\"",
			oldname, newname);
		if ((res = lfs_rename(&lfs, oldname, newname)) != LFS_ERR_OK) {
			log_msg(LOG_ERR, "Failed to rename file \"%s\": %d", oldname, res);
			ret = -3;
		}
	}
	lfs_unmount(&lfs);

	return ret;
}


int flash_copy_file(const char *srcname, const char *dstname, bool overwrite)
{
	const size_t buf_size = 4096;
	char *buf = NULL;
	lfs_file_t f_in;
	lfs_file_t f_out;
	int ret = 0;
	int res;
	uint32_t file_size;
	uint32_t bytes_written = 0;


	if (!srcname || !dstname)
		return -42;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -1;
	}

	if (!(buf = malloc(buf_size))) {
		log_msg(LOG_ERR, "Not enough memory.");
		ret = -2;
	}

	/* Open source file... */
	if (ret == 0) {
		if ((res = lfs_file_open(&lfs, &f_in, srcname, LFS_O_RDONLY)) == LFS_ERR_OK) {
			file_size = lfs_file_size(&lfs, &f_in);
		} else {
			log_msg(LOG_ERR, "Cannot open source file \"%s\": %d", srcname, res);
			ret = -2;
		}
	}

	/* Create destination file... */
	if (ret == 0) {
		int flags = LFS_O_WRONLY | LFS_O_CREAT;

		if (!overwrite)
			flags |= LFS_O_EXCL;

		if ((res = lfs_file_open(&lfs, &f_out, dstname, flags)) != LFS_ERR_OK) {
			log_msg(LOG_ERR, "Cannot create destination file \"%s\": %d", srcname, res);
			lfs_file_close(&lfs, &f_out);
			ret = -3;
		}
	}

	/* Copy file...*/
	if (ret == 0) {
		int32_t bytes_read, wrote;

		do {
			if ((bytes_read = lfs_file_read(&lfs, &f_in, buf, buf_size)) < 0) {
				log_msg(LOG_ERR, "Failed to read input file: %d", bytes_read);
				ret = -4;
				break;
			}
			if (bytes_read > 0) {
				wrote = lfs_file_write(&lfs, &f_out, buf, bytes_read);
				if (wrote < bytes_read) {
					log_msg(LOG_ERR, "Failed to write to target file: %d", wrote);
					ret = -5;
					break;
				}
				bytes_written += wrote;
			}
		} while (bytes_read > 0);

		lfs_file_close(&lfs, &f_in);
		lfs_file_close(&lfs, &f_out);

		if (ret == 0 && bytes_written < file_size) {
			log_msg(LOG_ERR, "Failed to copy entire file.");
			ret = -6;
		} else {
			log_msg(LOG_INFO, "Copied \"%s\" --> \"%s\" (%lu bytes)",
				srcname, dstname, bytes_written);
		}
	}

	if (buf)
		free(buf);
	lfs_unmount(&lfs);

	return ret;
}


int flash_file_size(const char *filename)
{
	struct lfs_info stat;
	int res;

	if (!filename)
		return -42;


	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -1;
	}

	/* Check if file exists... */
	if ((res = lfs_stat(&lfs, filename, &stat)) != LFS_ERR_OK) {
		res = -2;
	} else {
		res = stat.size;
	}
	lfs_unmount(&lfs);

	return res;
}


static int littlefs_scan_dir(const char *path, size_t *files, size_t *dirs, size_t *used)
{
	lfs_dir_t dir;
	struct lfs_info info;
	char *dirname = NULL;
	char separator[2] = "/";
	int res;
	size_t f_count = 0;
	size_t d_count = 0;
	size_t total = 0;
	size_t path_len;

	if (!path)
		return -1;

	/* Check if path ends with "/"... */
	path_len = strnlen(path, LFS_NAME_MAX);
	if (path_len > 0) {
		if (path[path_len - 1] == '/')
			separator[0] = 0;
	}

	log_msg(LOG_DEBUG, "littlefs_scan_dir(%s)", path);
	if ((res = lfs_dir_open(&lfs, &dir, path)) != LFS_ERR_OK)
		return res;

	while ((res = lfs_dir_read(&lfs, &dir, &info) > 0)) {
		if (info.type == LFS_TYPE_REG) {
			f_count++;
			total += info.size;
			log_msg(LOG_DEBUG, "lfs: File '%s%s%s': size=%u",
				path, separator, info.name, info.size);
		}
		else if (info.type == LFS_TYPE_DIR) {
			/* Skip special directories ("." and "..") */
			if (info.name[0] == '.') {
				if (info.name[1] == 0)
					continue;
				if (info.name[1] == '.' && info.name[2] == 0)
						continue;
			}
			d_count++;
			log_msg(LOG_DEBUG, "lfs: Directory '%s%s%s'",
				path, separator, info.name);
			if ((dirname = malloc(LFS_NAME_MAX + 1))) {
				res = snprintf(dirname, LFS_NAME_MAX + 1, "%s%s%s",
					path, separator, info.name);
				if (res > LFS_NAME_MAX)
					log_msg(LOG_WARNING, "lfs: filename truncated '%s'", dirname);
				littlefs_scan_dir(dirname, files, dirs, used);
				free(dirname);
			} else {
				log_msg(LOG_WARNING, "littlefs_scan_dir: malloc failed");
			}
		}
		else {
			log_msg(LOG_NOTICE, "lfs: Unknown directory entry %s%s%s: type=%u, size=%u",
				path, separator, info.name, info.type, info.size);
		}
	}
	lfs_dir_close(&lfs, &dir);

	if (files)
		*files += f_count;
	if (dirs)
		*dirs += d_count;
	if (used)
		*used += total;
	log_msg(LOG_DEBUG, "littlefs_scan_dir(%s): files=%u (total size=%u), dirs=%u",
		path, f_count, total, d_count);

	return 0;
}


int flash_get_fs_info(size_t *size, size_t *free, size_t *files,
		size_t *directories, size_t *filesizetotal)
{
	int res;
	size_t fs_dirs = 0;
	size_t fs_files = 0;
	size_t fs_total = 0;
	size_t used, used_blocks, fs_size;

	if (!size || !free)
		return -1;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -2;
	}

	used_blocks = lfs_fs_size(&lfs);
	used = used_blocks * lfs_cfg->block_size;
	fs_size = lfs_cfg->block_count * lfs_cfg->block_size;

	if ((res = littlefs_scan_dir("/", &fs_files, &fs_dirs, &fs_total)) < 0)
		log_msg(LOG_ERR, "little_fs_scan_dir() failed: %d", res);

	if (size)
		*size = fs_size;
	if (free)
		*free = fs_size - used;
	if (files)
		*files = fs_files;
	if (directories)
		*directories = fs_dirs;
	if (filesizetotal)
		*filesizetotal = fs_total;

	lfs_unmount(&lfs);

	return 0;
}


static int littlefs_list_dir(const char *path, bool recursive)
{
	lfs_dir_t dir;
	struct lfs_info info;
	char separator[2] = "/";
	char *dirname;
	size_t path_len;
	int res;

	if (!path)
		return LFS_ERR_INVAL;

	/* Check if path ends with "/" ... */
	path_len = strnlen(path, LFS_NAME_MAX);
	if (path_len > 0) {
		if (path[path_len - 1] == '/')
			separator[0] = 0;
	}

	printf("Directory: %s\n\n", path);

	/* Open directory */
	if ((res = lfs_dir_open(&lfs, &dir, path)) != LFS_ERR_OK)
		return res;

	/* First scan for directories... */
	while ((res = lfs_dir_read(&lfs, &dir, &info)) > 0) {
		if (info.type == LFS_TYPE_DIR)
			printf("%-50s %10s\n", info.name, "<DIR>");
	}

	/* Next scan for files... */
	lfs_dir_rewind(&lfs, &dir);
	while ((res = lfs_dir_read(&lfs, &dir, &info)) > 0) {
		if (info.type == LFS_TYPE_REG)
			printf("%-50s %10lu\n", info.name, info.size);
	}
	printf("\n");

	if (recursive) {
		/* Scan subdirectories recursively... */
		lfs_dir_rewind(&lfs, &dir);
		while ((res = lfs_dir_read(&lfs, &dir, &info)) > 0) {
			if (info.type != LFS_TYPE_DIR)
				continue;
			/* Skip special directories ("." and "..") */
			if (info.name[0] == '.') {
				if (info.name[1] == 0)
					continue;
				if (info.name[1] == '.' && info.name[2] == 0)
					continue;
			}
			if ((dirname = malloc(LFS_NAME_MAX + 1))) {
				snprintf(dirname, LFS_NAME_MAX + 1, "%s%s%s",
					path, separator, info.name);
				flash_list_directory(dirname, recursive);
				free(dirname);
			}
		}
	}

	lfs_dir_close(&lfs, &dir);

	return LFS_ERR_OK;
}


int flash_list_directory(const char *path, bool recursive)
{
	int res;

	if (!path)
		return -1;

	/* Mount flash filesystem... */
	if ((res = lfs_mount(&lfs, lfs_cfg)) != LFS_ERR_OK) {
		log_msg(LOG_ERR, "lfs_mount() failed: %d", res);
		return -2;
	}

	res = littlefs_list_dir(path, recursive);

	lfs_unmount(&lfs);

	return res;
}


void print_rp2040_flashinfo()
{
	size_t binary_size = &__flash_binary_end - &__flash_binary_start;
	size_t fs_size = lfs_cfg->block_count * lfs_cfg->block_size;

	printf("Flash memory size:                     %u\n", PICO_FLASH_SIZE_BYTES);
	printf("Binary size:                           %u\n", binary_size);
	printf("LittleFS size:                         %u\n", fs_size);
	printf("Unused flash memory:                   %u\n",
		PICO_FLASH_SIZE_BYTES - binary_size - fs_size);

}

