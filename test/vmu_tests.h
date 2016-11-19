#ifndef VMU_DRIVER_TESTS_H
#define VMU_DRIVER_TESTS_H

#include <gtest/gtest.h>
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

uint8_t *read_file(const char *file_path, long *file_len);

#endif
