#include "football/log/log.hpp"

#include <bq_log/bq_log.h>

using BqLog = bq::log;

namespace football::log {

namespace {

constexpr const char* kLogName = "football";

constexpr const char* kLogConfig = R"(
  appenders_config.console.type=console
  appenders_config.console.levels=[debug,info,warning,error,fatal]

  appenders_config.file.type=text_file
  appenders_config.file.levels=[debug,info,warning,error,fatal]
  appenders_config.file.file_name=logs/football
  appenders_config.file.base_dir_type=0
  appenders_config.file.max_file_size=10000000
  appenders_config.file.expire_time_days=7
  appenders_config.file.capacity_limit=200000000

  log.thread_mode=async
  log.buffer_size=65536
)";

// A function-local trivial static: created once, no destructor to run at exit.
void EnsureCreated() {
  static const bool kCreated = [] {
    BqLog::create_log(kLogName, kLogConfig);
    return true;
  }();
  (void)kCreated;
}

BqLog Logger() {
  EnsureCreated();
  return BqLog::get_log_by_name(kLogName);
}

constexpr const char* ChannelName(Channel channel) {
  switch (channel) {
    case Channel::capi: return "capi";
    case Channel::env: return "env";
    case Channel::pitch: return "pitch";
    case Channel::ball: return "ball";
  }
  return "unknown";
}

// std::source_location yields a full signature such as
// "void football::ball::Ball::Step(const Pitch&)". Reduce it to a short
// "Ball::Step" (or "Env" for a constructor) so the prefix stays readable.
std::string_view FunctionName(const char* signature) {
  const std::string_view text = signature != nullptr ? signature : "";
  const std::size_t arguments = text.find('(');
  std::string_view name = arguments == std::string_view::npos
                              ? text
                              : text.substr(0, arguments);

  const std::size_t return_type = name.rfind(' ');
  if (return_type != std::string_view::npos) name = name.substr(return_type + 1);

  // Keep only the last two scope components: Class::Function.
  const std::size_t scope = name.rfind("::");
  if (scope == std::string_view::npos) return name;
  const std::size_t outer = scope >= 2 ? name.rfind("::", scope - 2)
                                       : std::string_view::npos;
  const std::string_view tail =
      outer == std::string_view::npos ? name : name.substr(outer + 2);

  // A constructor reads "Pitch::Pitch"; collapse it to "Pitch".
  const std::size_t split = tail.rfind("::");
  if (split != std::string_view::npos &&
      tail.substr(0, split) == tail.substr(split + 2)) {
    return tail.substr(split + 2);
  }
  return tail;
}

}  // namespace

void Write(Channel channel, Level level, std::string_view message,
           const std::source_location& location) {
  const BqLog logger = Logger();
  if (!logger.is_valid()) return;

  const char* name = ChannelName(channel);
  const std::string_view function = FunctionName(location.function_name());
  switch (level) {
    case Level::debug:
      logger.debug("[{}][{}] {}", name, function, message);
      break;
    case Level::info:
      logger.info("[{}][{}] {}", name, function, message);
      break;
    case Level::warning:
      logger.warning("[{}][{}] {}", name, function, message);
      break;
    case Level::error:
      logger.error("[{}][{}] {}", name, function, message);
      break;
  }
}

void Flush() {
  BqLog logger = BqLog::get_log_by_name(kLogName);
  if (logger.is_valid()) logger.force_flush();
}

}  // namespace football::log
