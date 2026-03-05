/**
 * @file VideoDemuxer.cpp
 * @brief Implementation of VideoDemuxer
 */
#include "VideoDemuxer.hpp"
#include "Logger.h"
#include "PacketQueue.h"
#include "NvV4l2Element.h"

#include <cstring>
#include <utility>

extern "C" {
#include <libavcodec/bsf.h>
}

namespace jetson {

class PacketQueueWrapper {
  public:
    explicit PacketQueueWrapper(void *queue) : queue_(static_cast<PacketQueue *>(queue)) {}

    bool Push(AVPacket *pkt) {
      if (queue_) {
        return queue_->push(pkt);
      }
      return false;
    }

  private:
    PacketQueue *queue_;
};

class BSFWrapper {
  public:
    explicit BSFWrapper() = default;
    ~BSFWrapper() {
      if (av_bsf_context_) {
        av_bsf_free(&av_bsf_context_);
        av_bsf_context_ = nullptr;
      }
      init_ = false;
    }
    [[nodiscard]] bool IsInitialized() const { return init_; }
    bool Init(const AVCodecID codec_id, const AVStream *av_stream) {
      if (init_) {
        LOG_WARN("BSF context is already initialized");
        return true;
      }

      if (codec_id == AV_CODEC_ID_H264) {
        av_bsf_ = av_bsf_get_by_name("h264_mp4toannexb");
      } else if (codec_id == AV_CODEC_ID_HEVC) {
        av_bsf_ = av_bsf_get_by_name("hevc_mp4toannexb");
      } else {
        LOG_WARN("codec id: {} don't need bsf filter.", (int) codec_id);
        return true;
      }

      if (av_bsf_alloc(av_bsf_, &av_bsf_context_) != 0) {
        LOG_ERROR("av_bsf_alloc() failed");
        return false;
      }
      if (avcodec_parameters_copy(av_bsf_context_->par_in, av_stream->codecpar) != 0) {
        LOG_ERROR("avcodec_parameters_copy() failed !");
        av_bsf_free(&av_bsf_context_);
        return false;
      }
      av_bsf_context_->time_base_in = av_stream->time_base;

      if (av_bsf_init(av_bsf_context_) < 0) {
        LOG_ERROR("av_bsf_init() failed");
        av_bsf_free(&av_bsf_context_);
        return false;
      }
      init_ = true;
      LOG_INFO("BSF Filter init success!");
      return true;
    }

    bool Process(AVPacket *packet, AVPacket *filtered_packet, PacketQueueWrapper *queue) const {
      if (!IsInitialized()) {
        LOG_ERROR("BSF is not initialized!");
        return false;
      }

      if (!packet && !filtered_packet && !queue) {
        LOG_ERROR("Invalid input parameters!");
        return false;
      }

      if (const int ret = av_bsf_send_packet(av_bsf_context_, packet); ret < 0) {
        char err_str[AV_ERROR_MAX_STRING_SIZE] = {};
        av_make_error_string(err_str, sizeof(err_str), ret);
        LOG_ERROR("av_bsf_send_packet() failed, {}!", err_str);
        return false;
      }

      while (av_bsf_receive_packet(av_bsf_context_, filtered_packet) == 0) {
        if (!queue->Push(filtered_packet)) {
          av_packet_unref(filtered_packet);
          LOG_ERROR("BSF push filtered packet failure!");
          return false;
        }
      }
      return true;
    }

  private:
    const AVBitStreamFilter *av_bsf_{nullptr};
    AVBSFContext *av_bsf_context_{nullptr};
    bool init_{false};
};

VideoDemuxer::VideoDemuxer(VideoDemuxerConfig config) : config_(std::move(config)) {}

VideoDemuxer::~VideoDemuxer() { Stop(); }

void VideoDemuxer::SetOutputQueue(void *queue) { packet_queue_ = queue; }

void VideoDemuxer::SetStreamInfoCallback(StreamInfoCallback callback) {
  stream_callback_ = std::move(callback);
}

bool VideoDemuxer::WaitForStreamReady(int timeout_ms) const {
  int elapsed = 0;
  while (elapsed < timeout_ms && !stream_ready_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    elapsed += 50;
  }
  return stream_ready_.load();
}

void VideoDemuxer::NotifyStreamInfo() {
  if (!stream_ready_.load()) {
    stream_ready_.store(true);
    if (stream_callback_) {
      stream_callback_(width_, height_, codec_id_);
    }
  }
}

Status VideoDemuxer::Start() {
  if (running_.load()) {
    LOG_WARN("Demuxer is already running");
    return Status::kOk;
  }

  running_ = true;
  demux_thread_ = std::thread(&VideoDemuxer::DemuxLoop, this);

  LOG_INFO("Demuxer started");
  return Status::kOk;
}

void VideoDemuxer::Stop() {
  if (!running_.load()) {
    return;
  }

  running_ = false;
  if (demux_thread_.joinable()) {
    demux_thread_.join();
  }

  auto *queue = static_cast<PacketQueue *>(packet_queue_);
  if (queue) {
    queue->abort();
  }
  LOG_INFO("Demuxer stopped, total frames: {}", frame_count_.load());
}

uint32_t VideoDemuxer::GetPixFormat() const {
  if (codec_id_ == AV_CODEC_ID_H264) {
    return V4L2_PIX_FMT_H264;
  }
  if (codec_id_ == AV_CODEC_ID_HEVC) {
    return V4L2_PIX_FMT_H265;
  }
  if (codec_id_ == AV_CODEC_ID_VP8) {
    return V4L2_PIX_FMT_VP8;
  }
  if (codec_id_ == AV_CODEC_ID_VP9) {
    return V4L2_PIX_FMT_VP9;
  }
  if (codec_id_ == AV_CODEC_ID_MPEG2VIDEO) {
    return V4L2_PIX_FMT_MPEG2;
  }
  if (codec_id_ == AV_CODEC_ID_MPEG4) {
    return V4L2_PIX_FMT_MPEG4;
  }
  return V4L2_PIX_FMT_H264;
}

void VideoDemuxer::DemuxLoop() {
  AVFormatContext *fmt_ctx = nullptr;
  auto bsf_wrapper = std::make_unique<BSFWrapper>();
  PacketQueueWrapper queue(packet_queue_);

  while (running_.load()) {
    // Open RTSP stream
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", config_.enable_tcp ? "tcp" : "udp", 0);
    av_dict_set(&opts, "timeout", "5000000", 0); // 5 second timeout in microseconds
    av_dict_set(&opts, "max_delay", "5000000", 0);
    av_dict_set(&opts, "reconnect", "1", 0);
    av_dict_set(&opts, "reconnect_streamed", "1", 0);
    av_dict_set(&opts, "reconnect_delay_max", "5", 0);

    if (avformat_open_input(&fmt_ctx, config_.rtsp_url.c_str(), nullptr, &opts) < 0) {
      LOG_ERROR("Failed to open RTSP stream: {}", config_.rtsp_url);
      if (config_.auto_reconnect) {
        LOG_INFO("Reconnecting in {}ms...", config_.reconnect_delay_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_delay_ms));
        continue;
      }
      break;
    }

    avformat_find_stream_info(fmt_ctx, nullptr);
    av_dict_free(&opts);

    // Find video stream
    int video_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
      if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_idx = i;
        width_ = fmt_ctx->streams[i]->codecpar->width;
        height_ = fmt_ctx->streams[i]->codecpar->height;
        codec_id_ = fmt_ctx->streams[i]->codecpar->codec_id;
        NotifyStreamInfo();  // Notify that stream info is ready
        break;
      }
    }

    if (video_idx < 0) {
      LOG_ERROR("No video stream found");
      avformat_close_input(&fmt_ctx);
      break;
    }

    if (!bsf_wrapper->Init(codec_id_, fmt_ctx->streams[video_idx])) {
      LOG_ERROR("bsf filter init failure!");
      avformat_close_input(&fmt_ctx);
      break;
    }

    frame_fps_ = int(av_q2d(fmt_ctx->streams[video_idx]->avg_frame_rate));
    if (frame_fps_ <= 0) frame_fps_ = 25;
    is_video_file_ = config_.rtsp_url.find("file:") == 0;
    LOG_INFO("Video stream url: {} found: {}x{}, codec id: {}, fps: {}",
             config_.rtsp_url,
             width_,
             height_,
             (int) codec_id_,
             frame_fps_);

    if (!bsf_wrapper->IsInitialized()) {
      // Push SPS/PPS as first packet
      uint8_t *extradata = fmt_ctx->streams[video_idx]->codecpar->extradata;
      int extradata_size = fmt_ctx->streams[video_idx]->codecpar->extradata_size;
      if (extradata_size > 0) {
        AVPacket *extra_pkt = av_packet_alloc();
        av_new_packet(extra_pkt, extradata_size);
        memcpy(extra_pkt->data, extradata, extradata_size);
        queue.Push(extra_pkt);
        av_packet_free(&extra_pkt);
        LOG_INFO("Pushed SPS/PPS to queue");
      }
    }

    // Main demux loop
    AVPacket *packet = av_packet_alloc();
    AVPacket *filtered_pkt = av_packet_alloc();
    while (running_.load()) {
      if (int ret = av_read_frame(fmt_ctx, packet); ret < 0) {
        if (ret == AVERROR_EOF || avio_feof(fmt_ctx->pb)) {
          LOG_WARN("End of stream, reopening...");
          break;
        }
        if (ret == AVERROR(EAGAIN)) {
          // Non-blocking mode, no data available yet
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }
        LOG_WARN("Failed to read frame: {}, retrying...", ret);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      if (packet->stream_index == video_idx) {
        LOG_DEBUG("Pushed packet to queue, size={}", packet->size);

        if (bsf_wrapper->IsInitialized()) {
          if (!bsf_wrapper->Process(packet, filtered_pkt, &queue)) {
            LOG_ERROR("BSF filter process failed!");
            break;
          }
        } else {
          if (!queue.Push(packet)) {
            LOG_WARN("Queue push failed (aborted), aborting...");
            break;
          }
        }
        frame_count_++;
      }
      av_packet_unref(packet);
      av_packet_unref(filtered_pkt);

      if (is_video_file_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(int(1000 / frame_fps_)));
      }
    }
    av_packet_free(&packet);
    av_packet_free(&filtered_pkt);
    avformat_close_input(&fmt_ctx);

    if (!config_.auto_reconnect) {
      break;
    }

    LOG_INFO("Reconnecting to stream...");
    std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_delay_ms));
  }

  running_ = false;
  LOG_INFO("Demuxer loop exited");
}

} // namespace jetson
