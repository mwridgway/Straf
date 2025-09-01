// src/logging.cpp
#include "Straf/logging.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>                 // async logger
#include <spdlog/sinks/ostream_sink.h>

namespace logsys {

void init(bool verbose) {
  // Pattern. Time, level, thread, source file and line
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [tid %t] [%s:%#] %v");

  // Sinks
  auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  auto rotating = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      "logs/straflogs.log", 5 * 1024 * 1024, 3);

  std::vector<spdlog::sink_ptr> sinks {console, rotating};

  // Async logger for throughput
  spdlog::init_thread_pool(8192, 1); // q size, threads
  auto logger = std::make_shared<spdlog::async_logger>(
      "app", sinks.begin(), sinks.end(),
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::block);

  spdlog::set_default_logger(logger);
  // spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
  spdlog::set_level(spdlog::level::trace);
  spdlog::flush_every(std::chrono::seconds(1));
  spdlog::enable_backtrace(32);               // record last 32 messages
}

std::shared_ptr<spdlog::logger> get() { return spdlog::default_logger(); }

} // namespace logsys
