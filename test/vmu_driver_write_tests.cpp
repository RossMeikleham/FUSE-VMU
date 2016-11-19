#include <gtest/gtest.h>
#include "vmu_driver_write_tests.h"
#include "../vmu_driver.h"
#include <gtest/gtest.h>
#include "vmu_tests.h"
#include "vmu_driver_write_tests.h"
#include "../vmu_driver.h"
#include <cstdio>
#include <cstdint>


INSTANTIATE_TEST_CASE_P(VmuFsWriteTest, VmuWriteFsTest, 
    testing::Values(new VmuFsWriteFile("../vmu_b.bin", "../sa2.dci")));
 

// Obtain the total number of files in the filesystem
static int get_filecount(struct vmu_fs *vmu_fs) {
    int file_count = 0;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        file_count += !vmu_fs->vmu_file[i].is_free; 
    }
    return file_count;
}

// Tests that writing a single file works correctly
TEST_P(VmuWriteFsTest, CorrectlyNormalWrites) {
   
    ASSERT_EQ(3, get_filecount(&vmu_fs));

    int res = write_file(&vmu_fs, "SONIC2__S01", write_file_contents, BLOCK_SIZE_BYTES * 18);   
    ASSERT_EQ(res, 0);

    ASSERT_EQ(4, get_filecount(&vmu_fs));

    bool correct_starting_block = false;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        if (!vmu_fs.vmu_file[i].is_free) {
            if (vmu_fs.vmu_file[i].starting_block == 199) {
                correct_starting_block = true;
                break;
            }
        }
    }

    if (!correct_starting_block) {
        FAIL() << "File wasn't written to the correct starting block\n";
    }

    int blocks_unallocated = 0;
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs.root_block.fat_location;
    
    for (int entry = 0; entry < vmu_fs.root_block.user_block_count; entry++) {
        uint16_t fat_value = to_16bit_le(vmu_fs.img + fat_block_addr + (entry * 2));
        if (fat_value == 0xFFFC) {
            blocks_unallocated++;
        }
    } 

    ASSERT_EQ(46, vmu_fs.root_block.user_block_count - blocks_unallocated);    
}

// Tests that a full filesystem cannot have files written to it
TEST_P(VmuWriteFsTest, FailsWhenFull) {
  
    char buf[MAX_FILENAME_SIZE + 1];

    int i;
    for (i = 0; i < 9; i++) {
        strcpy(buf, "SONIC2___S0");
        buf[MAX_FILENAME_SIZE - 1] = (char)(i + 48);
        buf[MAX_FILENAME_SIZE] = '\0'; 
        ASSERT_EQ(0, write_file(&vmu_fs, buf, write_file_contents, BLOCK_SIZE_BYTES * 18));
    }
        
    strcpy(buf, "SONIC2___S0");
    buf[MAX_FILENAME_SIZE - 1] = (char)(i + 48);
    buf[MAX_FILENAME_SIZE] = '\0'; 

    ASSERT_NE(0, write_file(&vmu_fs, buf, write_file_contents, BLOCK_SIZE_BYTES * 18));      
} 

// Tests that overwriting a file with a file of equal size works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesEqualSize) {
     
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18));
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18));
   
    ASSERT_EQ(4, get_filecount(&vmu_fs));

    int blocks_unallocated = 0;
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs.root_block.fat_location;
    
    for (int entry = 0; entry < vmu_fs.root_block.user_block_count; entry++) {
        uint16_t fat_value = to_16bit_le(vmu_fs.img + fat_block_addr + (entry * 2));
        if (fat_value == 0xFFFC) {
            blocks_unallocated++;
        }
    } 

    ASSERT_EQ(46, vmu_fs.root_block.user_block_count - blocks_unallocated);    

}

// Tests that overwriting a file with a smaller file works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesSmallerSize) {
    
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18));
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 7));
   
    ASSERT_EQ(4, get_filecount(&vmu_fs));

    int blocks_unallocated = 0;
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs.root_block.fat_location;
    
    for (int entry = 0; entry < vmu_fs.root_block.user_block_count; entry++) {
        uint16_t fat_value = to_16bit_le(vmu_fs.img + fat_block_addr + (entry * 2));
        if (fat_value == 0xFFFC) {
            blocks_unallocated++;
        }
    } 

    ASSERT_EQ(35, vmu_fs.root_block.user_block_count - blocks_unallocated);    
}

// Tests that overwriting a file with a larger file works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesLargerSize) {
    
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 5));
    ASSERT_EQ(0, write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18));
   
    ASSERT_EQ(4, get_filecount(&vmu_fs));

    int blocks_unallocated = 0;
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs.root_block.fat_location;
    
    for (int entry = 0; entry < vmu_fs.root_block.user_block_count; entry++) {
        uint16_t fat_value = to_16bit_le(vmu_fs.img + fat_block_addr + (entry * 2));
        if (fat_value == 0xFFFC) {
            blocks_unallocated++;
        }
    } 

    ASSERT_EQ(46, vmu_fs.root_block.user_block_count - blocks_unallocated);    
}
