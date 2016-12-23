#include "vmu_driver.h"

#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <fuse/fuse_lowlevel.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

uint16_t to_16bit_le(const uint8_t *img) {
    return img[0] | (img[1] << 8);
}

static void write_16bit_le(uint8_t *img, uint16_t value) {
    img[0] = value & 0xFF;
    img[1] = value >> 8;
}

static struct timestamp create_timestamp(const uint8_t *img) {
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

static uint8_t bcd_to_byte(uint8_t bcd) {
    return (((bcd & 0xF0) >> 4 ) * 10) + (bcd & 0x0F);
}

// Transforms a byte with the value between 0 - 99 into a bcd
// encoded byte
static uint8_t byte_to_bcd(uint8_t byte) {
    return ((byte / 10) << 4) + (byte % 10);
}

static bool is_leap_year(uint64_t year) {
    return (year % 400 == 0) || (!(year % 100 == 0) && year % 4 == 0);
}

static struct timestamp to_timestamp(time_t time) {
    struct tm *tm = localtime(&time);
    struct timestamp timestamp;

    timestamp.century = byte_to_bcd((tm->tm_year - 1900) / 100);
    timestamp.year = byte_to_bcd(tm->tm_year % 100);
    timestamp.month = byte_to_bcd(tm->tm_mon + 1);
    timestamp.day = byte_to_bcd(tm->tm_mday);
    timestamp.hour = byte_to_bcd(tm->tm_hour);
    timestamp.minute = byte_to_bcd(tm->tm_min);
    timestamp.second = byte_to_bcd(tm->tm_sec);
    timestamp.day_of_week = byte_to_bcd(tm->tm_wday);

    return timestamp;
}

// Yay dates 
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
    if (ts->century < 20) {
        if (!(ts->century == 19 && ts->year >= 70)) {
            return 0;
        }
    }

    uint64_t full_year = (century * 100) + year;
    uint64_t full_days = 0;
    for (uint64_t i = 1970; i < full_year; i++) 
    {
        full_days += 365 + is_leap_year(i);
    }

    int feb_days = is_leap_year(full_year) ? 29 : 28;
    const uint8_t days_in_month[] = {31, feb_days, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint8_t i = 1; i <= month; i++) {
        full_days += days_in_month[i-1];
    }
    
    if ((month == 1 && day == 29) || (month > 1 && is_leap_year(full_year))) {
        full_days++;
    }

    time_t time = ((((full_days * 24) + hour) * 60 + minute) * 60 + second); 
    return time;
} 

int vmufs_get_dir_entry(const struct vmu_fs *vmu_fs, const char *path)
{
    int matched_dir_entry = -1;

    for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) { 
        bool is_free = !vmu_fs->vmu_file[i].is_free;
        const char *filename = vmu_fs->vmu_file[i].filename;
        if (is_free && strncmp(path, filename, MAX_FILENAME_SIZE) == 0) {
            matched_dir_entry = i;
            break;                
        }
    }

    return matched_dir_entry;
}


int vmufs_next_block(const struct vmu_fs *vmu_fs, uint16_t block_no) {
    int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location; 
    int next_block_fat_addr = fat_block_addr + (block_no * 2);
    return to_16bit_le(vmu_fs->img + next_block_fat_addr);
}


int vmufs_read_fs(uint8_t *img, const unsigned length, struct vmu_fs *vmu_fs) {
    
    // VMU fs should be EXACTLY 128KB
    if (length != BLOCK_SIZE_BYTES * TOTAL_BLOCKS) {
        return -EIO;    
    }

    // Read Root Block
    memset(vmu_fs, 0, sizeof(struct vmu_fs));
    const int root_block_addr = ROOT_BLOCK_NO * BLOCK_SIZE_BYTES; 

    vmu_fs->root_block.custom_vms_color = img[root_block_addr + 0x10];
    vmu_fs->root_block.blue = img[root_block_addr + 0x11];
    vmu_fs->root_block.red = img[root_block_addr + 0x12];
    vmu_fs->root_block.green = img[root_block_addr + 0x13];
    vmu_fs->root_block.alpha = img[root_block_addr + 0x14];
    vmu_fs->root_block.green = img[root_block_addr + 0x13];

    vmu_fs->root_block.timestamp = create_timestamp(img + root_block_addr + 0x30);

    vmu_fs->root_block.fat_location = to_16bit_le(img + (root_block_addr + 0x46)); 
    vmu_fs->root_block.fat_size = to_16bit_le(img + (root_block_addr + 0x48)); 
    vmu_fs->root_block.directory_location = to_16bit_le(img + (root_block_addr + 0x4A)); 
    vmu_fs->root_block.directory_size = to_16bit_le(img + (root_block_addr + 0x4C)); 
    vmu_fs->root_block.icon_shape = to_16bit_le(img + (root_block_addr + 0x4E)); 
    vmu_fs->root_block.user_block_count = to_16bit_le(img + (root_block_addr + 0x50));     

    int dir_block_start = vmu_fs->root_block.directory_location;

    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
       int dir_entry_offset = ((dir_block_start + 1) * BLOCK_SIZE_BYTES) - 
            (DIRECTORY_ENTRY_BYTE_SIZE * (i + 1)); 
       vmu_fs->vmu_file[i].is_free = false;
        
       // Set File Type
       switch (img[dir_entry_offset]) {
           case 0x33 : vmu_fs->vmu_file[i].filetype = DATA; break; 
           case 0xCC : vmu_fs->vmu_file[i].filetype = GAME; break;
           default:
               vmu_fs->vmu_file[i].is_free = true; 
               continue; 
       }
       
       // Set Copy Protection
       switch(img[dir_entry_offset + 0x1]) {
           case 0x00 : vmu_fs->vmu_file[i].copy_protected = false; break;
           case 0xFF : vmu_fs->vmu_file[i].copy_protected = true; break;
           default: 
               vmu_fs->vmu_file[i].is_free = true;
               continue;
       } 
 
       vmu_fs->vmu_file[i].starting_block = to_16bit_le(img + dir_entry_offset + 0x2);
       
       for (int j = 0; j < MAX_FILENAME_SIZE; j++) { 
            vmu_fs->vmu_file[i].filename[j] = img[dir_entry_offset + 0x4 + j]; 
       }
       vmu_fs->vmu_file[i].filename[MAX_FILENAME_SIZE] = 0;      
       
       vmu_fs->vmu_file[i].timestamp = create_timestamp(img + dir_entry_offset + 0x10);
       vmu_fs->vmu_file[i].size_in_blocks = to_16bit_le(img + dir_entry_offset + 0x18);
       vmu_fs->vmu_file[i].offset_in_blocks = to_16bit_le(img + dir_entry_offset + 0x1A);
    }

    vmu_fs->img = img;

    return 0;
}

int vmufs_rename_file(struct vmu_fs *vmu_fs, 
    const char *from, 
    const char *to) 
{
    if (strlen(from) > 0 && strstr(from, "/") == from) {
        from++;
    }
    
    if (strlen(to) > 0 && strstr(to, "/") == to) {
        to++;
    }

    if (strnlen(to, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE) {
        fprintf(stderr, "Filename \"%s\" is too large\n", to);
        return -EIO;
    } 
            
    // Same filename, don't need to do anything
    if (strncmp(from, to, MAX_FILENAME_SIZE) == 0) {
        return 0;
    }

    bool to_exists = false;
    int from_entry = -1;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        if (!vmu_fs->vmu_file[i].is_free) {
            if (from_entry < 0 && 
                strncmp(from, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) { 
                from_entry = i;
            }

            if (strncmp(to, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) {
                to_exists = true;
                break;
            }
        }
    }

    if (to_exists) {
        fprintf(stderr, "Filename \"%s\" already exists\n", to);
        return -EIO;
    }

    if (from_entry < 0) {
        fprintf(stderr, "Could not find \"%s\"\n", from);
        return -EIO;
    }

    strncpy(vmu_fs->vmu_file[from_entry].filename, to, MAX_FILENAME_SIZE);
    return 0;
}

int vmufs_read_file(const struct vmu_fs *vmu_fs, 
    const char *path, 
    uint8_t *buf, 
    size_t size, 
    uint64_t offset)
{
    int dir_entry = vmufs_get_dir_entry(vmu_fs, path);
    if (dir_entry < 0)
    {
        return -EIO;
    }

    // If the given offset is larger than the filesize or the requested
    // number of bytes to read is 0 then we don't need to do anything
    size_t file_length = vmu_fs->vmu_file[dir_entry].size_in_blocks * BLOCK_SIZE_BYTES;
    if (offset >= file_length || size == 0) {
      return 0;
    }

    size = offset + size > file_length ? file_length - offset : size;
    size_t copied = 0;

    // Read through the file until reaching the block where we need
    // to start copying
    uint16_t cur_block = vmu_fs->vmu_file[dir_entry].starting_block;
    for (int blocks_read = 0; blocks_read < offset / BLOCK_SIZE_BYTES; blocks_read++) {
    
        // Something wrong with the file in the FS shouldn't reach
        // the end, or be higher than the max number of blocks
        if (cur_block >= vmu_fs->root_block.user_block_count) {
            return -EIO;
        }

        cur_block = vmufs_next_block(vmu_fs, cur_block);
    }

    // Copy the offset into the first block til the end of that block
    int offset_bytes = offset % BLOCK_SIZE_BYTES;
    
    if (offset_bytes != 0) {
        int bytes_to_copy = BLOCK_SIZE_BYTES - offset_bytes;
        bytes_to_copy = size > bytes_to_copy ? bytes_to_copy : size;
        memcpy(buf, vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES) + offset_bytes, bytes_to_copy); 
        // We've read all that we need to
        if (bytes_to_copy == size) {
            return size;
        }

        copied += bytes_to_copy;
        cur_block = vmufs_next_block(vmu_fs, cur_block);
    }

    // Bytes to read in the last block (if not block alligned)
    int leftover_bytes = (offset + size) % BLOCK_SIZE_BYTES;

    // Copy all Full blocks
    int full_blocks = (size / BLOCK_SIZE_BYTES) + !!(size % BLOCK_SIZE_BYTES) -   
        ((offset_bytes != 0) || (leftover_bytes != 0)); 

    for (int blocks_read = 0; blocks_read < full_blocks; blocks_read++) {
        
        if (cur_block >= vmu_fs->root_block.user_block_count) {
            return -EIO;
        }

        memcpy(buf + copied, vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES), BLOCK_SIZE_BYTES);
        copied += BLOCK_SIZE_BYTES;        
        cur_block = vmufs_next_block(vmu_fs, cur_block);
    }

    // Copy the leftover bytes
    if (leftover_bytes > 0) {
        memcpy(buf + copied, vmu_fs->img + (cur_block * BLOCK_SIZE_BYTES), leftover_bytes);
    }

    return size;
}


int vmufs_write_file(struct vmu_fs *vmu_fs, 
    const char *path, 
    uint8_t *buf, 
    size_t size, 
    uint64_t offset)
{
        
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location;
    uint8_t *img = vmu_fs->img;
        
    if (strnlen(path, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE) {
        fprintf(stderr, "Filename \"%s\" is too large," 
          "  maximum filename size is %d\n", path, MAX_FILENAME_SIZE); 
        return -EIO;
    } 

    int first_free_dir_entry = -1;
    int matched_dir_entry = -1;

     /* Check if file already exists so we may be able to re-use 
      * the directory entry and allocated blocks */  
    for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) { 
        if (vmu_fs->vmu_file[i].is_free) { 
            if (first_free_dir_entry == -1) {
                first_free_dir_entry = i;
            }
            continue;
        }
         
        if (strncmp(path, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) {
            matched_dir_entry = i;
            break;                
        }
    }

    // Not enough space for the directory entry for the file
    if (first_free_dir_entry == -1 && matched_dir_entry == -1) {
        fprintf(stderr, "Ran out of space\n");
        return -EIO;
    }

    if (first_free_dir_entry != -1 && offset != 0) {
        fprintf(stderr, "Attempting to write to an offset in "
            "a non existing file\n");
    }
    
    // Calculate the total blocks needed to perform the write operation
    int offset_bytes = offset % BLOCK_SIZE_BYTES;
    int leftover_bytes = (size + offset) % BLOCK_SIZE_BYTES;
    unsigned blocks_needed = (size + offset) / BLOCK_SIZE_BYTES + (!!leftover_bytes);
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
        vmu_fs->vmu_file[first_free_dir_entry].timestamp = to_timestamp(raw_time);
         
        vmu_fs->vmu_file[first_free_dir_entry].size_in_blocks = blocks_needed;
        vmu_fs->vmu_file[first_free_dir_entry].offset_in_blocks = 0;
        matched_dir_entry = first_free_dir_entry; 
    }

    // No file content to write, we can stop here
    if (size == 0) {
        return 0;    
    }
    int blocks_written = 0;
    int last_block = -1;

    int offset_left = offset;
      
    uint16_t prev_block = 0xFFFF; 
    uint16_t cur_block = vmu_fs->vmu_file[matched_dir_entry].starting_block;

    // Skip over blocks present in an existing file 
    if (offset > 0 && first_free_dir_entry == -1) {

        while (offset_left >= BLOCK_SIZE_BYTES && cur_block != 0xFFFA) {
            cur_block = vmufs_next_block(vmu_fs, cur_block);
            prev_block = cur_block;
            offset_left -= BLOCK_SIZE_BYTES;
        }
    }

    int block_no = vmu_fs->root_block.user_block_count - 1;
    
    // Need to create blocks to skip over
    while (offset_left >= BLOCK_SIZE_BYTES) {
        
       bool block_free;
       uint8_t *next_block;
    
       // Locate next free block
       do {
          next_block = img + fat_block_addr + (block_no * 2);
          block_free = to_16bit_le(next_block) == 0xFFFC;
          if (!block_free) {
              block_no--;
          }
       } while (!block_free && block_no >= 0);

       // All blocks are being used
       if (!block_free) {
            fprintf(stderr, "Ran out of space\n");
            return -EIO;
       }

       if (prev_block == 0xFFFF) { 
            vmu_fs->vmu_file[matched_dir_entry].starting_block = block_no;
       } else {
            *(img + fat_block_addr + (prev_block * 2)) = block_no;
       }

       cur_block = block_no;
       offset_left -= BLOCK_SIZE_BYTES;
    }

    uint32_t bytes_written = 0;
     
    if (offset_left > 0) {
        memcpy(img + cur_block + offset_left, buf, BLOCK_SIZE_BYTES - offset_left);
        bytes_written += BLOCK_SIZE_BYTES - offset_left;
    } 
    
    int full_blocks = (size / BLOCK_SIZE_BYTES) + !!(size % BLOCK_SIZE_BYTES) -   
        ((offset_bytes != 0) || (leftover_bytes != 0)); 
    
    // Overwrite full blocks in an existing file    
    if (first_free_dir_entry == -1) {
        while (blocks_needed > 0 && cur_block != 0xFFFA) {
            int bytes_to_write = blocks_needed == 1 && leftover_bytes ? 
                leftover_bytes :
                BLOCK_SIZE_BYTES;
            printf("cur_block %d bytes to write %d\n", cur_block, bytes_to_write); 

            memcpy(img + (cur_block * BLOCK_SIZE_BYTES), buf + bytes_written, bytes_to_write);
            bytes_written += bytes_to_write;
            cur_block = vmufs_next_block(vmu_fs, cur_block);
            blocks_needed--;
        }
    }
    printf("bytes written %d, size %d\n", bytes_written, size);
    // Need to create blocks to write content to
    while (bytes_written < size) {
        
       bool block_free;
       uint8_t *next_block;
    
        int bytes_to_write = size - bytes_written < BLOCK_SIZE_BYTES ?
            leftover_bytes :
            BLOCK_SIZE_BYTES;
        printf("bytes to write %d\n", bytes_to_write);
       // Locate next free block
       do {
          next_block = img + fat_block_addr + (block_no * 2);
          block_free = to_16bit_le(next_block) == 0xFFFC;
          if (!block_free) {
              block_no--;
          }
       } while (!block_free && block_no >= 0);

       // All blocks are being used
       if (!block_free) {
            fprintf(stderr, "Ran out of space\n");
            return -EIO;
       }

       printf("cur_block %d next_block %d\n", cur_block, block_no);

       if (prev_block == 0xFFFF) { 
           vmu_fs->vmu_file[matched_dir_entry].starting_block = block_no;
       } else {
           write_16bit_le(img + fat_block_addr + (cur_block * 2), block_no);
       }
       prev_block = cur_block;
       cur_block = block_no;
       memcpy(img + (cur_block * BLOCK_SIZE_BYTES), img + bytes_written, bytes_to_write);
       write_16bit_le(img + fat_block_addr + (cur_block * 2), 0xFFFA);
       bytes_written += bytes_to_write;
    }

   
    uint16_t current_blocks_used = vmu_fs->vmu_file[matched_dir_entry].size_in_blocks;
    uint16_t new_blocks_used = (size + offset) / BLOCK_SIZE_BYTES + (!!leftover_bytes);

    if (new_blocks_used > current_blocks_used) {
        vmu_fs->vmu_file[matched_dir_entry].size_in_blocks = new_blocks_used;
    }
    return bytes_written;
}

int vmufs_remove_file(struct vmu_fs *vmu_fs, const char *file_name) 
{
    // File doesn't exist as filename is too large
    if (strnlen(file_name, MAX_FILENAME_SIZE + 1) > MAX_FILENAME_SIZE) {
        return -ENAMETOOLONG;
    } 

    int matched_dir_entry = -1;

     /* Locate the FAT directory entry for the file*/  
    for (int i = TOTAL_DIRECTORY_ENTRIES - 1; i >= 0; i--) { 
        if (!vmu_fs->vmu_file[i].is_free && 
            strncmp(file_name, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) {
                 matched_dir_entry = i;
                 break;
        }
    }
    
    // File not found 
    if (matched_dir_entry == -1) {
        fprintf(stderr, "Could not find \"%s\"\n", file_name);
        return -ENOENT;
    }
    
    // Mark directory entry as free as well as all the FAT blocks
    // allocated to it
    vmu_fs->vmu_file[matched_dir_entry].is_free = 1;

    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location;
    uint16_t cur_block = vmu_fs->vmu_file[matched_dir_entry].starting_block;
    int fat_addr = (cur_block * 2) + fat_block_addr;
    
    while (cur_block != 0xFFFA) {
        cur_block = to_16bit_le(vmu_fs->img + fat_addr);
        int next = fat_block_addr + (cur_block * 2); 
        write_16bit_le(vmu_fs->img + fat_addr, 0xFFFC);
        fat_addr = next;
    } 
      
    return 0;
}



