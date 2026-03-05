#include "Logger.h"
#include "Nv12FrameSaver.hpp"
#include "PacketQueue.h"
#include "VideoDecoder.hpp"
#include "VideoDemuxer.hpp"

#include <atomic>
#include <csignal>
#include <getopt.h>

std::atomic<bool> g_running{true};

void signal_handler(int) { g_running = false; }

void env_init() {
  logger::init();
  // Setup signal handler for graceful shutdown
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
}

void print_usage(const char *prog_name) {
  printf("Usage: %s [options]\n", prog_name);
  printf("Options:\n");
  printf("  -u, --url <url>    RTSP stream URL or file path (required)\n");
  printf("  -h, --help         Show this help message\n");
  printf("\nExamples:\n");
  printf("  %s -u rtsp://admin:password@192.168.1.65\n", prog_name);
  printf("  %s -u file:///path/to/video.mp4\n", prog_name);
}

int main(int argc, char *argv[]) {
  std::string rtsp_url;

  // Parse command line options
  static struct option long_options[] = {
      {"url", required_argument, 0, 'u'}, {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "u:h", long_options, nullptr)) != -1) {
    switch (opt) {
      case 'u':
        rtsp_url = optarg;
        break;
      case 'h':
        print_usage(argv[0]);
        return 0;
      default:
        print_usage(argv[0]);
        return 1;
    }
  }

  env_init();

  if (rtsp_url.empty()) {
    LOG_ERROR("RTSP URL is required. Use -u <url> or --url <url>");
    print_usage(argv[0]);
    return 1;
  }

  LOG_INFO("Starting Jetson Decoupled Pipeline...");
  LOG_INFO("Video source: {}", rtsp_url);

  // 创建 PacketQueue 用于解复用器和解码器之间的通信
  PacketQueue pkt_queue(60);

  // 创建 VideoDemuxer (解复用)
  jetson::VideoDemuxerConfig demuxer_config;
  demuxer_config.rtsp_url = rtsp_url;
  jetson::VideoDemuxer demuxer(demuxer_config);

  // 设置输出队列 (连接到解码器)
  demuxer.SetOutputQueue(&pkt_queue);
  demuxer.Start();

  if (!demuxer.WaitForStreamReady(5000)) {
    LOG_ERROR("Failed to get video stream");
    return -1;
  }

  // 创建 VideoDecoder (管理 decoder + 捕获)
  jetson::VideoDecoderConfig decoder_config;
  decoder_config.decoder_name = "dec0";
  decoder_config.num_buffers = 10;
  decoder_config.image_size = demuxer.GetWidth() * demuxer.GetHeight() * 3 / 2; // nv12 format
  decoder_config.pixel_format = demuxer.GetPixFormat();

  jetson::VideoDecoder decoder(decoder_config);
  // 设置输入队列 (连接解复用器)
  decoder.SetInputQueue(&pkt_queue);
  // 设置帧处理器 (保存 NV12 文件)
  jetson::Nv12FrameSaverConfig saver_config;
  saver_config.output_dir = "nv12";
  saver_config.enable_debug_log = true;
  decoder.SetFrameProcessor(std::make_unique<jetson::Nv12FrameSaver>(saver_config));

  // 初始化 decoder
  if (decoder.Init() != jetson::Status::kOk) {
    LOG_ERROR("Failed to initialize decoder");
    return -1;
  }

  // 启动组件
  decoder.Start();

  LOG_INFO("Pipeline running. Press Ctrl+C to stop...");

  // Keep running until signal received
  while (g_running.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  LOG_INFO("Shutting down...");
  decoder.Stop();
  demuxer.Stop();

  LOG_INFO("Total frames - Demuxer: {}, Decoder: {}", demuxer.GetFrameCount(), decoder.GetFrameCount());
  return 0;
}
