/**
 * @file VideoDecoder.cpp
 * @brief Implementation of VideoDecoder
 */
#include "VideoDecoder.hpp"

#include <utility>
#include "Logger.h"
#include "NvBufSurface.h"
#include "PacketQueue.h"
#include "nv_bufsurface.h"

namespace jetson {

// ============================================================================
// VideoDecoder Implementation
// ============================================================================

VideoDecoder::VideoDecoder(VideoDecoderConfig config) : config_(std::move(config)) {
  memset(dma_buffer_fd_, -1, sizeof(dma_buffer_fd_));
  memset(dma_buffer_surface_, 0, sizeof(dma_buffer_surface_));
}

VideoDecoder::~VideoDecoder() {
  Stop();
  if (decoder_) {
    delete decoder_;
    decoder_ = nullptr;
  }
}

void VideoDecoder::SetFrameProcessor(std::unique_ptr<IFrameProcessor> processor) {
  processor_ = std::move(processor);
  if (processor_) {
    processor_->Init();
  }
}

void VideoDecoder::SetInputQueue(void *queue) { packet_queue_ = queue; }

Status VideoDecoder::Init() {
  if (!decoder_) {
    decoder_ = NvVideoDecoder::createVideoDecoder(config_.decoder_name.c_str());
    if (!decoder_) {
      LOG_ERROR("Failed to create decoder");
      return Status::kAllocationFailed;
    }
  }

  if (decoder_->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE, 0, 0) < 0) {
    LOG_ERROR("Failed to subscribe resolution change event");
    return Status::kInvalidParam;
  }

  // Set output plane format
  decoder_->setOutputPlaneFormat(config_.pixel_format, config_.image_size);
  decoder_->setFrameInputMode(0);

  // Setup output plane
  decoder_->output_plane.setupPlane(V4L2_MEMORY_USERPTR, config_.num_buffers, false, true);
  decoder_->output_plane.setStreamStatus(true);

  LOG_INFO("Decoder initialized");
  return Status::kOk;
}

Status VideoDecoder::AllocateCaptureBuffers() {
  v4l2_format format{};
  int32_t min_capture_buffers = 0;

  decoder_->capture_plane.getFormat(format);
  width_ = format.fmt.pix_mp.width;
  height_ = format.fmt.pix_mp.height;

  if (width_ == 0 || height_ == 0) {
    LOG_WARN("Invalid capture format ({}x{})", width_, height_);
    return Status::kInvalidParam;
  }

  LOG_INFO("Capture plane: {}x{}", width_, height_);

  // Set capture plane format
  if (decoder_->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat, width_, height_) < 0) {
    LOG_ERROR("Failed to set capture plane format");
    return Status::kInvalidParam;
  }

  if (decoder_->getMinimumCapturePlaneBuffers(min_capture_buffers) < 0) {
    LOG_ERROR("Failed to get minimum capture buffers");
    return Status::kAllocationFailed;
  }

  /*
   * refer to: https://github.com/Keylost/jetson-ffmpeg/blob/master/src/nvmpi_dec.cpp
   * Request (min + extra) buffers, export and map buffers.
   */
  num_buffers_ = min_capture_buffers + 5;

  decoder_->capture_plane.reqbufs(V4L2_MEMORY_DMABUF, num_buffers_);

  // Allocate DMA buffers
  NvBufSurf::NvCommonAllocateParams allocate_params{};
  allocate_params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
  allocate_params.width = width_;
  allocate_params.height = height_;
  allocate_params.layout = NVBUF_LAYOUT_BLOCK_LINEAR; // here must be block linear, we need convert later!
  allocate_params.memType = NVBUF_MEM_SURFACE_ARRAY;
  allocate_params.memtag = NvBufSurfaceTag_VIDEO_DEC;

  if (NvBufSurf::NvAllocate(&allocate_params, num_buffers_, dma_buffer_fd_) < 0) {
    LOG_ERROR("Failed to allocate capture buffers");
    return Status::kAllocationFailed;
  }

  for (int i = 0; i < num_buffers_; ++i) {
    if (NvBufSurfaceFromFd(dma_buffer_fd_[i], reinterpret_cast<void **>(&dma_buffer_surface_[i])) < 0) {
      LOG_ERROR("Failed to get surface from fd");
      return Status::kAllocationFailed;
    }
  }

  // Queue buffers
  for (uint32_t i = 0; i < decoder_->capture_plane.getNumBuffers(); ++i) {
    v4l2_buffer v4l2_buf{};
    v4l2_plane planes[MAX_PLANES]{};
    v4l2_buf.index = i;
    v4l2_buf.m.planes = planes;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_DMABUF;
    v4l2_buf.m.planes[0].m.fd = dma_buffer_fd_[i];
    decoder_->capture_plane.qBuffer(v4l2_buf, nullptr);
  }

  if (decoder_->capture_plane.setStreamStatus(true) < 0) {
    LOG_ERROR("Failed to start capture plane streaming");
    return Status::kIoError;
  }
  LOG_INFO("Allocated {} capture buffers", num_buffers_);
  return Status::kOk;
}

void VideoDecoder::DeallocateCaptureBuffers() {
  decoder_->capture_plane.setStreamStatus(false);
  decoder_->capture_plane.deinitPlane();
  for (uint32_t index = 0; index < num_buffers_; index++) {
    if (dma_buffer_fd_[index] != -1) {
      if (NvBufSurf::NvDestroy(dma_buffer_fd_[index]) < 0) {
        LOG_ERROR("Failed to destroy dma buffer");
      }
      dma_buffer_fd_[index] = -1;
    }
  }
  num_buffers_ = 0;
}

void VideoDecoder::HandleResolutionChange() {
  if (decoder_) {
    decoder_->capture_plane.setStreamStatus(false);
  }
  DeallocateCaptureBuffers();
  AllocateCaptureBuffers();
  // Notify processor
  if (processor_) {
    processor_->OnResolutionChange(width_, height_);
  }
}

Status VideoDecoder::Start() {
  if (running_.load()) {
    LOG_WARN("Decoder is already running");
    return Status::kOk;
  }

  // Create decoder if not exists
  if (!decoder_) {
    decoder_ = NvVideoDecoder::createVideoDecoder(config_.decoder_name.c_str());
    if (!decoder_) {
      LOG_ERROR("Failed to create decoder");
      return Status::kAllocationFailed;
    }
  }

  running_ = true;
  decode_thread_ = std::thread(&VideoDecoder::DecodeThreadFunc, this);
  capture_thread_ = std::thread(&VideoDecoder::CaptureThreadFunc, this);

  LOG_INFO("Decoder started");
  return Status::kOk;
}

void VideoDecoder::Stop() {
  if (!running_.load()) {
    return;
  }

  running_ = false;

  // Abort packet queue to unblock decode thread
  auto *queue = static_cast<PacketQueue *>(packet_queue_);
  if (queue) {
    queue->abort();
  }

  if (decoder_) {
    decoder_->capture_plane.setStreamStatus(false);
    decoder_->output_plane.setStreamStatus(false);
  }

  if (decode_thread_.joinable()) {
    decode_thread_.join();
  }

  if (capture_thread_.joinable()) {
    capture_thread_.join();
  }

  DeallocateCaptureBuffers();

  if (processor_) {
    processor_->Deinit();
  }

  LOG_INFO("Decoder stopped, total frames: {}", frame_count_.load(std::memory_order_relaxed));
}

void VideoDecoder::DecodeThreadFunc() const {
  LOG_INFO("Decode thread starting...");

  auto *queue = static_cast<PacketQueue *>(packet_queue_);
  if (!queue) {
    LOG_ERROR("Packet queue is null in decode thread");
    return;
  }

  uint32_t index = 0;
  const uint32_t num_buffers = decoder_->output_plane.getNumBuffers();
  AVPacket *packet = av_packet_alloc();
  if (!packet) {
    LOG_ERROR("Failed to allocate packet in decode thread");
    return;
  }
  while (running_.load()) {
    if (!queue->pop(packet, true)) {
      if (running_.load()) {
        continue;
      }
      break;
    }

    v4l2_buffer v4l2_buf{};
    v4l2_plane planes[MAX_PLANES]{};
    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    memset(planes, 0, sizeof(planes));
    v4l2_buf.m.planes = planes;

    NvBuffer *buffer_proxy = nullptr;

    if (index < num_buffers) {
      // Initial phase: use pre-allocated buffer
      buffer_proxy = decoder_->output_plane.getNthBuffer(index);
      v4l2_buf.index = index;
      index++;
    } else {
      // Circular phase: get processed buffer from driver
      if (decoder_->output_plane.dqBuffer(v4l2_buf, &buffer_proxy, nullptr, -1) != 0) {
        LOG_ERROR("Failed to dequeue output buffer");
        av_packet_unref(packet);
        continue;
      }
    }

    if (!buffer_proxy) {
      LOG_ERROR("Failed to get buffer proxy");
      av_packet_unref(packet);
      continue;
    }
    if (packet->size <= 0) {
      av_packet_unref(packet);
      continue;
    }
    if (static_cast<uint32_t>(packet->size) > buffer_proxy->planes[0].length) {
      LOG_ERROR("Packet size ({}) exceeds buffer capacity ({})", packet->size, buffer_proxy->planes[0].length);
      av_packet_unref(packet);
      continue;
    }

    memcpy(buffer_proxy->planes[0].data, packet->data, packet->size);
    buffer_proxy->planes[0].bytesused = packet->size;

    // Queue buffer
    if (decoder_->output_plane.qBuffer(v4l2_buf, nullptr) != 0) {
      LOG_ERROR("Failed to queue output buffer");
    }
    av_packet_unref(packet);
  }
  av_packet_free(&packet);

  LOG_INFO("Decode thread exited");
}

void VideoDecoder::CaptureThreadFunc() {
  LOG_INFO("Capture thread starting...");

  while (running_.load()) {
    v4l2_event ev{};
    memset(&ev, 0, sizeof(ev));
    int ret = decoder_->dqEvent(ev, 100);
    if (ret == 0 && ev.type == V4L2_EVENT_RESOLUTION_CHANGE) {
      LOG_INFO("Initial resolution change detected");

      auto status = AllocateCaptureBuffers();
      if (status != Status::kOk) {
        LOG_ERROR("Failed to allocate capture buffers: {}", static_cast<int>(status));
        DeallocateCaptureBuffers();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }
      break;
    }
    if (ret < 0 && errno != EAGAIN) {
      LOG_ERROR("Error polling for event: {}", ret);
      break;
    }
  }

  while (running_.load()) {
    // Check for events (resolution change or EOS)
    v4l2_event ev{};
    memset(&ev, 0, sizeof(ev));
    if (decoder_->dqEvent(ev, 0) == 0) {
      if (ev.type == V4L2_EVENT_RESOLUTION_CHANGE) {
        LOG_INFO("Dynamic resolution change detected in capture thread");
        HandleResolutionChange();
        continue;
      }
      if (ev.type == V4L2_EVENT_EOS) {
        LOG_INFO("EOS received in capture thread");
        running_.store(false);
        break;
      }
    }

    if (!running_.load())
      break;

    v4l2_buffer v4l2_buf{};
    v4l2_plane planes[MAX_PLANES]{};
    memset(&v4l2_buf, 0, sizeof(v4l2_buf));
    memset(planes, 0, sizeof(planes));
    v4l2_buf.m.planes = planes;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_DMABUF;

    if (!decoder_->capture_plane.dqBuffer(v4l2_buf, nullptr, nullptr, 100)) {
      int dma_buffer_fd = v4l2_buf.m.planes[0].m.fd;

      // Process frame with processor
      if (processor_) {
        FrameInfo frame_info;
        frame_info.dmabuf_fd = dma_buffer_fd;
        frame_info.width = width_;
        frame_info.height = height_;
        frame_info.frame_idx = frame_count_.load(std::memory_order_relaxed);
        if (auto status = processor_->Process(frame_info); status != Status::kOk) {
          LOG_ERROR("Processor failed: {}", static_cast<int>(status));
        }
        frame_count_.fetch_add(1, std::memory_order_relaxed);
      }

      // Re-queue buffer
      v4l2_buf.m.planes[0].m.fd = dma_buffer_fd;
      if (decoder_->capture_plane.qBuffer(v4l2_buf, nullptr) < 0) {
        LOG_ERROR("Failed to re-queue capture buffer!");
      }
    } else if (errno != EAGAIN) {
      LOG_ERROR("dqBuffer failed with fatal error!");
    }
  }
  LOG_INFO("Capture thread exited");
}

} // namespace jetson
