/**
 * @file VideoDemuxer.hpp
 * @brief VideoDemuxer: FFmpeg-based RTSP demuxer for video streaming
 */
#ifndef VIDEO_DEMUXER_HPP_
#define VIDEO_DEMUXER_HPP_

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

#include "IFrameProcessor.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

namespace jetson {

/**
 * @brief Callback for video stream info ready
 */
using StreamInfoCallback = std::function<void(int width, int height, AVCodecID codec_id)>;

/**
 * @brief Configuration for VideoDemuxer
 */
struct VideoDemuxerConfig {
    std::string rtsp_url;                    ///< RTSP stream URL
    size_t queue_max_size = 60;              ///< Maximum packet queue size
    bool auto_reconnect = true;              ///< Auto reconnect on disconnect
    bool enable_tcp = true;                  ///< Enable TCP transport
    int reconnect_delay_ms = 1000;           ///< Reconnect delay in milliseconds
};

/**
 * @brief VideoDemuxer: FFmpeg-based RTSP demuxer
 *
 * Wraps FFmpeg demuxing logic in a reusable class.
 * Usage:
 * @code
 * VideoDemuxer demuxer(config);
 * demuxer.SetOutputQueue(packet_queue);
 * demuxer.Start();
 * @endcode
 */
class VideoDemuxer {
public:
    /**
     * @brief Construct with configuration
     * @param config Configuration parameters
     */
    explicit VideoDemuxer(VideoDemuxerConfig  config);

    /**
     * @brief Destructor - releases all resources
     */
    ~VideoDemuxer();

    VideoDemuxer(const VideoDemuxer&) = delete;
    VideoDemuxer& operator=(const VideoDemuxer&) = delete;
    VideoDemuxer(VideoDemuxer&& other) noexcept = delete;
    VideoDemuxer& operator=(VideoDemuxer&& other) noexcept = delete;

    /**
     * @brief Set output packet queue (external queue)
     * @param queue Pointer to PacketQueue
     */
    void SetOutputQueue(void* queue);

    /**
     * @brief Set callback for stream info ready
     * @param callback Function called when video stream info is available
     */
    void SetStreamInfoCallback(StreamInfoCallback callback);

    /**
     * @brief Wait for video stream to be ready
     * @param timeout_ms Timeout in milliseconds
     * @return true if stream is ready
     */
    bool WaitForStreamReady(int timeout_ms = 5000) const;

    /**
     * @brief Start demuxing in background thread
     * @return Status operation result
     */
    Status Start();

    /**
     * @brief Stop demuxing
     */
    void Stop();

    /**
     * @brief Check if running
     * @return true if demuxing is active
     */
    [[nodiscard]] bool IsRunning() const { return running_.load(); }

    /**
     * @brief Check if stream info is ready
     * @return true if video stream info is available
     */
    [[nodiscard]] bool IsStreamReady() const { return stream_ready_.load(); }

    /**
     * @brief Get frame count
     * @return Number of frames demuxed
     */
    [[nodiscard]] uint64_t GetFrameCount() const { return frame_count_.load(); }

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

    /**
     * @brief Get video pix format
     * @return Video pix format
     */
    [[nodiscard]] uint32_t GetPixFormat() const;
private:
    void DemuxLoop();
    void NotifyStreamInfo();

    VideoDemuxerConfig config_;
    void* packet_queue_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> stream_ready_{false};
    std::atomic<uint64_t> frame_count_{0};
    std::thread demux_thread_;
    int width_ = 0;
    int height_ = 0;
    int frame_fps_ = 0;
    AVCodecID codec_id_ = AV_CODEC_ID_H264;
    bool is_video_file_ = false;
    StreamInfoCallback stream_callback_;
};

}  // namespace jetson

#endif  // VIDEO_DEMUXER_HPP_