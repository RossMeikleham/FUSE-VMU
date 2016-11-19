#ifndef VMU_DRIVER_H
#define VMU_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    uint16_t fat_size;
    uint16_t directory_location;
    uint16_t directory_size;
    uint16_t icon_shape;
    uint16_t user_block_count;
};

struct vmu_file {
    bool is_free;
    enum filetype filetype;
    bool copy_protected;
    uint16_t starting_block;
    char filename[MAX_FILENAME_SIZE + 1];
    struct timestamp timestamp;
    uint16_t size_in_blocks;
    uint16_t offset_in_blocks;
};

struct vmu_fs {
    struct root_block root_block;
    struct vmu_file vmu_file[TOTAL_DIRECTORY_ENTRIES];      
    uint8_t *img;
};


// Convert 2 bytes into a 16 bit little endian integer
uint16_t to_16bit_le(const uint8_t *img);

// Read basic filesystem structures from vmu image
int read_fs(uint8_t *img, const unsigned length, struct vmu_fs*);

// Write a file into the vmu filesystem
int write_file(struct vmu_fs *vmu_fs, 
    const char *file_name, 
    const uint8_t *file_contents, 
    const unsigned file_length);


#ifdef __cplusplus
}
#endif

#endif
