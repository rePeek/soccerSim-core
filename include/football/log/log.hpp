#pragma once

#include <source_location>
#include <string_view>

// Component channels: one log line carries the component that emitted it.
namespace football::log {

enum class Channel { capi, env, pitch, ball };

enum class Level { debug, info, warning, error };

// Writes "[channel][function] message" through the shared BqLog instance.
// The location defaults to the caller, so callers never pass it explicitly.
void Write(Channel channel, Level level, std::string_view message,
           const std::source_location& location = std::source_location::current());

inline void Debug(Channel channel, std::string_view message,
                  const std::source_location& location = std::source_location::current()) {
  Write(channel, Level::debug, message, location);
}

inline void Info(Channel channel, std::string_view message,
                 const std::source_location& location = std::source_location::current()) {
  Write(channel, Level::info, message, location);
}

inline void Warning(Channel channel, std::string_view message,
                    const std::source_location& location = std::source_location::current()) {
  Write(channel, Level::warning, message, location);
}

inline void Error(Channel channel, std::string_view message,
                  const std::source_location& location = std::source_location::current()) {
  Write(channel, Level::error, message, location);
}

// Blocks until every buffered entry is written. Call before the process exits.
void Flush();

}  // namespace football::log
