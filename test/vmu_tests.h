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

inline uint8_t *read_file(const char *file_path, long *file_len) 
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

#endif
