cmake_minimum_required(VERSION 3.18)
project(JetsonVideoCapture)

set(CMAKE_CXX_STANDARD 17)

# Set default build type to Release
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Set debug symbols and optimizations based on build type
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Cross-compile toolchain path
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/toolchain-aarch64.cmake")

# ============================================================
# Jetson Multimedia API paths (set your sysroot path here)
# ============================================================
if (NOT JETSON_SYSROOT OR NOT EXISTS "${JETSON_SYSROOT}")
    message(FATAL_ERROR "JETSON_SYSROOT not set or does not exist: ${JETSON_SYSROOT}")
endif ()


set(JETSON_LIBRARY_SEARCH_DIRS
        "${JETSON_SYSROOT}/lib"
        "${JETSON_SYSROOT}/lib/aarch64-linux-gnu"
        "${JETSON_SYSROOT}/lib/aarch64-linux-gnu/nvidia"
)

# ============================================================
# Build classes library (from jetson_multimedia_api/classes/*.cpp)
# ============================================================
# Add all classes source files
file(GLOB_RECURSE CLASSES_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvBuffer.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvBufSurface.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvVideoDecoder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvLogging.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvUtils.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvElement.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvElementProfiler.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvV4l2Element.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/classes/NvV4l2ElementPlane.cpp"
)

# ============================================================
# Build configuration
# ============================================================
include_directories(
        ${JETSON_SYSROOT}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/jetson_multimedia_api/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
)

link_directories(
        ${JETSON_LIBRARY_SEARCH_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg/lib
)

add_executable(JetsonVideoCapture main.cpp core/Nv12FrameSaver.cpp core/VideoDemuxer.cpp core/VideoDecoder.cpp ${CLASSES_SOURCES})
# Link Jetson libraries and classes library
target_link_libraries(JetsonVideoCapture PRIVATE
        avformat swresample avutil avcodec nvbufsurface nvbufsurftransform v4l2
        "-Wl,-rpath-link,${JETSON_SYSROOT}/lib"
        "-Wl,-rpath-link,${JETSON_SYSROOT}/lib/aarch64-linux-gnu"
        "-Wl,-rpath-link,${JETSON_SYSROOT}/lib/aarch64-linux-gnu/nvidia"
)
