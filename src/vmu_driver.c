#include "vmu_driver.h"

#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

#ifndef EUCLEAN
#define EUCLEAN -135
#endif

uint16_t to_16bit_le(const uint8_t *img)
{
	return img[0] | (img[1] << 8);
}

/* Write a 16 bit value in little endian format
 * into the given memory address
 */
static void write_16bit_le(uint8_t *img, uint16_t value)
{
	img[0] = value & 0xFF;
	img[1] = value >> 8;
}

static struct timestamp create_timestamp(const uint8_t *img)
{
	struct timestamp ts;

	ts.century = img[0];
	ts.year = img[1];
	ts.month = img[2];
	ts.day = img[3];
	ts.hour = img[4];
	ts.minute = img[5];
	ts.second = img[6];
	ts.day_of_week = img[7];

	return ts;
}

// Converts a Binary Coded Decimal value into an integer
static uint8_t bcd_to_byte(uint8_t bcd)
{
	return (((bcd & 0xF0) >> 4) * 10) + (bcd & 0x0F);
}

// Transforms a byte with the value between 0 - 99 into a bcd
// encoded byte
static uint8_t byte_to_bcd(uint8_t byte)
{
	return ((byte / 10) << 4) + (byte % 10);
}

static bool is_leap_year(uint64_t year)
{
	return (year % 400 == 0) || (!(year % 100 == 0) && year % 4 == 0);
}

static struct timestamp to_timestamp(time_t time)
{
	struct tm *tm = localtime(&time);
	struct timestamp timestamp;

	timestamp.century = byte_to_bcd((tm->tm_year + 1900) / 100);
	timestamp.year = byte_to_bcd(tm->tm_year % 100);
	timestamp.month = byte_to_bcd(tm->tm_mon + 1);
	timestamp.day = byte_to_bcd(tm->tm_mday);
	timestamp.hour = byte_to_bcd(tm->tm_hour);
	timestamp.minute = byte_to_bcd(tm->tm_min);
	timestamp.second = byte_to_bcd(tm->tm_sec);
	timestamp.day_of_week = byte_to_bcd(tm->tm_wday);

	return timestamp;
}

// Translate the creation date of the given vmu file into
// a time_t format
time_t get_creation_time(const struct vmu_file *vmu_file)
{
	const struct timestamp *ts = &(vmu_file->timestamp);

	// translate bcd values into actual integers
	uint8_t century = bcd_to_byte(ts->century);
	uint8_t year = bcd_to_byte(ts->year);
	uint8_t month = bcd_to_byte(ts->month);
	uint8_t day = bcd_to_byte(ts->day);
	uint8_t hour = bcd_to_byte(ts->hour);
	uint8_t minute = bcd_to_byte(ts->minute);
	uint8_t second = bcd_to_byte(ts->second);

	// Check date is after Unix Epoch (1/1/1970)
	if (ts->century < 20 && !(ts->century == 19 && ts->year >= 70))
		return 0;

	uint64_t full_year = (century * 100) + year;
	uint64_t full_days = 0;

	for (uint64_t i = 1970; i < full_year; i++)
		full_days += 365 + is_leap_year(i);

	int feb_days = is_leap_year(full_year) ? 29 : 28;
	const uint8_t days_in_month[] = {
		31, feb_days, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};

	for (uint8_t i = 1; i < month; i++)
		full_days += days_in_month[i-1];

	full_days += (day - 1);
	time_t time = ((((full_days * 24) + hour) * 60 + minute) * 60 + second);

	return time;
}

int vmufs_get_dir_entry(const struct vmu_fs *vmu_fs, const char *path)
{
	int matched_dir_entry = -1;

	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {

		bool is_free = !vmu_fs->vmu_file[i].is_free;
		const char *filename = vmu_fs->vmu_file[i].filename;
		bool name_match = !strncmp(path, filename, MAX_FILENAME_SIZE);

		if (is_free && name_match) {
			matched_dir_entry = i;
			break;
		}
	}

	return matched_dir_entry;
}


int32_t vmufs_next_block(const struct vmu_fs *vmu_fs, uint16_t block_no)
{
	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	const int next_block_fat_addr = fat_block_addr + (block_no * 2);

	return to_16bit_le(vmu_fs->img + next_block_fat_addr);
}


static void vmufs_set_next_block(const struct vmu_fs *vmu_fs,
	uint16_t block_no, uint16_t next_block_no)
{
	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	const int next_block_fat_addr = fat_block_addr + (block_no * 2);

	write_16bit_le(vmu_fs->img +  next_block_fat_addr, next_block_no);
}


static void vmufs_free_block(const struct vmu_fs *vmu_fs, uint16_t block_no)
{
	vmufs_set_next_block(vmu_fs, block_no, 0xFFFC);
}

static void vmufs_mark_eof(const struct vmu_fs *vmu_fs, uint16_t block_no)
{
	vmufs_set_next_block(vmu_fs, block_no, 0xFFFA);
}


// Attempts to locate a free block counting down from the given
// block number, returns the block number > 0 if sucessful, -1 otherwise
static int32_t vmufs_next_free_block(const struct vmu_fs *vmu_fs,
	int32_t block_no)
{
	if (block_no < 0)
		return -1;

	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	for (;;) {
		const int next_block_fat_addr = fat_block_addr + (block_no * 2);

		if (to_16bit_le(vmu_fs->img + next_block_fat_addr) == 0xFFFC)
			return block_no;

		if (block_no == 0)
			return -1;

		block_no--;
	}
}


int vmufs_read_fs(uint8_t *img, const unsigned int length,
	struct vmu_fs *vmu_fs)
{
	// VMU fs should be EXACTLY 128KB
	if (length != BLOCK_SIZE_BYTES * TOTAL_BLOCKS)
		return -EUCLEAN;

	// Read Root Block
	memset(vmu_fs, 0, sizeof(const struct vmu_fs));
	const int root_block_addr = ROOT_BLOCK_NO * BLOCK_SIZE_BYTES;

	vmu_fs->root_block.custom_vms_color = img[root_block_addr + 0x10];
	vmu_fs->root_block.blue = img[root_block_addr + 0x11];
	vmu_fs->root_block.red = img[root_block_addr + 0x12];
	vmu_fs->root_block.green = img[root_block_addr + 0x13];
	vmu_fs->root_block.alpha = img[root_block_addr + 0x14];
	vmu_fs->root_block.green = img[root_block_addr + 0x13];

	vmu_fs->root_block.timestamp =
		create_timestamp(img + root_block_addr + 0x30);

	vmu_fs->root_block.fat_location =
		to_16bit_le(img + (root_block_addr + 0x46));

	vmu_fs->root_block.fat_size =
		to_16bit_le(img + (root_block_addr + 0x48));

	vmu_fs->root_block.directory_location =
		to_16bit_le(img + (root_block_addr + 0x4A));

	vmu_fs->root_block.directory_size =
		to_16bit_le(img + (root_block_addr + 0x4C));

	vmu_fs->root_block.icon_shape =
		to_16bit_le(img + (root_block_addr + 0x4E));

	vmu_fs->root_block.user_block_count =
		to_16bit_le(img + (root_block_addr + 0x50));

	vmu_fs->img = img;

	int dir_block_start = vmu_fs->root_block.directory_location;

	for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {

		int dir_entry_offset = ((dir_block_start + 1) *
			BLOCK_SIZE_BYTES) -
			(DIRECTORY_ENTRY_BYTE_SIZE * (i + 1));

		vmu_fs->vmu_file[i].is_free = false;

		// Set File Type
		switch (img[dir_entry_offset]) {
		case 0x33:
			vmu_fs->vmu_file[i].filetype = DATA;
			break;
		case 0xCC:
			vmu_fs->vmu_file[i].filetype = GAME;
			break;
		default:
			vmu_fs->vmu_file[i].filetype = UNKNOWN;
			vmu_fs->vmu_file[i].is_free = true;
			continue;
		}

		// Set Copy Protection
		switch (img[dir_entry_offset + 0x1]) {
		case 0x00:
			vmu_fs->vmu_file[i].copy_protected = false;
			break;
		case 0xFF:
			vmu_fs->vmu_file[i].copy_protected = true;
			break;
		default:
			vmu_fs->vmu_file[i].is_free = true;
			continue;
		}

		vmu_fs->vmu_file[i].starting_block =
			to_16bit_le(img + dir_entry_offset + 0x2);

		for (int j = 0; j < MAX_FILENAME_SIZE; j++)
			vmu_fs->vmu_file[i].filename[j] =
				img[dir_entry_offset + 0x4 + j];

		vmu_fs->vmu_file[i].filename[MAX_FILENAME_SIZE] = 0;

		vmu_fs->vmu_file[i].timestamp =
			create_timestamp(img + dir_entry_offset + 0x10);

		vmu_fs->vmu_file[i].size_in_blocks =
			to_16bit_le(img + dir_entry_offset + 0x18);

		vmu_fs->vmu_file[i].offset_in_blocks =
			to_16bit_le(img + dir_entry_offset + 0x1A);
	}

	return 0;
}


int vmufs_rename_file(struct vmu_fs *vmu_fs, const char *from,
	const char *to)
{
	if (strlen(from) > 0 && strstr(from, "/") == from)
		from++;

	if (strlen(to) > 0 && strstr(to, "/") == to)
		to++;

	if (strnlen(to, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE)
		return -ENAMETOOLONG;

	// Same filename, don't need to do anything
	if (strncmp(from, to, MAX_FILENAME_SIZE) == 0)
		return 0;

	bool to_exists = false;
	int from_entry = -1;

	for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {

		if (!vmu_fs->vmu_file[i].is_free) {

			int fm_cmp = strncmp(from, vmu_fs->vmu_file[i].filename,
				MAX_FILENAME_SIZE);

			int to_cmp = strncmp(to, vmu_fs->vmu_file[i].filename,
				MAX_FILENAME_SIZE);

			if (from_entry < 0 && fm_cmp == 0)
				from_entry = i;

			if (to_cmp == 0) {
				to_exists = true;
				break;
			}
		}
	}

	if (to_exists)
		return -EEXIST;

	if (from_entry < 0)
		return -ENOENT;

	strncpy(vmu_fs->vmu_file[from_entry].filename, to, MAX_FILENAME_SIZE);
	return 0;
}

int vmufs_read_file(const struct vmu_fs *vmu_fs, const char *path,
	uint8_t *buf, size_t size, uint64_t offset)
{
	int dir_entry = vmufs_get_dir_entry(vmu_fs, path);

	if (dir_entry < 0)
		return -EEXIST;

	// If the given offset is larger than the filesize or the requested
	// number of bytes to read is 0 then we don't need to do anything
	size_t file_length = vmu_fs->vmu_file[dir_entry].size_in_blocks *
		BLOCK_SIZE_BYTES;

	// Attempting to write past the end of the file
	if (offset + size > file_length)
		return -EINVAL;

	// Not writing anything, no need to do anything
	if (size == 0)
		return 0;


	size = offset + size > file_length ? file_length - offset : size;
	size_t copied = 0;

	// Read through the file until reaching the block where we need
	// to start copying
	uint16_t cur_block = vmu_fs->vmu_file[dir_entry].starting_block;
	int offset_blocks = offset / BLOCK_SIZE_BYTES;

	for (int blocks_read = 0; blocks_read < offset_blocks; blocks_read++) {

		// Something wrong with the file in the FS shouldn't reach
		// the end, or be higher than the max number of blocks
		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		cur_block = vmufs_next_block(vmu_fs, cur_block);
	}

	// Copy the offset into the first block til the end of that block
	int offset_bytes = offset % BLOCK_SIZE_BYTES;

	if (offset_bytes != 0) {

		int bytes_to_copy = BLOCK_SIZE_BYTES - offset_bytes;

		bytes_to_copy = size > bytes_to_copy ? bytes_to_copy : size;
		memcpy(buf, vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES) +
			offset_bytes, bytes_to_copy);

		// We've read all that we need to
		if (bytes_to_copy == size)
			return size;

		copied += bytes_to_copy;

		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		cur_block = vmufs_next_block(vmu_fs, cur_block);
	}

	// Bytes to read in the last block (if not block alligned)
	int leftover_bytes = (offset + size) % BLOCK_SIZE_BYTES;

	// Copy all Full blocks
	int full_blocks = (size - copied) / BLOCK_SIZE_BYTES;

	for (int blocks_read = 0; blocks_read < full_blocks; blocks_read++) {

		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		uint8_t *to = buf + copied;
		uint8_t *from = vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES);

		memcpy(to, from, BLOCK_SIZE_BYTES);
		copied += BLOCK_SIZE_BYTES;
		cur_block = vmufs_next_block(vmu_fs, cur_block);
	}

	// Copy the leftover bytes
	if (leftover_bytes > 0) {

		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		uint8_t *to = buf + copied;
		uint8_t *from = vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES);

		memcpy(to, from, leftover_bytes);
	}

	return size;
}


int vmu_fs_create_file(struct vmu_fs *vmu_fs, const char *path)
{
	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	uint8_t *img = vmu_fs->img;

	if (strnlen(path, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE)
		return -ENAMETOOLONG;

	int first_free_dir_entry = -1;
	int matched_dir_entry = -1;

	// Locate a free entry for the directory entry and
	// check if the file already exists
	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {
		if (vmu_fs->vmu_file[i].is_free) {
			if (first_free_dir_entry == -1)
				first_free_dir_entry = i;

			continue;
		}

		const char *fname = vmu_fs->vmu_file[i].filename;

		// Can't create duplicates
		if (strncmp(path, fname, MAX_FILENAME_SIZE) == 0)
			return -EEXIST;
	}

	// Not enough space for the directory entry for the file
	if (first_free_dir_entry == -1 && matched_dir_entry == -1)
		return -ENOSPC;

	vmu_fs->vmu_file[first_free_dir_entry].is_free = 0;
	vmu_fs->vmu_file[first_free_dir_entry].filetype = DATA;
	vmu_fs->vmu_file[first_free_dir_entry].copy_protected = false;
	vmu_fs->vmu_file[first_free_dir_entry].starting_block = 0xFFFA;
	strncpy(vmu_fs->vmu_file[first_free_dir_entry].filename,
		path, MAX_FILENAME_SIZE + 1);

	time_t raw_time;

	time(&raw_time);
	vmu_fs->vmu_file[first_free_dir_entry].timestamp =
		to_timestamp(raw_time);

	vmu_fs->vmu_file[first_free_dir_entry].size_in_blocks = 0;
	vmu_fs->vmu_file[first_free_dir_entry].offset_in_blocks = 0;

	return 0;
}


int vmufs_write_file(struct vmu_fs *vmu_fs, const char *path,
	uint8_t *buf, size_t size, uint64_t offset)
{
	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	uint8_t *img = vmu_fs->img;

	if (strnlen(path, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE)
		return -ENAMETOOLONG;

	int first_free_dir_entry = -1;
	int matched_dir_entry = -1;

	// Check if file already exists so we may be able to re-use
	// the directory entry and allocated blocks
	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {

		if (vmu_fs->vmu_file[i].is_free) {
			if (first_free_dir_entry == -1)
				first_free_dir_entry = i;

			continue;
		}

		const char *fname = vmu_fs->vmu_file[i].filename;

		if (strncmp(path, fname, MAX_FILENAME_SIZE) == 0) {
			matched_dir_entry = i;
			break;
		}
	}

	// Not enough space for the directory entry for the file
	if (first_free_dir_entry == -1 && matched_dir_entry == -1)
		return -ENOSPC;

	if (first_free_dir_entry != -1 && offset != 0)
		return -EEXIST;

	// Calculate the total blocks needed to perform the write operation
	int offset_bytes = offset % BLOCK_SIZE_BYTES;
	int leftover_bytes = (size + offset) % BLOCK_SIZE_BYTES;
	unsigned int blocks_needed = (size + offset) / BLOCK_SIZE_BYTES +
		(!!leftover_bytes);

	int starting_block_set = 1;

	// Set the directory information if writing a new file
	if (first_free_dir_entry != -1) {
		starting_block_set = 0;

		vmu_fs->vmu_file[first_free_dir_entry].is_free = 0;
		vmu_fs->vmu_file[first_free_dir_entry].filetype = DATA;
		vmu_fs->vmu_file[first_free_dir_entry].copy_protected = false;
		vmu_fs->vmu_file[first_free_dir_entry].starting_block = 0xFFFA;
		strncpy(vmu_fs->vmu_file[first_free_dir_entry].filename,
			path, MAX_FILENAME_SIZE + 1);

		time_t raw_time;

		time(&raw_time);

		vmu_fs->vmu_file[first_free_dir_entry].timestamp =
			to_timestamp(raw_time);

		vmu_fs->vmu_file[first_free_dir_entry].size_in_blocks =
			blocks_needed;

		vmu_fs->vmu_file[first_free_dir_entry].offset_in_blocks = 0;
		matched_dir_entry = first_free_dir_entry;

		uint16_t block_no = vmu_fs->root_block.user_block_count;
		int32_t free_block = vmufs_next_free_block(vmu_fs, block_no);

		if (free_block == -1)
			return -ENOSPC;

		vmu_fs->vmu_file[first_free_dir_entry].starting_block =
			free_block;

		write_16bit_le(img + fat_block_addr +
			(free_block * 2), 0xFFFA);
	}

	// No file content to write, we can stop here
	if (size == 0)
		return 0;

	int blocks_written = 0;
	int last_block = -1;

	int offset_left = offset;

	uint16_t prev_block = 0xFFFF;
	uint16_t cur_block = vmu_fs->vmu_file[matched_dir_entry].starting_block;

	// Skip over blocks present in an existing file
	if (offset >= BLOCK_SIZE_BYTES && first_free_dir_entry == -1) {

		do {
			if (cur_block >= vmu_fs->root_block.user_block_count)
				return -EINVAL;

			cur_block = vmufs_next_block(vmu_fs, cur_block);
			prev_block = cur_block;
			offset_left -= BLOCK_SIZE_BYTES;

		} while (offset_left >= BLOCK_SIZE_BYTES);
	}

	int32_t block_no = vmu_fs->root_block.user_block_count - 1;

	// Need to create blocks to skip over
	while (offset_left >= BLOCK_SIZE_BYTES) {

		bool block_free;
		uint8_t *next_block;

		block_no = vmufs_next_free_block(vmu_fs, block_no);

		// No free blocks left
		if (block_no == -1)
			return -ENOSPC;

		if (prev_block == 0xFFFF) {
			vmu_fs->vmu_file[matched_dir_entry].starting_block =
				block_no;

		} else {
			*(img + fat_block_addr + (prev_block * 2)) = block_no;
		}

		cur_block = block_no;
		offset_left -= BLOCK_SIZE_BYTES;
	}

	uint32_t bytes_written = 0;

	if (offset_left > 0) {
		uint8_t *to = img + cur_block + offset_left;

		memcpy(to, buf, BLOCK_SIZE_BYTES - offset_left);
		bytes_written += BLOCK_SIZE_BYTES - offset_left;
	}

	int full_blocks = (size / BLOCK_SIZE_BYTES) +
		!!(size % BLOCK_SIZE_BYTES) -
		((offset_bytes != 0) || (leftover_bytes != 0));

	// Overwrite full blocks in an existing file
	if (first_free_dir_entry == -1) {

		while (blocks_needed > 0 && cur_block != 0xFFFA) {
			int bytes_to_write = blocks_needed == 1 &&
				leftover_bytes ?
					leftover_bytes :
					BLOCK_SIZE_BYTES;

			uint8_t *to = img + (cur_block * BLOCK_SIZE_BYTES);

			memcpy(to, buf + bytes_written, bytes_to_write);
			bytes_written += bytes_to_write;
			cur_block = vmufs_next_block(vmu_fs, cur_block);
			blocks_needed--;
		}
	}

	// Need to create blocks to write content to
	while (bytes_written < size) {

		int bytes_to_write = size - bytes_written < BLOCK_SIZE_BYTES ?
			leftover_bytes :
			BLOCK_SIZE_BYTES;

		// Locate next free block
		block_no = vmufs_next_free_block(vmu_fs, block_no);

		// All blocks are being used
		if (block_no < 0)
			return -ENOSPC;

		if (prev_block == 0xFFFF) {
			vmu_fs->vmu_file[matched_dir_entry].starting_block =
				block_no;
		} else {
			uint8_t *addr = img + fat_block_addr + (cur_block * 2);

			write_16bit_le(addr, block_no);
		}

		prev_block = cur_block;
		cur_block = block_no;

		uint8_t *to = img + (cur_block * BLOCK_SIZE_BYTES);

		memcpy(to, buf + bytes_written, bytes_to_write);
		write_16bit_le(img + fat_block_addr + (cur_block * 2), 0xFFFA);
		bytes_written += bytes_to_write;
	}

	uint16_t current_blocks_used =
		vmu_fs->vmu_file[matched_dir_entry].size_in_blocks;

	uint16_t new_blocks_used = (size + offset) / BLOCK_SIZE_BYTES +
		(!!leftover_bytes);

	if (new_blocks_used > current_blocks_used)
		vmu_fs->vmu_file[matched_dir_entry].size_in_blocks =
			new_blocks_used;

	return bytes_written;
}


int vmufs_remove_file(struct vmu_fs *vmu_fs, const char *file_name)
{
	// File doesn't exist as filename is too large
	if (strnlen(file_name, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE)
		return -ENAMETOOLONG;

	int matched_dir_entry = -1;

	/* Locate the FAT directory entry for the file*/
	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {

		const char *vmu_fname = vmu_fs->vmu_file[i].filename;

		if (!vmu_fs->vmu_file[i].is_free &&
			strncmp(file_name, vmu_fname, MAX_FILENAME_SIZE) == 0) {

			matched_dir_entry = i;
			break;
		}
	}

	// File not found
	if (matched_dir_entry == -1)
		return -ENOENT;

	// Mark directory entry as free as well as all the FAT blocks
	// allocated to it
	vmu_fs->vmu_file[matched_dir_entry].is_free = 1;

	const int fat_block_addr = BLOCK_SIZE_BYTES *
		vmu_fs->root_block.fat_location;

	uint16_t cur_block = vmu_fs->vmu_file[matched_dir_entry].starting_block;
	int fat_addr = (cur_block * 2) + fat_block_addr;

	while (cur_block != 0xFFFA) {
		cur_block = vmufs_next_block(vmu_fs, cur_block);

		if (cur_block >= vmu_fs->root_block.user_block_count &&
			cur_block !=  0xFFFA)
			return -EINVAL;

		int next = fat_block_addr + (cur_block * 2);

		write_16bit_le(vmu_fs->img + fat_addr, 0xFFFC);
		fat_addr = next;
	}

	return 0;
}


int vmufs_truncate_file(struct vmu_fs *vmu_fs, const char *path, off_t size)
{
	// VMU filesizes are always in blocks
	uint16_t blocks_required = (size / BLOCK_SIZE_BYTES) +
		!!(size % BLOCK_SIZE_BYTES);

	int dir_entry = vmufs_get_dir_entry(vmu_fs, path);

	if (dir_entry < 0)
		return -ENOENT;

	if (blocks_required > TOTAL_BLOCKS)
		return -ENOSPC;

	struct vmu_file *vmu_file = &vmu_fs->vmu_file[dir_entry];

	// No need to do anything
	if (blocks_required == vmu_file->size_in_blocks)
		return (blocks_required * BLOCK_SIZE_BYTES);

	// Navigate to where we need to truncate or extend the file
	int block_to_go_to = vmu_file->size_in_blocks < blocks_required ?
		vmu_file->size_in_blocks : blocks_required;

	uint16_t cur_block = vmu_file->starting_block;

	for (int i = 0; i < block_to_go_to - 1; i++) {
		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		cur_block = vmufs_next_block(vmu_fs, cur_block);
	}

	// Truncating from this point onwards
	if (vmu_file->size_in_blocks > blocks_required) {

		// Mark end of file
		if (cur_block >= vmu_fs->root_block.user_block_count)
			return -EINVAL;

		int next_block = vmufs_next_block(vmu_fs, cur_block);

		if (blocks_required == 0)
			vmufs_free_block(vmu_fs, cur_block);
		else
			vmufs_mark_eof(vmu_fs, cur_block);

		cur_block = next_block;

		while (cur_block != 0xFFFA) {
			if (cur_block >= vmu_fs->root_block.user_block_count)
				return -EINVAL;

			int next_block = vmufs_next_block(vmu_fs, cur_block);

			vmufs_free_block(vmu_fs, cur_block);
			cur_block = next_block;
		}

		vmu_file->size_in_blocks = blocks_required;

		if (blocks_required == 0)
			vmu_file->starting_block = 0xFFFA;

		return (blocks_required * BLOCK_SIZE_BYTES);
	}

	// Otherwise need to append blocks
	int next_free_block = vmu_fs->root_block.user_block_count;

	for (int i = 0; i < (blocks_required - vmu_file->size_in_blocks); i++) {

		next_free_block =
			vmufs_next_free_block(vmu_fs, next_free_block - 1);

		if (next_free_block < 0) {
			vmufs_mark_eof(vmu_fs, cur_block);
			vmu_file->size_in_blocks += i;
			return (vmu_file->size_in_blocks * BLOCK_SIZE_BYTES);
		}

		// Empty file
		if (cur_block == 0xFFFA)
			vmu_file->starting_block = next_free_block;
		else
			vmufs_set_next_block(vmu_fs,
				cur_block, next_free_block);

		cur_block = next_free_block;
	}

	vmufs_mark_eof(vmu_fs, cur_block);
	vmu_file->size_in_blocks = blocks_required;

	return (blocks_required * BLOCK_SIZE_BYTES);
}


int vmufs_write_changes_to_disk(struct vmu_fs *vmu_fs, const char *file_path)
{
	FILE *vmu_file = fopen(file_path, "wb");

	if (vmu_file == NULL) {
		perror("Error");
		fprintf(stderr, "Unable to open file \"%s\"\n", file_path);
		return -1;
	}

	// Write "User Blocks' from 0 - 240
	fwrite(vmu_fs->img, sizeof(uint8_t), 241 * BLOCK_SIZE_BYTES, vmu_file);

	// Write Directory Entries, blocks (240 - 253)
	for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) {

		uint8_t file_type;

		switch (vmu_fs->vmu_file[i].filetype) {
		case DATA:
			file_type = 0x33;
			break;
		case GAME:
			file_type = 0xCC;
			break;
		default:
			file_type = 0x00;
		}

		fwrite(&file_type, sizeof(uint8_t), 1, vmu_file);

		uint8_t copy_protection = vmu_fs->vmu_file[i].copy_protected ?
			0xFF : 0x00;

		fwrite(&copy_protection, sizeof(uint8_t), 1, vmu_file);

		uint8_t starting_block[2];

		write_16bit_le(starting_block,
			vmu_fs->vmu_file[i].starting_block);

		fwrite(starting_block, sizeof(uint8_t), 2, vmu_file);

		fwrite(vmu_fs->vmu_file[i].filename, sizeof(uint8_t),
			MAX_FILENAME_SIZE, vmu_file);

		const struct timestamp ts = vmu_fs->vmu_file[i].timestamp;

		fwrite(&ts.century, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.year, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.month, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.day, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.hour, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.minute, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.second, sizeof(uint8_t), 1, vmu_file);
		fwrite(&ts.day_of_week, sizeof(uint8_t), 1, vmu_file);

		uint8_t size_in_blocks[2];

		write_16bit_le(size_in_blocks,
			vmu_fs->vmu_file[i].size_in_blocks);
		fwrite(size_in_blocks, sizeof(uint8_t), 2, vmu_file);

		uint8_t offset_in_blocks[2];

		write_16bit_le(offset_in_blocks,
			vmu_fs->vmu_file[i].size_in_blocks);
		fwrite(offset_in_blocks, sizeof(uint8_t), 2, vmu_file);

		// Write Unused bytes
		uint32_t zero = 0;

		fwrite(&zero, sizeof(uint8_t), 4, vmu_file);
	}

	// Write FAT Block
	const uint8_t *fat_block_addr =
		vmu_fs->img +
		(BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location);

	fwrite(fat_block_addr, sizeof(uint8_t), BLOCK_SIZE_BYTES, vmu_file);

	// Write Root Block (255) (shouldn't have changed)
	const uint8_t *root_block_addr =
		vmu_fs->img + (BLOCK_SIZE_BYTES * ROOT_BLOCK_NO);

	fwrite(root_block_addr, sizeof(uint8_t), BLOCK_SIZE_BYTES, vmu_file);

	fclose(vmu_file);
	return 0;
}
