/**
 * @file Nv12FrameSaver.hpp
 * @brief Nv12FrameSaver: High-performance NV12 frame saver with block linear to pitch linear conversion
 */
#pragma once

#include <memory>
#include <string>

#include "IFrameProcessor.hpp"

namespace jetson {

/**
 * @brief Configuration for Nv12FrameSaver
 */
struct Nv12FrameSaverConfig {
    std::string output_dir = "nv12";    ///< Output directory for NV12 files
    uint32_t max_width = 3840;          ///< Maximum supported width
    uint32_t max_height = 2160;         ///< Maximum supported height
    bool enable_debug_log = false;      ///< Enable debug logging
};

/**
 * @brief Nv12FrameSaver: Saves decoded frames as NV12 files
 *
 * Implements IFrameProcessor interface.
 * Features:
 * - Block linear to pitch linear conversion
 * - RAII resource management
 * - PIMPL pattern for ABI stability
 */
class Nv12FrameSaver : public IFrameProcessor {
public:
    /**
     * @brief Construct with configuration
     * @param config Configuration parameters
     */
    explicit Nv12FrameSaver(const Nv12FrameSaverConfig& config);

    /**
     * @brief Destructor - releases all resources
     */
    ~Nv12FrameSaver() override;

    // Disable copy operations
    Nv12FrameSaver(const Nv12FrameSaver&) = delete;
    Nv12FrameSaver& operator=(const Nv12FrameSaver&) = delete;

    // Enable move operations
    Nv12FrameSaver(Nv12FrameSaver&& other) noexcept;
    Nv12FrameSaver& operator=(Nv12FrameSaver&& other) noexcept;

    // IFrameProcessor interface
    /**
     * @brief Process a frame from DMA-BUF FD
     * @param frame_info Frame metadata including FD, width, height, index
     * @return Status operation result
     */
    Status Process(const FrameInfo& frame_info) override;

    /**
     * @brief Process a frame (legacy overload for backward compatibility)
     * @param dmabuf_fd DMA-BUF file descriptor
     * @param frame_idx Frame index for naming
     * @return Status operation result
     */
    Status Process(int dmabuf_fd, uint32_t frame_idx);

    /**
     * @brief Reset on resolution change
     * @param width New width
     * @param height New height
     */
    void OnResolutionChange(uint32_t width, uint32_t height) override;

    /**
     * @brief Get last frame info
     * @return FrameInfo reference
     */
    const FrameInfo& GetFrameInfo() const { return frame_info_; }

    /**
     * @brief Check if initialized
     * @return true if ready to process frames
     */
    bool IsInitialized() const { return initialized_; }

private:
    class Impl;                              ///< PIMPL implementation
    std::unique_ptr<Impl> pImpl_;            ///< Pointer to implementation

    Nv12FrameSaverConfig config_;             ///< Configuration
    FrameInfo frame_info_;                    ///< Frame metadata
    bool initialized_ = false;                ///< Initialization flag
};

}  // namespace jetson
