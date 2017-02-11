#include "vmu_tests.h"
#include "vmu_driver_write_tests.h"
#include "../src/vmu_driver.h"

#include <cstdio>
#include <cstdint>
#include <gtest/gtest.h>


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


static int get_allocated_blocks(struct vmu_fs *vmu_fs) {

    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs->root_block.fat_location;
    int blocks_unallocated = 0;

    for (int entry = 0; entry < vmu_fs->root_block.user_block_count; entry++) {
        uint16_t fat_value = to_16bit_le(vmu_fs->img + fat_block_addr + (entry * 2));
        if (fat_value == 0xFFFC) {
            blocks_unallocated++;
        }
    } 
    
    return vmu_fs->root_block.user_block_count - blocks_unallocated;    
}


// Tests that writing a single new file works correctly
TEST_P(VmuWriteFsTest, CorrectlyNormalWrites) {
    
    const int fat_block_addr = BLOCK_SIZE_BYTES * vmu_fs.root_block.fat_location;

    ASSERT_EQ(3, get_filecount(&vmu_fs));

    int res = vmufs_write_file(&vmu_fs, "SONIC2__S03", write_file_contents, 
        BLOCK_SIZE_BYTES * 18, 0);   
    ASSERT_EQ(BLOCK_SIZE_BYTES * 18, res);

    ASSERT_EQ(4, get_filecount(&vmu_fs));

    bool correct_starting_block = false;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        if (!vmu_fs.vmu_file[i].is_free) {
            printf("starting block %d\n", vmu_fs.vmu_file[i].starting_block);
            if (vmu_fs.vmu_file[i].starting_block == 199) {
                correct_starting_block = true;
                break;
            }
        }
    }

    if (!correct_starting_block) {
        FAIL() << "File wasn't written to the correct starting block\n";
    }

    ASSERT_EQ(46, get_allocated_blocks(&vmu_fs));
}


// Tests that a full filesystem cannot have files written to it
TEST_P(VmuWriteFsTest, FailsWhenFull) {
  
    char buf[MAX_FILENAME_SIZE + 1];

    int i;
    for (i = 0; i < 9; i++) {
        strcpy(buf, "SONIC2___S0");
        buf[MAX_FILENAME_SIZE - 1] = (char)(i + 48);
        buf[MAX_FILENAME_SIZE] = '\0'; 
        ASSERT_EQ(BLOCK_SIZE_BYTES * 18, vmufs_write_file(&vmu_fs, buf, write_file_contents, 
            BLOCK_SIZE_BYTES * 18, 0));
    }
        
    strcpy(buf, "SONIC2___S0");
    buf[MAX_FILENAME_SIZE - 1] = (char)(i + 48);
    buf[MAX_FILENAME_SIZE] = '\0'; 

    ASSERT_EQ(-ENOSPC, vmufs_write_file(&vmu_fs, buf, write_file_contents, 
        BLOCK_SIZE_BYTES * 18, 0));    
} 


// Tests that overwriting a file with a file of equal size works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesEqualSize) {
     
    int write_size = BLOCK_SIZE_BYTES * 18;
     
    ASSERT_EQ(write_size, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, 
        write_size, 0));

    ASSERT_EQ(write_size, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, 
        write_size, 0));
    ASSERT_EQ(4, get_filecount(&vmu_fs));
    ASSERT_EQ(46, get_allocated_blocks(&vmu_fs));
}



// Tests that overwriting the start of a file works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesStart) {
    
    ASSERT_EQ(BLOCK_SIZE_BYTES * 18, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18, 0));
    ASSERT_EQ(BLOCK_SIZE_BYTES * 7, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 7, 0));  
    ASSERT_EQ(4, get_filecount(&vmu_fs));
    ASSERT_EQ(46, get_allocated_blocks(&vmu_fs)); 
}


// Tests that overwriting a file with a larger file works correctly
TEST_P(VmuWriteFsTest, CorrectlyOverwritesLargerSize) {
    
    ASSERT_EQ(BLOCK_SIZE_BYTES * 5, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 5, 0));
    ASSERT_EQ(BLOCK_SIZE_BYTES * 18, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, BLOCK_SIZE_BYTES * 18, 0));
    ASSERT_EQ(4, get_filecount(&vmu_fs));
    ASSERT_EQ(46, get_allocated_blocks(&vmu_fs));
}

TEST_P(VmuWriteFsTest, CorrectlyCreatesThenWritesFile) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);

    ASSERT_EQ(0, vmu_fs_create_file(&vmu_fs, "FILE"));
    ASSERT_EQ(BLOCK_SIZE_BYTES * 18, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, 
        BLOCK_SIZE_BYTES * 18, 0)); 
    ASSERT_EQ(before_blocks + 18, get_allocated_blocks(&vmu_fs)); 
   
    printf("block count %d\n", vmu_fs.root_block.user_block_count); 
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "FILE");
    ASSERT_NE(-1, dir_entry);
    ASSERT_NE(0xFFFA, vmu_fs.vmu_file[dir_entry].starting_block);
}

// Tests that removing an existing file works correctly
TEST_P(VmuWriteFsTest, CorrectlyRemovesIndividualFile) {

    ASSERT_EQ(0, vmufs_remove_file(&vmu_fs, "SONICADV_INT")); 
    ASSERT_EQ(2, get_filecount(&vmu_fs));
    ASSERT_EQ(18, get_allocated_blocks(&vmu_fs)); 
}

// Tests that removing all files from the filesystem works correctly
TEST_P(VmuWriteFsTest, CorrectlyRemovesAllFiles) {
    
    ASSERT_EQ(0, vmufs_remove_file(&vmu_fs, "EVO_DATA.001")); 
    ASSERT_EQ(20, get_allocated_blocks(&vmu_fs)); 
    ASSERT_EQ(0, vmufs_remove_file(&vmu_fs, "SONICADV_INT")); 
    ASSERT_EQ(10, get_allocated_blocks(&vmu_fs)); 
    ASSERT_EQ(0, vmufs_remove_file(&vmu_fs, "SONICADV_INT")); 
    ASSERT_EQ(0, get_allocated_blocks(&vmu_fs)); 
}


// Test that removing a non existing file fails
TEST_P(VmuWriteFsTest, FailsToRemoveNonExistingFile) {

    ASSERT_NE(0, vmufs_remove_file(&vmu_fs, "DOESNT_EXIST")); 
}

// Test that writing a file then removing is works correctly
TEST_P(VmuWriteFsTest, CorrectlyWritesThenRemovesFile) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);

    ASSERT_EQ(BLOCK_SIZE_BYTES * 18, vmufs_write_file(&vmu_fs, "FILE", write_file_contents, 
        BLOCK_SIZE_BYTES * 18, 0)); 
    ASSERT_EQ(0, vmufs_remove_file(&vmu_fs, "FILE")); 
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs)); 
}


// Test that renaming a file works correctly
TEST_P(VmuWriteFsTest, CorrectlyRenamesFile) {
    
    // Check operation returns sucessfully
    ASSERT_EQ(0, vmufs_rename_file(&vmu_fs, "EVO_DATA.001", "TEST"));
    
    // Check it has been renamed in the fs
    bool found = false;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        if (!vmu_fs.vmu_file[i].is_free) {
            if (strcmp(vmu_fs.vmu_file[i].filename, "TEST") == 0) {
                found = true;
                break;
            } 
        }
    }

    ASSERT_EQ(true, found);
}

// Test that renaming a non-existant file doesn't succeed
TEST_P(VmuWriteFsTest, DoesntRenameNonExisting) {
    ASSERT_NE(0, vmufs_rename_file(&vmu_fs, "NOPE", "TEST")); 
}

// Test that renaming a file to one that doesn't exist doesn't succeed
TEST_P(VmuWriteFsTest, DoesntRenameToExisting) {
    ASSERT_NE(0, vmufs_rename_file(&vmu_fs, "EVO_DATA.001", "SONICADV_INT"));
}

// Test that truncating a file to the exact same size works correctly
TEST_P(VmuWriteFsTest, TruncatesSameSizeCorrectly) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 8, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 8));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs));
}

// Test that truncating a file which has the same block size works correctly
TEST_P(VmuWriteFsTest, TruncatesSameBlockSizeCorrectly) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 8, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 8 - 217));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs));
}

TEST_P(VmuWriteFsTest, TruncatesSmallerCorrectly0) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 7, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 7));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks - 1, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(7, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesSmallerCorrectly1) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks - 7, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(1, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesSmallerCorrectly2) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks - 7, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(1, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesSmallerCorrectly3) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 2, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 2 - 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks - 6, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(2, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesToZeroCorrectly) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(0, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 0));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks - 8, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(0, vmu_fs.vmu_file[dir_entry].size_in_blocks);
    ASSERT_EQ(0xFFFA, vmu_fs.vmu_file[dir_entry].starting_block);
}

TEST_P(VmuWriteFsTest, TruncatesEqualCorrectly0) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 8, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 8));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs));

    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(8, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}


TEST_P(VmuWriteFsTest, TruncatesEqualCorrectly1) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 8, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 8 - 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs));

    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(8, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}


TEST_P(VmuWriteFsTest, TruncatesEqualCorrectly2) {

    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 8, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 7 + 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks, get_allocated_blocks(&vmu_fs));

    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(8, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesLargerCorrectly0) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 10, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 10));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks + 2, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(10, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesLargerCorrectly1) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 10, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 10 - 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks + 2, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(10, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesLargerCorrectly3) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * 10, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 9 + 1));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks + 2, get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(10, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}


// Assert that truncating the file to all the available space
// works correctly
TEST_P(VmuWriteFsTest, TruncatesUpToAvailableBlocksCorrectly) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    int available_blocks = vmu_fs.root_block.user_block_count - before_blocks;
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * (available_blocks + 8), 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * (available_blocks + 8)));

    // Check entire FS has been allocated
    ASSERT_EQ(vmu_fs.root_block.user_block_count , get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(available_blocks + 8, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

// Assert that truncating the file to larger than the available space
// truncates it to the available space
TEST_P(VmuWriteFsTest, TruncatesMoreThanAvailableBlocksCorrectly) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    int available_blocks = vmu_fs.root_block.user_block_count - before_blocks;
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(BLOCK_SIZE_BYTES * (available_blocks + 8), 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * (available_blocks + 8 + 1)));

    // Check entire FS has been allocated
    ASSERT_EQ(vmu_fs.root_block.user_block_count , get_allocated_blocks(&vmu_fs));

    
    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(available_blocks + 8, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}

TEST_P(VmuWriteFsTest, TruncatesFromZeroCorrectly) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", 0);

    ASSERT_EQ(BLOCK_SIZE_BYTES * 10, 
        vmufs_truncate_file(&vmu_fs, "EVO_DATA.001", BLOCK_SIZE_BYTES * 10));

    // Check entire FS has same total blocks allocated
    ASSERT_EQ(before_blocks + 2, get_allocated_blocks(&vmu_fs));

    int dir_entry = vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001");
    ASSERT_EQ(10, vmu_fs.vmu_file[dir_entry].size_in_blocks);
}


// Remove tests

TEST_P(VmuWriteFsTest, RemovesExistingCorrectly) {
    
    int before_blocks = get_allocated_blocks(&vmu_fs);
    
    // Check operation returns correct number of block bytes of the new
    // file size
    ASSERT_EQ(0, 
        vmufs_remove_file(&vmu_fs, "EVO_DATA.001"));

    // Check entire FS doesn't have the blocks allocated
    ASSERT_EQ(before_blocks - 8, get_allocated_blocks(&vmu_fs));

    // Check entry is no longer present in the directory
    ASSERT_EQ(-1, vmufs_get_dir_entry(&vmu_fs, "EVO_DATA.001"));
}

TEST_P(VmuWriteFsTest, RemovesNonExistingReturnsError) {
    
    ASSERT_EQ(-ENOENT, 
        vmufs_remove_file(&vmu_fs, "Test"));

}
