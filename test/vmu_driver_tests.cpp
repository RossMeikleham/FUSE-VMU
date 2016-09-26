#include <gtest/gtest.h>
#include "../vmu_driver.h"
#include <cstdio>
#include <cstdint>

class VmuFsEnvironment : public ::testing::Environment 
{
 public:
  virtual void SetUp() {
  }

  virtual void TearDown() {
  }
};

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


uint8_t *read_file(const char *file_path, long *file_len) 
{
    FILE *fileptr;
    uint8_t *buffer;

    fileptr = fopen(file_path, "rb");
    if (fileptr == NULL) {
        return NULL;  
    }
    fseek(fileptr, 0, SEEK_END);       
    *file_len = ftell(fileptr);         
    rewind(fileptr);                 

    buffer = (uint8_t *)malloc((*file_len) * sizeof(uint8_t)); 
    
    if (buffer == NULL) {
        return NULL;
    }
    
    fread(buffer, *file_len, 1, fileptr); 
    fclose(fileptr);
    
    return buffer; 
}

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


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new VmuFsEnvironment());
  return RUN_ALL_TESTS();
}
