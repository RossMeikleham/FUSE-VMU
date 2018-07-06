# FUSE VMU
[![Build Status](https://travis-ci.org/RossMeikleham/FUSE-VMU.svg?branch=master)](https://travis-ci.org/RossMeikleham/FUSE-VMU)

A FUSE ([Filesystem in userspace](https://en.wikipedia.org/wiki/Filesystem_in_Userspace)) 
implementation of the Sega Dreamcast's VMU ([Visual Memory Unit](https://en.wikipedia.org/wiki/VMU)) for Linux and OSX. 

# Design Decisions
The VMU filesystem is similar to the FAT filesystem except that
it is entirely flat (no directories). Also in a FAT filesystem 
the size of a file in bytes is stored in the directory entry for the 
given file; which unfortunately isn't true for the VMU filesystem. 
The VMU assumes all files are "VMS" files which have a "header block" 
which contains the full filesize. I decided on  making this implementation 
somewhat practical which means files aren't assumed to be VMS files
and as result there is only information on the block size of a file. 
So the reported filesize of a given file is a multiple of a blocksize on 
disk (multiples of 512 bytes). I may decide to change this at some point.

# Required
- Linux : FUSE Kernel Module enabled
- OSX : [OSXFuse]('http://osxfuse.github.com/')

# Docker Builds
- `docker build -t fuse-vmu .` 
- Produces a docker image in which the fuse_vmu binary is in the `/usr/bin` folder

# Native Builds

## Required
- Linux : libfuse userspace library
- gcc or clang version which supports C99
- cmake
- (If running unit tests a version of gcc or clang which supports C++11)
- (If running unit tests the [google test framework](https://github.com/google/googletest)) 

## Building
```
git clone http://github.com/RossMeikleham/Fuse-VMU
cd Fuse-VMU
mkdir build
cd build
cmake ..
make
```

The "fuse-vmu" binary should be in the `Fuse-VMU/build/bin` folder


# Running
`./fuse-vmu <vmu_file_path> [fuse_args] <mount_path>`

# Building + Mounting the example VMU filesystem
```
git clone http://github.com/RossMeikleham/Fuse-VMU
cd Fuse-VMU
mkdir build
cd build
cmake ..
make
mkdir MOUNT_POINT
./bin/fuse_vmu ../example/example_vmu.bin -f MOUNT_POINT
```


# Unmounting
`umount <mount_path>`

# Building + Running Unit Tests
```
git clone http://github.com/RossMeikleham/Fuse-VMU
cd Fuse-VMU/test
mkdir build
cd build
cmake ..
make
bin/fuse_vmu_tests
```
