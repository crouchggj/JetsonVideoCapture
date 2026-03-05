/**
 * @file IFrameProcessor.hpp
 * @brief Frame processor interface for VideoCaptureCore
 */
#pragma once

namespace jetson {

/**
 * @brief Error status codes
 */
enum class Status {
    kOk = 0,
    kInvalidParam,
    kAllocationFailed,
    kTransformFailed,
    kMapFailed,
    kIoError,
    kNotRunning,
    kUnhandled
};

/**
 * @brief Frame metadata
 */
struct FrameInfo {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frame_idx = 0;
    int dmabuf_fd = -1;
};

/**
 * @brief IFrameProcessor: Interface for frame processing
 *
 * Implement this interface to process decoded frames from VideoCaptureCore.
 * Examples: save to file, TensorRT inference, display, etc.
 */
class IFrameProcessor {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~IFrameProcessor() = default;

    /**
     * @brief Process a decoded frame
     * @param frame_info Frame metadata including FD, width, height, index
     * @return Status processing result
     */
    virtual Status Process(const FrameInfo& frame_info) = 0;

    /**
     * @brief Called when resolution changes
     * @param width New width
     * @param height New height
     */
    virtual void OnResolutionChange(uint32_t width, uint32_t height) {}

    /**
     * @brief Initialize the processor
     * @return Status initialization result
     */
    virtual Status Init() { return Status::kOk; }

    /**
     * @brief Release resources
     */
    virtual void Deinit() {}
};

}  // namespace jetson
