// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_LOGGING_H
#define __TOOLBELT_LOGGING_H

#include <stdarg.h>
#include <string>
#include <unistd.h>
#include "toolbelt/color.h"

namespace toolbelt {

enum class LogLevel {
  kVerboseDebug,
  kDebug,
  kInfo,
  kWarning,
  kError,
  kFatal,
};

// A logger logs timestamped messages to a FILE pointer, possibly in color.
// Only messages that are are level above the current log level are
// logged.
class Logger {
public:
  Logger() = default;
  Logger(const std::string& subsystem, bool enabled = true) : subsystem_(subsystem), enabled_(enabled) {}
  Logger(LogLevel min) : min_level_(min) {}
  virtual ~Logger() = default;

  void Enable() { enabled_ = true; }
  void Disable() { enabled_ = false; }

  // Log a message at the given log level.  If standard error is a TTY
  // it will be in color.
  virtual void Log(LogLevel level, const char *fmt, ...);
  virtual void VLog(LogLevel level, const char *fmt, va_list ap);

  virtual void Log(LogLevel level, uint64_t timestamp, const std::string& source, std::string text);

  // All logged messages with a level below the min level will be
  // ignored.
  void SetLogLevel(LogLevel l) { min_level_ = l; }
  void SetLogLevel(const std::string &s) {
    LogLevel level;
    if (s == "verbose") {
      level = LogLevel::kVerboseDebug;
    } else if (s == "debug") {
      level = LogLevel::kDebug;
    } else if (s == "info") {
      level = LogLevel::kInfo;
    } else if (s == "error") {
      level = LogLevel::kError;
    } else if (s == "warning") {
      level = LogLevel::kWarning;
    } else if (s == "fatal") {
      level = LogLevel::kFatal;
    } else {
      fprintf(stderr, "Unknown log level %s\n", s.c_str());
      exit(1);
    }
    min_level_ = level;
  }

  LogLevel GetLogLevel() const { return min_level_; }
  void SetOutputStream(FILE* stream) {
    output_stream_ = stream;
    in_color_ = isatty(fileno(stream));
  }

private:
  static constexpr size_t kBufferSize = 256;

  static color::Color ColorForLogLevel(LogLevel level);
  std::string ColorString(color::Color color);
  std::string NormalString();

  std::string subsystem_;
  bool enabled_ = true;
  char buffer_[kBufferSize];
  LogLevel min_level_ = LogLevel::kInfo;
  FILE* output_stream_ = stderr;
  bool in_color_ = isatty(STDERR_FILENO);
};

} // namespace toolbelt

#endif //  __TOOLBELT_LOGGING_H
