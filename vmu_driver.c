#include "vmu_driver.h"

#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

int stat_vmu_fs(const struct vmu_fs *vmu_fs, const char *path, struct stat *stbuf) {
   
   if (strnlen(path, MAX_FILENAME_SIZE) >= MAX_FILENAME_SIZE) {
       return -ENOENT;
   } 
  
   for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES ; i++) {
        if (vmu_fs->vmu_file[i].is_free) 
            continue;

        if (strncmp(path, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) {
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_nlink = 1;
            stbuf->st_size = vmu_fs->vmu_file[i].size_in_blocks * BLOCK_SIZE_BYTES;
            return 0;    
        }
   }

   return -ENOENT;
}   

static uint16_t to_16bit_le(const uint8_t *img) {
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

int read_fs(uint8_t *img, const unsigned length, struct vmu_fs *vmu_fs) {
    
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

  
int write_file(struct vmu_fs *vmu_fs, 
    const char *file_name, 
    const uint8_t *file_contents, 
    const unsigned file_length) 
{
        
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location;
    uint8_t *img = vmu_fs->img;
        
    if (strlen(file_name) > MAX_FILENAME_SIZE) {
        return -EIO;
    } 

    int first_free_dir_entry = -1;
    int matched_dir_entry = -1;

     /* Check if file already exists so we may be able to re-use 
      * the directory entry and allocated blocks */  
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) { 
        if (vmu_fs->vmu_file[i].is_free) { 
            if (first_free_dir_entry != -1) {
                first_free_dir_entry = i;
            }
            continue;
        }
          
        if (strncmp(file_name, vmu_fs->vmu_file[i].filename, MAX_FILENAME_SIZE) == 0) {
            matched_dir_entry = i;
            break;                
        }
    }

    // Not enough space for the directory entry for the file
    if (first_free_dir_entry == -1 && matched_dir_entry == -1) {
        return -EIO;
    }

    // Calculate the total blocks needed to perform the write operation
    unsigned leftovers = file_length % BLOCK_SIZE_BYTES;
    unsigned blocks_needed = file_length / BLOCK_SIZE_BYTES + !!leftovers;
    int starting_block_set = 1;

    if (leftovers == 0) {
        leftovers = BLOCK_SIZE_BYTES;
    }

    // Set the directory information if writing a new file
    if (first_free_dir_entry != -1) {
        starting_block_set = 0;

        vmu_fs->vmu_file[first_free_dir_entry].is_free = 0;
        vmu_fs->vmu_file[first_free_dir_entry].filetype = DATA;
        vmu_fs->vmu_file[first_free_dir_entry].copy_protected = false;
        strncpy(vmu_fs->vmu_file[first_free_dir_entry].filename, 
            file_name, MAX_FILENAME_SIZE + 1);
        //vmu_fs->vmu_file[first_free_dir_entry].timestamp = -1; 
        vmu_fs->vmu_file[first_free_dir_entry].size_in_blocks = blocks_needed;
        vmu_fs->vmu_file[first_free_dir_entry].offset_in_blocks = 0; 
    }

    int blocks_written = 0;
    int last_block = -1;

    // Overwrite blocks in an existing file     
    if (matched_dir_entry != -1) {
        int fat_addr = (vmu_fs->vmu_file[matched_dir_entry].starting_block * 2) + 
                fat_block_addr;
        
        uint16_t cur_block = to_16bit_le(img + fat_addr);
        if (blocks_needed != 0) {
            do {    
                int bytes_to_copy = blocks_needed > blocks_written + 1 
                    ? BLOCK_SIZE_BYTES : leftovers; 

                memcpy(img + (cur_block * BLOCK_SIZE_BYTES), 
                    file_contents + (blocks_written * BLOCK_SIZE_BYTES), 
                    bytes_to_copy); 

                fat_addr = (cur_block * 2) + fat_block_addr;
                last_block = cur_block;
                cur_block = to_16bit_le(img + fat_addr);
                blocks_written++;            
            } while (cur_block != 0xFFFA && blocks_written < blocks_needed);
        }

        // Overwritten file is smaller than the original
        // Need to mark the "extra" blocks as free memory
        while (blocks_written < vmu_fs->vmu_file[matched_dir_entry].size_in_blocks) {
            cur_block = to_16bit_le(img + fat_addr);
            int next =  fat_block_addr + (cur_block * 2); 
            write_16bit_le(img + fat_addr, 0xFFFA);
            fat_addr = next;
            blocks_written++;        
        }
    }

    // Write either entire new file, or extra data if file being saved
    // over an existing file is larger than the existing file
    for (int block_no = vmu_fs->root_block.user_block_count - 1; 
        blocks_written < blocks_needed && block_no >= 0; 
        block_no--) {
        
        // Block is already allocated, try the next
        if (to_16bit_le(img + fat_block_addr + (block_no * 2)) != 0xFFFA) {
            continue;
        }     

        int bytes_to_copy = blocks_needed > blocks_written + 1 ? BLOCK_SIZE_BYTES : leftovers; 
        memcpy(img + (block_no * BLOCK_SIZE_BYTES), 
            file_contents + (blocks_written * BLOCK_SIZE_BYTES),
            bytes_to_copy);

        if (first_free_dir_entry != -1 && !starting_block_set) { 
            vmu_fs->vmu_file[first_free_dir_entry].starting_block = block_no; 
        }
        if (last_block != -1) {
            write_16bit_le(img + fat_block_addr + (last_block * 2), block_no);
        }
        last_block = block_no;
        // In the FAT write that the block is the end block 
        // (although it might not be the end, if so will be overwritten in
        //  the next iteration)
        write_16bit_le(img + fat_block_addr + (block_no * 2), 0xFFFA);
        blocks_written++;
    }           

    // Not enough space to write the file contents
    if (blocks_written < blocks_needed) {
        return -EIO;
    }
    
    return 0;
}
