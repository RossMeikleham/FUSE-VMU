FROM m1kev4ndyke/docker-gtest

RUN apt-get update && apt-get install -y libfuse-dev kmod

RUN mkdir /fuse-vmu
WORKDIR /fuse-vmu

COPY src ./src
COPY test ./test
COPY CMake ./CMake
COPY CMakeLists.txt ./CMakeLists.txt

# Build and Run Unit Tests
RUN cd test \
	&& mkdir -p build \
	&& cd build \
	&& cmake .. \
	&& make \
	&& bin/fuse_vmu_tests

WORKDIR /fuse-vmu

# Build and Install Application
RUN mkdir -p build \
	&& cd build \
	&& cmake .. \
	&& make \
	&& cp bin/fuse_vmu /usr/bin/fuse_vmu

# Cleanup
WORKDIR /
RUN rm -r /fuse-vmu
