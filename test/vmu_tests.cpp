#include "vmu_tests.h"
#include "vmu_driver_write_tests.h"
#include "vmu_driver_read_tests.h"
#include "../src/vmu_driver.h"
#include <gtest/gtest.h>

uint8_t *read_file(const char *file_path, long *file_len) {
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

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new VmuFsEnvironment());
  return RUN_ALL_TESTS();
}
