#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace logger {
    inline void init() {
        auto console = spdlog::stdout_color_mt("console");
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::debug);
    }

    inline std::shared_ptr<spdlog::logger> getLogger() {
        return spdlog::get("console");
    }
}

// Logging macros using logger singleton
#define LOG_DEBUG(msg, ...) \
    logger::getLogger()->debug(msg, ##__VA_ARGS__)
#define LOG_INFO(msg, ...) \
    logger::getLogger()->info(msg, ##__VA_ARGS__)
#define LOG_WARN(msg, ...) \
    logger::getLogger()->warn(msg, ##__VA_ARGS__)
#define LOG_ERROR(msg, ...) \
    logger::getLogger()->error(msg, ##__VA_ARGS__)
#define LOG_FATAL(msg, ...) \
    logger::getLogger()->critical(msg, ##__VA_ARGS__)
