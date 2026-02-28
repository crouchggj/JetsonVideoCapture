#pragma once
#include <condition_variable>
#include <mutex>
#include <queue>
#include "Logger.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueue {
  public:
    explicit PacketQueue(const size_t max_size = 60) : max_size_(max_size), abort_request_(false) {}

    PacketQueue(const PacketQueue &) = delete;
    PacketQueue &operator=(const PacketQueue &) = delete;

    ~PacketQueue() {
      abort();
      flush();
    }

    bool push(AVPacket *pkt) {
      std::unique_lock lock(mutex_);
      cond_push_.wait(lock, [this]() { return queue_.size() < max_size_ || abort_request_; });
      if (abort_request_)
        return false;

      AVPacket *new_pkt = av_packet_alloc();
      if (!new_pkt) {
        LOG_ERROR("Failed to allocate AVPacket");
        return false;
      }
      av_packet_move_ref(new_pkt, pkt);
      try {
        queue_.push(new_pkt);
      } catch (const std::exception &e) {
        av_packet_free(&new_pkt); // 异常时防止泄漏
        LOG_ERROR("Exception in pushing packet to queue: {}", e.what());
        return false;
      }
      LOG_DEBUG("Pushed packet. Queue length: {}/{}", queue_.size(), max_size_);
      cond_pop_.notify_one();
      return true;
    }

    bool pop(AVPacket *pkt, bool block = true) {
      std::unique_lock lock(mutex_);
      if (block) {
        cond_pop_.wait(lock, [this]() { return !queue_.empty() || abort_request_; });
      }
      if (abort_request_ || queue_.empty())
        return false;

      AVPacket *tmp_pkt = queue_.front();
      queue_.pop();
      av_packet_unref(pkt);
      av_packet_move_ref(pkt, tmp_pkt);
      av_packet_free(&tmp_pkt);

      cond_push_.notify_one();
      return true;
    }

    void flush() {
      std::unique_lock lock(mutex_);
      while (!queue_.empty()) {
        AVPacket *pkt = queue_.front();
        queue_.pop();
        av_packet_free(&pkt);
      }
      cond_push_.notify_all();
    }

    void abort() {
      std::unique_lock lock(mutex_);
      abort_request_ = true;
      cond_pop_.notify_all();
      cond_push_.notify_all();
    }

  private:
    std::queue<AVPacket *> queue_;
    size_t max_size_;
    bool abort_request_;
    std::mutex mutex_;
    std::condition_variable cond_pop_;
    std::condition_variable cond_push_;
};
