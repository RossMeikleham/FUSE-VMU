#include <gtest/gtest.h>

#include "vmu_tests.h"
#include "vmu_driver_write_tests.h"
#include "vmu_driver_read_tests.h"
#include "../vmu_driver.h"


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new VmuFsEnvironment());
  return RUN_ALL_TESTS();
}
