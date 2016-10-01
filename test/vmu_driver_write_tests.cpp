#include <gtest/gtest.h>
#include "vmu_driver_write_tests.h"
#include "../vmu_driver.h"
#include "vmu_tests.h"
#include <cstdio>
#include <cstdint>

/*
class VmuFsWriteFile { 
 public:
    const char *vmu_file_name;
    const char *write_file_name;
}
;
class VmuWriteFsTest : public ::testing::TestWithParam<VmuFsWriteFile *>
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

};*/

