#ifndef VMU_DRIVER_READ_TESTS_H
#define VMU_DRIVER_READ_TESTS_H

#include <gtest/gtest.h>
#include "vmu_tests.h"
#include "../vmu_driver.h"

class ValidVmuFs { 
 public:
    const char *file_name;
};

class ValidVmuFsExpected : public ValidVmuFs {
 public:
    int file_count;
    bool custom_vms_color;
    int fat_location;
    int fat_size;
    int directory_location;
    int directory_size;
    int user_block_count;

    ValidVmuFsExpected(const char *fname, int fcount, bool vms_color, int f_location,
        int f_size, int d_location, int d_size, int u_block_count) {
        file_name = fname;
        file_count = fcount;
        custom_vms_color = vms_color;
        fat_location = f_location;
        fat_size = f_size;
        directory_location = d_location;
        directory_size = d_size;
        user_block_count = u_block_count;
    }
};

class ValidVmuDirEntryExpected {
 public:
    struct vmu_file vmu_file;
    
    ValidVmuDirEntryExpected(const char *name, enum filetype filetype, bool copy_protected, 
        int starting_block, int size_in_blocks, int offset_in_blocks) {
        vmu_file.is_free = false;
        strncpy(vmu_file.filename, name, 13);
        vmu_file.filetype = filetype;
        vmu_file.copy_protected = copy_protected;
        vmu_file.starting_block = starting_block;
        vmu_file.size_in_blocks = size_in_blocks;
        vmu_file.offset_in_blocks = offset_in_blocks; 
    }
};

class ValidVmuDirEntriesExpected : public ValidVmuFs {
 public:
    std::vector<ValidVmuDirEntryExpected> dir_entries;
    ValidVmuDirEntriesExpected(const char *f_name, std::vector<ValidVmuDirEntryExpected> d_entries) {
        file_name = f_name;     
        dir_entries = d_entries;
    }    
};    

class VmuValidFsTest : public ::testing::TestWithParam<ValidVmuFs *>
{
 protected:
  struct vmu_fs vmu_fs;
  uint8_t *file;  

  virtual void SetUp() {
    long file_len;
    file = read_file(GetParam()->file_name, &file_len);
    
    if (file == NULL) {
        FAIL() << "Unable to open file: " << GetParam()->file_name;
    }
    
    if (read_fs(file, file_len, &vmu_fs) != 0) {
        FAIL() << "Failed to read FS from: " << GetParam()->file_name;
    } 
  }

  virtual void TearDown() {
    free(file);
  }

};

class VmuValidDirTest : public VmuValidFsTest {};

#endif
