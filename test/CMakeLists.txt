cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(fuse_vmu_tests VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall --pedantic -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_FILE_OFFSET_BITS=64 -std=c++11")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall --pedantic -g")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../CMake" ${CMAKE_MODULE_PATH})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

find_package(FUSE REQUIRED)

include_directories(/usr/local/include)
include_directories(${FUSE_INCLUDE_DIR})

add_executable(fuse_vmu_tests vmu_tests.cpp vmu_driver_read_tests.cpp 
    vmu_driver_write_tests.cpp ../src/vmu_driver.c)
target_link_libraries(fuse_vmu_tests /usr/local/lib/libgtest.a /usr/local/lib/libgtest_main.a pthread)
