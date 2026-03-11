/**
 * @file Nv12FrameSaver.cpp
 * @brief Implementation of Nv12FrameSaver
 */
#include "Nv12FrameSaver.hpp"
#include "Logger.h"
#include "nv_bufsurface.h"
#include "NvBufSurface.h"
#include "nvbufsurftransform.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

namespace jetson {

// ============================================================================
// PIMPL Implementation
// ============================================================================

class Nv12FrameSaver::Impl {
public:
    explicit Impl(const Nv12FrameSaverConfig& config)
        : config_(config), dst_surf_(nullptr), dst_fd_(-1) {
        // Create output directory if not exists
        mkdir(config.output_dir.c_str(), 0755);
    }

    ~Impl() { ReleaseResources(); }

    Status AllocateDstBuffer(uint32_t width, uint32_t height) {
        ReleaseResources();

        NvBufSurf::NvCommonAllocateParams params = {};
        params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
        params.width = width;
        params.height = height;
        params.layout = NVBUF_LAYOUT_PITCH;
        params.memType = NVBUF_MEM_SURFACE_ARRAY;
        params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

        if (NvBufSurf::NvAllocate(&params, 1, &dst_fd_) < 0) {
            LOG_ERROR("Failed to allocate destination buffer");
            return Status::kAllocationFailed;
        }

        if (NvBufSurfaceFromFd(dst_fd_, reinterpret_cast<void**>(&dst_surf_)) < 0) {
            LOG_ERROR("Failed to get surface from fd");
            NvBufSurf::NvDestroy(dst_fd_);
            dst_fd_ = -1;
            return Status::kAllocationFailed;
        }

        LOG_INFO("Created pitch linear buffer: {}x{}", width, height);
        return Status::kOk;
    }

    Status ProcessFrame(int dmabuf_fd, uint32_t frame_idx,
                        uint32_t width, uint32_t height,
                        bool enable_debug) {
        // Get source surface
        NvBufSurface* src_surf = nullptr;
        if (NvBufSurfaceFromFd(dmabuf_fd, reinterpret_cast<void**>(&src_surf)) != 0) {
            LOG_ERROR("Failed to get NvBufSurface from fd {}", dmabuf_fd);
            return Status::kInvalidParam;
        }

        // Get actual dimensions from planeParams (may differ from codec dimensions)
        uint32_t src_width = src_surf->surfaceList[0].planeParams.width[0];
        uint32_t src_height = src_surf->surfaceList[0].planeParams.height[0];

        if (enable_debug) {
            LOG_DEBUG("Source surface: codec {}x{}, actual {}x{}",
                          width, height, src_width, src_height);
        }

        // Allocate destination buffer if needed
        if (!dst_surf_ || src_width != frame_info_.width || src_height != frame_info_.height) {
            auto status = AllocateDstBuffer(src_width, src_height);
            if (status != Status::kOk) {
                return status;
            }
            frame_info_.width = src_width;
            frame_info_.height = src_height;
        }

        // Transform: block linear -> pitch linear
        NvBufSurfTransformParams transform_params{};
        transform_params.transform_flag = NVBUFSURF_TRANSFORM_FILTER;
        transform_params.transform_filter = NvBufSurfTransformInter_Nearest;

        if (NvBufSurfTransform(src_surf, dst_surf_, &transform_params) !=
            NvBufSurfTransformError_Success) {
            LOG_ERROR("Failed to transform block linear to pitch linear");
            return Status::kTransformFailed;
        }

        // Map to CPU
        if (NvBufSurfaceMap(dst_surf_, 0, -1, NVBUF_MAP_READ) != 0) {
            LOG_ERROR("Failed to map destination buffer");
            return Status::kMapFailed;
        }
        NvBufSurfaceSyncForCpu(dst_surf_, 0, -1);

        // Get actual dimensions from destination surface
        const auto& dst_plane_params = dst_surf_->surfaceList[0].planeParams;
        uint32_t dst_width = dst_plane_params.width[0];
        uint32_t dst_height = dst_plane_params.height[0];
        uint32_t y_pitch = dst_plane_params.pitch[0];
        uint32_t uv_pitch = dst_plane_params.pitch[1];

        // For NV12 format: Y plane = width * height, UV plane = width * height / 2
        uint32_t y_plane_size = dst_width * dst_height;
        uint32_t uv_plane_size = dst_width * dst_height / 2;
        uint32_t total_size = y_plane_size + uv_plane_size;

        if (enable_debug) {
            LOG_DEBUG("NV12: {}x{}, Y pitch: {}, UV pitch: {}, Y size: {}, UV size: {}",
                          dst_width, dst_height, y_pitch, uv_pitch, y_plane_size, uv_plane_size);
        }

        // Copy data - output without padding (width x height format)
        std::vector<char> nv12_data(total_size);
        auto* y_addr = static_cast<char*>(dst_surf_->surfaceList[0].mappedAddr.addr[0]);
        auto* uv_addr = static_cast<char*>(dst_surf_->surfaceList[0].mappedAddr.addr[1]);

        // Copy Y plane: pitch may be larger than width
        for (uint32_t i = 0; i < dst_height; ++i) {
            std::memcpy(nv12_data.data() + i * dst_width,
                       y_addr + i * y_pitch, dst_width);
        }

        // Copy UV plane: interleaved (UVUV...), pitch may be larger than width
        for (uint32_t i = 0; i < dst_height / 2; ++i) {
            std::memcpy(nv12_data.data() + y_plane_size + i * dst_width,
                        uv_addr + i * uv_pitch, dst_width);
        }

        // Save to file
        char filename[256];
        std::snprintf(filename, sizeof(filename), "%s/frame_%04u_%ux%u.nv12",
                      config_.output_dir.c_str(), frame_idx, width, height);

        std::ofstream outfile(filename, std::ios::binary);
        if (!outfile.is_open()) {
            LOG_ERROR("Failed to open file {}", filename);
            NvBufSurfaceUnMap(dst_surf_, 0, -1);
            return Status::kIoError;
        }

        outfile.write(nv12_data.data(), total_size);
        outfile.close();
        NvBufSurfaceUnMap(dst_surf_, 0, -1);

        LOG_INFO("Saved frame {} to {} ({} bytes)", frame_idx, filename, total_size);
        frame_info_.frame_idx = frame_idx;

        return Status::kOk;
    }

    void ReleaseResources() {
        if (dst_surf_) {
            NvBufSurfaceDestroy(dst_surf_);
            dst_surf_ = nullptr;
            dst_fd_ = -1;
        }
    }

    const Nv12FrameSaverConfig& config_;
    NvBufSurface* dst_surf_;
    int dst_fd_;
    FrameInfo frame_info_;
};

// ============================================================================
// Public Interface
// ============================================================================

Nv12FrameSaver::Nv12FrameSaver(const Nv12FrameSaverConfig& config)
    : pImpl_(std::make_unique<Impl>(config)),
      config_(config),
      initialized_(true) {}

Nv12FrameSaver::~Nv12FrameSaver() = default;

Nv12FrameSaver::Nv12FrameSaver(Nv12FrameSaver&& other) noexcept
    : pImpl_(std::move(other.pImpl_)),
      config_(other.config_),
      frame_info_(other.frame_info_),
      initialized_(other.initialized_) {
    other.initialized_ = false;
}

Nv12FrameSaver& Nv12FrameSaver::operator=(Nv12FrameSaver&& other) noexcept {
    if (this != &other) {
        pImpl_ = std::move(other.pImpl_);
        config_ = other.config_;
        frame_info_ = other.frame_info_;
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}

Status Nv12FrameSaver::Process(const FrameInfo& frame_info) {
    if (!initialized_ || !pImpl_) {
        return Status::kUnhandled;
    }

    return pImpl_->ProcessFrame(frame_info.dmabuf_fd, frame_info.frame_idx,
                        frame_info.width, frame_info.height,
                        config_.enable_debug_log);
}

Status Nv12FrameSaver::Process(int dmabuf_fd, uint32_t frame_idx) {
    if (!initialized_ || !pImpl_) {
        return Status::kUnhandled;
    }

    // Get width/height from source surface
    NvBufSurface* src_surf = nullptr;
    if (NvBufSurfaceFromFd(dmabuf_fd, reinterpret_cast<void**>(&src_surf)) != 0) {
        return Status::kInvalidParam;
    }

    uint32_t width = src_surf->surfaceList[0].width;
    uint32_t height = src_surf->surfaceList[0].height;

    return pImpl_->ProcessFrame(dmabuf_fd, frame_idx, width, height,
                                config_.enable_debug_log);
}

void Nv12FrameSaver::OnResolutionChange(uint32_t width, uint32_t height) {
    if (pImpl_) {
        pImpl_->AllocateDstBuffer(width, height);
    }
}

}  // namespace jetson