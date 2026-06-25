#include "Logger.h"

std::shared_ptr<spdlog::logger> GetLogger(const std::string& name)
{
    // 已有同名 logger，直接返回
    auto existing = spdlog::get(name);
    if (existing) return existing;

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/" + name + ".log", true);

    auto logger = std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{console_sink, file_sink});
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger->set_level(spdlog::level::debug);
    spdlog::register_logger(logger);

    return logger;
}