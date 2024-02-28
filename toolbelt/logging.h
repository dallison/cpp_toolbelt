// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_LOGGING_H
#define __TOOLBELT_LOGGING_H

#include "toolbelt/color.h"
#include <stdarg.h>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <cstdint>

namespace toolbelt {

enum class LogLevel {
  kVerboseDebug,
  kDebug,
  kInfo,
  kWarning,
  kError,
  kFatal,
};

enum class LogDisplayMode {
  kPlain,
  kColor,
  kColumnar,
};

enum class LogTheme {
  kDefault,
  kLight,
  kDark,
};

// A logger logs timestamped messages to a FILE pointer, possibly in color.
// Only messages that are are level above the current log level are
// logged.
class Logger {
public:
  Logger() {
    SetTheme(LogTheme::kDefault);
    SetDisplayMode(STDIN_FILENO);
  }
  Logger(const std::string &subsystem, bool enabled = true,
         LogTheme theme = LogTheme::kDefault,
         LogDisplayMode mode = LogDisplayMode::kPlain)
      : subsystem_(subsystem), enabled_(enabled), display_mode_(mode),
        theme_(theme) {
    SetTheme(theme_);
    SetDisplayMode(STDIN_FILENO);
  }
  Logger(LogLevel min) : min_level_(min) {}
  virtual ~Logger() = default;

  void Enable() { enabled_ = true; }
  void Disable() { enabled_ = false; }

  // Log a message at the given log level.  If standard error is a TTY
  // it will be in color.
  virtual void Log(LogLevel level, const char *fmt, ...);
  virtual void VLog(LogLevel level, const char *fmt, va_list ap);

  virtual void Log(LogLevel level, uint64_t timestamp,
                   const std::string &source, std::string text);

  void SetTheme(LogTheme theme);

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
  void SetOutputStream(FILE *stream) {
    output_stream_ = stream;
    SetDisplayMode(fileno(stream));
  }

private:
  static constexpr size_t kBufferSize = 4096;

  // Choose a good display mode.  If we can determine the width of the
  // window, use a columnar output mode, otherwise use plain or color
  // depending on whether the output is a tty or not.
  void SetDisplayMode(int fd);

  void LogColumnar(const char *timebuf, LogLevel level,
                   const std::string &source, const std::string &text);

  color::Color ColorForLogLevel(LogLevel level);
  color::Color BackgroundColorForLogLevel(LogLevel level);
  std::string ColorString(color::Color color);
  std::string NormalString();

  std::string subsystem_;
  bool enabled_ = true;
  char buffer_[kBufferSize];
  LogLevel min_level_ = LogLevel::kInfo;
  FILE *output_stream_ = stderr;
  bool in_color_ = isatty(STDERR_FILENO);
  LogDisplayMode display_mode_ = LogDisplayMode::kPlain;
  int screen_width_;
  LogTheme theme_ = LogTheme::kDefault;

  static constexpr int kNumColumns = 5;
  size_t column_widths_[kNumColumns];
  color::Color colors_[kNumColumns];
};

} // namespace toolbelt

#endif //  __TOOLBELT_LOGGING_H
