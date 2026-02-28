/**
 * @file VideoDecoder.hpp
 * @brief VideoDecoder: V4L2 hardware decoder with integrated capture plane
 */
#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "IFrameProcessor.hpp"
#include "NvVideoDecoder.h"

struct NvBufSurface;

namespace jetson {

/**
 * @brief Configuration for VideoDecoder
 */
struct VideoDecoderConfig {
    std::string decoder_name = "dec0"; ///< Decoder instance name
    uint32_t num_buffers = 10; ///< Number of output buffers
    uint32_t image_size = 4 * 1024 * 1024; ///< Max H.264 bitstream size
    uint32_t pixel_format = V4L2_PIX_FMT_H264; ///< Codec format
};

/**
 * @brief VideoDecoder: V4L2 hardware decoder with capture plane
 *
 * Wraps V4L2 decoder, output plane, and capture plane management.
 * Provides both decoding and frame capture functionality.
 *
 * Usage:
 * @code
 * VideoDecoder decoder(config);
 * decoder.SetFrameProcessor(std::make_unique<MyProcessor>());
 * decoder.Start();
 * @endcode
 */
class VideoDecoder {
  public:
    /**
     * @brief Construct with configuration
     * @param config Configuration parameters
     */
    explicit VideoDecoder(VideoDecoderConfig config = {});

    /**
     * @brief Destructor - releases all resources
     */
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;
    VideoDecoder(VideoDecoder &&other) = delete;
    VideoDecoder &operator=(VideoDecoder &&other) = delete;

    /**
     * @brief Set frame processor
     * @param processor Unique pointer to frame processor
     */
    void SetFrameProcessor(std::unique_ptr<IFrameProcessor> processor);

    /**
     * @brief Set input packet queue (from demuxer)
     * @param queue Pointer to PacketQueue
     */
    void SetInputQueue(void *queue);

    /**
     * @brief Initialize decoder and capture plane
     * @return Status initialization result
     */
    Status Init();

    /**
     * @brief Start decoding and capture in background thread
     * @return Status operation result
     */
    Status Start();

    /**
     * @brief Stop decoding and capture
     */
    void Stop();

    /**
     * @brief Check if running
     * @return true if active
     */
    [[nodiscard]] bool IsRunning() const { return running_.load(); }

    /**
     * @brief Get decoded frame count
     * @return Number of frames decoded
     */
    [[nodiscard]] uint64_t GetFrameCount() const { return frame_count_.load(); }

    /**
     * @brief Get decoder instance
     * @return Pointer to NvVideoDecoder
     */
    [[nodiscard]] NvVideoDecoder *GetDecoder() const { return decoder_; }

    /**
     * @brief Get video width
     * @return Video width
     */
    [[nodiscard]] int GetWidth() const { return width_; }

    /**
     * @brief Get video height
     * @return Video height
     */
    [[nodiscard]] int GetHeight() const { return height_; }

  private:
    void DecodeThreadFunc() const;
    void CaptureThreadFunc();
    Status AllocateCaptureBuffers();
    void DeallocateCaptureBuffers();
    void HandleResolutionChange();

    VideoDecoderConfig config_;
    NvVideoDecoder *decoder_ = nullptr;
    std::unique_ptr<IFrameProcessor> processor_;
    void *packet_queue_ = nullptr; ///< Input packet queue from demuxer
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frame_count_{0};
    std::thread decode_thread_;
    std::thread capture_thread_;

    // Capture plane resources
    static constexpr int MAX_BUFFERS = 32;
    int dma_buffer_fd_[MAX_BUFFERS]{};
    NvBufSurface *dma_buffer_surface_[MAX_BUFFERS]{};
    int num_buffers_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace jetson
