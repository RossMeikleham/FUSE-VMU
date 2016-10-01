#include "vmu_tests.h"
#include "vmu_driver_read_tests.h"
#include <gtest/gtest.h>
#include "../vmu_driver.h"
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
