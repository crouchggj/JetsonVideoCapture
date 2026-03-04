# Full-Stack Integrated Excellence: Jetson Video Capture

RTSP video stream capture and decoding solution for NVIDIA Jetson platforms.
A self-contained, high-cohesion repository that unifies demuxing(ffmpeg demuxer), buffering, and hardware acceleration into a single, optimized C++ pipeline.

## Features

- RTSP stream and local video file support
- Hardware decoding: H264/H265/VP8/VP9/MPEG2/MPEG4
- Save decoded frames as NV12 format
- TCP/UDP transport protocol switching
- Auto-reconnect mechanism
- Custom post-processing support

## Verified Environment

- Jetson Orin Nano, JetPack 6.2.1

## Build

- [Cross-compile toolchain](https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-linux-gnu.tar.xz): Tool Version: 12.3.rel1
- [Jetson SYS ROOTFS Download](https://pan.baidu.com/s/1a-SVHKsAJyYZNphMMpXGLw): code: h44m

```bash
# Debug build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake -DCMAKE_BUILD_TYPE=Debug -DJETSON_SYSROOT=/home/L4T_Jetpack6.2.1_ROOTFS
make -j$(nproc)

# Release build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake -DCMAKE_BUILD_TYPE=Release -DJETSON_SYSROOT=/home/L4T_Jetpack6.2.1_ROOTFS
make -j$(nproc)
```

## Usage

```bash
# RTSP stream
./JetsonVideoCapture -u rtsp://admin:password@192.168.1.65:554/stream1

# Local file
./JetsonVideoCapture -u file:///path/to/video.mp4

# Show help
./JetsonVideoCapture -h
```

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              main.cpp                                     │
│                         (Entry Point & CLI Parsing)                      │
│  1. Parse -u argument for RTSP URL                                      │
│  2. Create PacketQueue                                                   │
│  3. Create VideoDemuxer & VideoDecoder                                   │
│  4. Wait for stream ready                                                │
│  5. Start decoder                                                        │
│  6. Wait for exit signal                                                 │
└────────────────────────────────┬────────────────────────────────────────┘
                                 │
                ┌────────────────┴────────────────┐
                ▼                                 ▼
┌───────────────────────────────┐   ┌───────────────────────────────┐
│      VideoDemuxer              │   │      VideoDecoder              │
│    (Demultiplexer)             │   │    (V4L2 Hardware Decoder)    │
│                               │   │                                │
│  DemuxThread                  │   │  DecodeThread                  │
│  - Connect to RTSP/open file   │   │  - Pop AVPacket from queue    │
│  - Parse video stream          │   │  - Feed to V4L2 output plane   │
│  - Extract video packet        │   │  - Wait for decode completion │
│  - Push to PacketQueue         │   │                                │
│                               │   │  CaptureThread                  │
│                               │   │  - Dequeue from capture plane   │
│                               │   │  - Call IFrameProcessor        │
│                               │   │  - Increment frame count       │
└───────────────┬───────────────┘   └────────────────┬───────────────┘
                │                                   │
                │       PacketQueue                 │
                │    (Thread-safe Queue)            │
                │                                   │
                └───────────────┬───────────────────┘
                                ▼
                    ┌───────────────────────┐
                    │   IFrameProcessor      │
                    │   (Frame Processor)    │
                    │                       │
                    │  ┌─────────────────┐ │
                    │  │ Nv12FrameSaver  │ │
                    │  │ - Block→Pitch   │ │
                    │  │ - Save NV12    │ │
                    │  └─────────────────┘ │
                    └───────────────────────┘
```

## Component Overview

### main.cpp
Program entry point:
- Command-line argument parsing
- Component creation and initialization order control
- Signal handling and graceful shutdown

### PacketQueue
Thread-safe AVPacket queue:
- Demuxer pushes packets into queue
- Decoder pops packets from queue
- Supports blocking/non-blocking mode
- Supports abort() to cancel operations

### VideoDemuxer
FFmpeg RTSP/file demultiplexer:
- Connect to RTSP stream or open local file
- Parse video stream info (resolution, codec)
- Use BSF filter for H264/H265 streams
- Send stream-ready callback

### VideoDecoder
V4L2 hardware decoder:
- Create NvVideoDecoder instance
- Manage decode and capture threads
- Handle resolution change events
- Support multiple codec formats

### IFrameProcessor / Nv12FrameSaver
Frame processor interface and implementation:
- Receive decoded NV12 frames
- Convert Block Linear to Pitch Linear
- Save as NV12 files

## Directory Structure

```
.
├── core/                      # Core components
│   ├── VideoDemuxer.cpp/hpp  # FFmpeg demux
│   ├── VideoDecoder.cpp/hpp   # V4L2 hardware decode
│   ├── Nv12FrameSaver.cpp/hpp # NV12 file save
│   ├── PacketQueue.h          # Thread-safe queue
│   ├── IFrameProcessor.hpp     # Frame processor interface
│   └── Logger.h                # Logging macros
├── main.cpp                   # Entry point
└── CMakeLists.txt             # Build configuration
```

## Acknowledgments
This project stands on the shoulders of giants. We would like to express our gratitude to the following projects for their inspiration and excellent codebases:
* [jetson-ffmpeg](https://github.com/Keylost/jetson-ffmpeg)
* [Jetson Linux API](https://docs.nvidia.com/jetson/archives/r36.4.3/ApiReference/index.html)

A special thanks to the AI assistants that made programming more convenient:
* [MiniMax-M2.5](https://platform.minimax.com) - Powerful AI coding assistant
* [GLM-5](https://www.zhipuai.cn/) - Excellent Chinese AI model
* [Claude Code](https://claude.com/claude-code) - CLI assistant for software development