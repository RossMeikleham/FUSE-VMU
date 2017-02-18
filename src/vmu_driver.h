#ifndef VMU_DRIVER_H
#define VMU_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define BLOCK_SIZE_BYTES 512
#define TOTAL_BLOCKS 256
#define ROOT_BLOCK_NO 255
#define MAX_FILENAME_SIZE 12
#define DIRECTORY_ENTRY_BYTE_SIZE 32
#define DIRECTORY_ENTRY_BLOCK_SIZE 13
#define DIRECTORY_ENTRIES_PER_BLOCK\
	(BLOCK_SIZE_BYTES / DIRECTORY_ENTRY_BYTE_SIZE)
#define TOTAL_DIRECTORY_ENTRIES\
	(DIRECTORY_ENTRY_BLOCK_SIZE * DIRECTORY_ENTRIES_PER_BLOCK)

/* VMU Files can either be DATA (typically a save file)
 * or a GAME file (Typically minigames which can be played on the vmu)
 */
enum filetype {
	UNKNOWN,
	GAME,
	DATA
};

struct timestamp {
	uint8_t century;
	uint8_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t day_of_week;
};

struct root_block {
	bool custom_vms_color;
	uint8_t blue;
	uint8_t red;
	uint8_t green;
	uint8_t alpha;
	struct timestamp timestamp;
	uint16_t fat_location;
	uint16_t fat_size; // Size of the FAT table in blocks
	uint16_t directory_location; // Location of the base directory block
	uint16_t directory_size; // Number of blocks the directory uses
	uint16_t icon_shape;
	uint16_t user_block_count; // How many blocks are available to the user
};

// Directory information on an individual file
struct vmu_file {
	bool is_free; // Whether the directory entry contains a file or not
	enum filetype filetype; // Whether a file is DATA or a GAME
	bool copy_protected;
	uint16_t starting_block;
	char filename[MAX_FILENAME_SIZE + 1];
	struct timestamp timestamp;
	uint16_t size_in_blocks;
	uint16_t offset_in_blocks; // Offset of the File header
};

// VMU filesystem
struct vmu_fs {
	struct root_block root_block;
	struct vmu_file vmu_file[TOTAL_DIRECTORY_ENTRIES];
	uint8_t *img; // Binary representation of the Filesystem
};

// Convert 2 bytes into a 16 bit little endian integer
uint16_t to_16bit_le(const uint8_t *img);

// Locates the next free block in the filesystem below a given
// block number. Returns the number of the first free block found
// if successful. Returns -1 if there are no free blocks.
int32_t vmufs_next_block(const struct vmu_fs *vmu_fs, uint16_t block_no);

// Obtains the creation time of a file in a time_t format
time_t get_creation_time(const struct vmu_file *vmu_file);

// Obtains the directory entry offset for the given file path
// in the filesystem. Returns -1 if it cannot be found.
int vmufs_get_dir_entry(const struct vmu_fs *vmu_fs, const char *path);

// Read basic filesystem structures from vmu image, returns 0
// if successful, -EUCLEAN if the image is not of the correct
// size (128KB).
int vmufs_read_fs(uint8_t *img, const unsigned int length,
	struct vmu_fs *vmu_fs);

// Attempts to rename a file in the vmu filesystem
// returns 0 if successful, -ENAMETOOLONG if the
// rename is too long, -EEXIST if the rename already
// exists in the filesystem, -ENOENT if the original
// file doesn't exist.
int vmufs_rename_file(struct vmu_fs *vmu_fs,
	const char *from, const char *to);

// Reads a file from the vmu filesystem
// If successful returns the number of bytes read and
// those bytes are stored in the given buffer. Returns -EEXIST
// if the given file name doesn't exist, -EINVAL if attempting
// to read past the end of the file or if there is a problem
// traversing the file blocks which gives an invalid block number.
int vmufs_read_file(const struct vmu_fs *vmu_fs, const char *file_name,
	uint8_t *buf, size_t size, uint64_t offset);

// Creates a file in the filesystem given a path.
// returns 0 if successful, -ENAMETOOLONG if the file name
// is too long, -EEXIST if the file already exists, -ENOSPC
// if there is not enough space on the filesystem to create the file.
int vmu_fs_create_file(struct vmu_fs *vmu_fs, const char *path);

// Writes to the specified file, if successful returns the number of
// bytes written to the file. Attempts to create the file if
// it doesn't already exist. Returns -ENAMETOOLONG if the given path
// is too long, -ENOSPC if there is not enough space to write the
// given data to the file, -EEXIST if attempting to write to an offset
// in a file which doesn't exist, -EINVAL if there is a problem
// obtaining a valid block.
int vmufs_write_file(struct vmu_fs *vmu_fs, const char *path, uint8_t *buf,
	size_t size, uint64_t offset);

// Resizes the given file to the specified size. If successful returns
// the new size of the given file. Returns -ENOENT if the given file
// cannot be found, -ENOSPC if there isn't enough space in the filesystem
// to resize the file to the specified size,-EINVAL if there is a problem
// obtaining a valid block.
int vmufs_truncate_file(struct vmu_fs *vmu_fs, const char *path, off_t size);

// Remove a file from the filesystem. If successful returns 0.
// Returns -ENOENT if the given file cannot be found, -ENAMETOOLONG
// if the given file name is too long, -EINVAL if there is a problem
// obtaining a valid block.
int vmufs_remove_file(struct vmu_fs *vmu_fs, const char *path);

// Save the changes made to the VMU Filesystem to disk
// returns 0 if successful, -1 otherwise
int vmufs_write_changes_to_disk(struct vmu_fs *vmu_fs, const char *file_path);

#ifdef __cplusplus
}
#endif

#endif
