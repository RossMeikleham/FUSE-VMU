#ifndef VMU_DRIVER_WRITE_TESTS_H
#define VMU_DRIVER_WRITE_TESTS_H

#include <gtest/gtest.h>
#include "vmu_tests.h"
#include "../src/vmu_driver.h"

class VmuFsWriteFile { 
 public:
    const char *vmu_file_name;
    const char *write_file_name;

    VmuFsWriteFile(const char *v_file_name, const char *w_file_name) {
        vmu_file_name = v_file_name;
        write_file_name = w_file_name;
    }
};


class VmuWriteFsTest : public ::testing::TestWithParam<VmuFsWriteFile *>
{
 protected:
  struct vmu_fs vmu_fs;
  uint8_t *vmu_file;
  uint8_t *write_file_contents;  

  virtual void SetUp() {
    long file_len, file2_len;
    vmu_file = read_file(GetParam()->vmu_file_name, &file_len);
    write_file_contents = read_file(GetParam()->write_file_name, &file2_len);

    if (vmu_file == NULL) {
        FAIL() << "Unable to open file: " << GetParam()->vmu_file_name;
    } 

    if (write_file_contents == NULL) {
        FAIL() << "Unable to open file: " << GetParam()->write_file_name;
    }
    
    if (vmufs_read_fs(vmu_file, file_len, &vmu_fs) != 0) {
        FAIL() << "Failed to read FS from: " << GetParam()->vmu_file_name;
    }  
  }

  virtual void TearDown() {
    free(vmu_file);
    free(write_file_contents);
  }

};


#endif
