#pragma once

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/RotatingFileSink.h"

#include <string>

namespace game::log {

inline quill::Logger* server_logger() {
    static quill::Logger* logger = nullptr;
    if (!logger) {
        logger = quill::Frontend::get_logger("game");
    }
    return logger;
}

inline void init(const std::string& log_file, const std::string& log_level) {
    quill::BackendOptions backend_opts;
    quill::Backend::start(backend_opts);

    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");

    quill::RotatingFileSinkConfig file_cfg;
    file_cfg.set_open_mode('a');
    file_cfg.set_filename_append_option(quill::FilenameAppendOption::None);
    file_cfg.set_rotation_max_file_size(50 * 1024 * 1024);
    file_cfg.set_max_backup_files(5);

    auto file_sink = quill::Frontend::create_or_get_sink<quill::RotatingFileSink>(
        log_file, file_cfg);

    quill::Logger* logger = quill::Frontend::create_or_get_logger(
        "game",
        {std::move(console_sink), std::move(file_sink)},
        quill::PatternFormatterOptions{
            "%(time) [%(log_level:<8)] [%(logger)] %(message)",
            "%Y-%m-%d %H:%M:%S.%Qms"
        }
    );

    if (log_level == "trace")
        logger->set_log_level(quill::LogLevel::TraceL1);
    else if (log_level == "debug")
        logger->set_log_level(quill::LogLevel::Debug);
    else if (log_level == "warning" || log_level == "warn")
        logger->set_log_level(quill::LogLevel::Warning);
    else if (log_level == "error")
        logger->set_log_level(quill::LogLevel::Error);
    else
        logger->set_log_level(quill::LogLevel::Info);
}

} // namespace game::log

#define LOG_TRACE(fmt, ...) LOG_TRACE_L1(game::log::server_logger(), fmt, ##__VA_ARGS__)
#define LOG_DBG(fmt, ...)   LOG_DEBUG(game::log::server_logger(), fmt, ##__VA_ARGS__)
#define LOG_INF(fmt, ...)   LOG_INFO(game::log::server_logger(), fmt, ##__VA_ARGS__)
#define LOG_WRN(fmt, ...)   LOG_WARNING(game::log::server_logger(), fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   LOG_ERROR(game::log::server_logger(), fmt, ##__VA_ARGS__)
