#include "vmu_tests.h"
#include "vmu_driver_read_tests.h"
#include "../src/vmu_driver.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdint>

INSTANTIATE_TEST_CASE_P(VmuFsTest, VmuValidFsTest, 
    testing::Values(new ValidVmuFsExpected("../vmu_a.bin", 5, true, 254, 1, 253, 13, 200)));

// Test Root Block is read from the FS correctly
TEST_P(VmuValidFsTest, RootBlockReadCorrect) {

    ValidVmuFsExpected *expected = (ValidVmuFsExpected *)GetParam();
    ASSERT_EQ(expected->custom_vms_color, vmu_fs.root_block.custom_vms_color);
    ASSERT_EQ(expected->fat_location, vmu_fs.root_block.fat_location);
    ASSERT_EQ(expected->fat_size, vmu_fs.root_block.fat_size);
    ASSERT_EQ(expected->directory_location, vmu_fs.root_block.directory_location);
    ASSERT_EQ(expected->directory_size, vmu_fs.root_block.directory_size);
    ASSERT_EQ(expected->user_block_count, vmu_fs.root_block.user_block_count);
}

// Check the correct amount of files are found in the FS
TEST_P(VmuValidFsTest, CountsFSCorrect) {
 
    int file_count = 0;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        file_count += !vmu_fs.vmu_file[i].is_free; 
    }
     
    ValidVmuFsExpected *expected = (ValidVmuFsExpected *)GetParam();
    ASSERT_EQ(expected->file_count, file_count);   
}


INSTANTIATE_TEST_CASE_P(VmuValidDirEntriesTest, VmuValidDirTest, 
    testing::Values(new ValidVmuDirEntriesExpected("../vmu_a.bin", 
        std::vector<ValidVmuDirEntryExpected>{
            ValidVmuDirEntryExpected("SONIC2___S01", DATA, false, 199, 18, 0),
            ValidVmuDirEntryExpected("SONICADV_INT", DATA, false, 181, 10, 0),
            ValidVmuDirEntryExpected("EVO_DATA.001", DATA, false, 171,  8, 0),
            ValidVmuDirEntryExpected("SONIC2___S01", DATA, false, 163, 18, 0),
            ValidVmuDirEntryExpected("SONICADV_INT", DATA, false, 145, 10, 0)}
        )));


// Check entries from the directory block are read correctly
TEST_P(VmuValidDirTest, ReadsDirCorrect) {

    ValidVmuDirEntriesExpected *expected = (ValidVmuDirEntriesExpected *)GetParam();
    std::vector<ValidVmuDirEntryExpected> entries = expected->dir_entries;

    int file_count = 0;
    for (int i = 0; i < TOTAL_DIRECTORY_ENTRIES; i++) {
        if (!vmu_fs.vmu_file[i].is_free) {
            struct vmu_file vmu_file = vmu_fs.vmu_file[i];
            int j;
            bool found = false;
            for (j = 0; j < entries.size(); j++) {
                if (entries[j].vmu_file.starting_block == vmu_file.starting_block) {
                    found = true;
                    break;
                }
            }

            if (!found) {
                FAIL() << "Found unexpected file starting at block " << 
                    vmu_file.starting_block;
            }
            ASSERT_STREQ(entries[j].vmu_file.filename, vmu_file.filename);
            ASSERT_EQ(entries[j].vmu_file.filetype, vmu_file.filetype);
            ASSERT_EQ(entries[j].vmu_file.copy_protected, vmu_file.copy_protected);
            ASSERT_EQ(entries[j].vmu_file.size_in_blocks, vmu_file.size_in_blocks);
            ASSERT_EQ(entries[j].vmu_file.offset_in_blocks, vmu_file.offset_in_blocks);
        
            entries.erase(entries.begin() + j);
        }
       }
        
        if (entries.size() > 0) {
            FAIL() << "Was unable to locate " << entries.size() << " entry(s) in the filesystem";
        }
}


INSTANTIATE_TEST_CASE_P(VmuValidReadTest, VmuValidReadFileTest, 
    testing::Values(
        new ValidVmuReadEntry("../vmu_a.bin", "SONIC2___S01", 0, BLOCK_SIZE_BYTES, 199, 18),
        new ValidVmuReadEntry("../vmu_a.bin", "SONIC2___S01", 0, BLOCK_SIZE_BYTES * 18, 199, 18),
        new ValidVmuReadEntry("../vmu_a.bin", "SONICADV_INT", 28, BLOCK_SIZE_BYTES, 181, 10),
        new ValidVmuReadEntry("../vmu_a.bin", "EVO_DATA.001", 524, 24, 171, 8),
        new ValidVmuReadEntry("../vmu_a.bin", "EVO_DATA.001", BLOCK_SIZE_BYTES * 7, 256, 171, 8),
        new ValidVmuReadEntry("../vmu_a.bin", "SONICADV_INT", 1004, 1024, 181, 10)
        ));



// Checks that blocks can be read from files correctly
TEST_P(VmuValidReadFileTest, ReadsBlocksCorrect) {
   
    ValidVmuReadEntry *entry = (ValidVmuReadEntry *)GetParam();

    uint8_t *buf = new uint8_t[entry->size_to_read];
    int bytes_read =vmufs_read_file(&vmu_fs, entry->dir_entry_name, buf, 
        entry->size_to_read, entry->offset_in_file);  
    
    // Check correct number of bytes have been reported as read
    ASSERT_EQ(entry->size_to_read, bytes_read);

    // Traverse to the block where we should of started reading from
    int cur_block_no = entry->file_start_block;
    for (int i = 0; i < entry->offset_in_file / BLOCK_SIZE_BYTES; i++)
    {
        cur_block_no = vmufs_next_block(&vmu_fs, cur_block_no);
    }
    uint32_t offset = cur_block_no * BLOCK_SIZE_BYTES + (entry->offset_in_file % BLOCK_SIZE_BYTES);
    
    int i = 0;
    while (i < bytes_read) {
        do {
        
            uint32_t actual;
            memcpy(&actual, buf + i, sizeof(uint32_t));

            uint32_t expected;
            memcpy(&expected, vmu_fs.img + offset, sizeof(uint32_t));

            ASSERT_EQ(expected, actual);
            i += sizeof(uint32_t);
            offset += sizeof(uint32_t);
        } while (i < bytes_read && (offset % BLOCK_SIZE_BYTES != 0));

        cur_block_no = vmufs_next_block(&vmu_fs, cur_block_no);
        offset = cur_block_no * BLOCK_SIZE_BYTES;
    }

    delete[] buf;
}
