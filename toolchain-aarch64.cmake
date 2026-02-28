# Cross-compilation toolchain for Jetson devices (ARM64)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-aarch64.cmake ..

set(TOOLCHAIN_ROOT "/home/corey/Downloads/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-linux-gnu")

# Target system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler binaries
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-none-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-none-linux-gnu-g++")
set(CMAKE_ASM_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-none-linux-gnu-gcc")

# Cross-compilation settings
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)


# RPATH settings for proper library loading
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
