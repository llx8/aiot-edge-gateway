#ifndef __LOGGER_H__
#define __LOGGER_H__ 

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

std::shared_ptr<spdlog::logger> GetLogger(const std::string& name);

#endif