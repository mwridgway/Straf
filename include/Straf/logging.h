// src/logging.hpp
#pragma once
#include <memory>
#include <spdlog/spdlog.h>

namespace logsys {
void init(bool verbose = false);
std::shared_ptr<spdlog::logger> get(); // optional helper
}
