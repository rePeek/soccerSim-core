#pragma once

#include <source_location>
#include <string_view>

namespace football::log {

enum class Channel { capi, env, pitch, ball };

enum class Level { debug, info, warning, error };

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

void Flush();

}
