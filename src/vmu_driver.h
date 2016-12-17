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
#define DIRECTORY_ENTRIES_PER_BLOCK (BLOCK_SIZE_BYTES / DIRECTORY_ENTRY_BYTE_SIZE)
#define TOTAL_DIRECTORY_ENTRIES DIRECTORY_ENTRY_BLOCK_SIZE * DIRECTORY_ENTRIES_PER_BLOCK

enum filetype {
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

// Standard header used by the file manager to display info about the file
struct vms_header {
    char vms_file_description[16]; // Description of file shown in VMS file menu
    char boot_ROM_file_description[32]; // Description of file shown in boot ROM file manager
    char application_creation_id[16]; // Id of application that created the file
    uint16_t no_of_icons;
    uint16_t icon_animation_speed;
    uint16_t graphic_eyecatch_type;
    uint16_t crc;
    uint32_t bytes_of_file_data; // Bytes of file data following header
    uint16_t icon_palette[16];
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

int vmufs_next_block(const struct vmu_fs *vmu_fs, uint16_t block_no);

// Obtains the creation time of a file
time_t get_creation_time(const struct vmu_file *vmu_file);

// Obtains the directory entry offset for the given file path
// in the filesystem. Returns -1 if it cannot be found
int vmufs_get_dir_entry(const struct vmu_fs *vmu_fs, const char *path);

// Read basic filesystem structures from vmu image
int vmufs_read_fs(uint8_t *img, const unsigned length, struct vmu_fs*);


// Read a file from the vmu filesystem
// returns the number of bytes successfully read
int vmufs_read_file(const struct vmu_fs *vmu_fs, 
    const char *file_name, 
    uint8_t *buf, 
    size_t size, 
    uint64_t offset);  

// Write a file into the vmu filesystem
int vmufs_write_file(struct vmu_fs *vmu_fs, 
    const char *file_name, 
    const uint8_t *file_contents, 
    const unsigned file_length);


// Remove a file from the filesystem
int vmufs_remove_file(struct vmu_fs *vmu_fs, const char *path);

#ifdef __cplusplus
}
#endif

#endif
