// Copyright 2023,2025 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "logging.h"
#include "absl/strings/str_format.h"
#include "clock.h"
#include <cstdio>
#include <inttypes.h>
#include <termios.h>

namespace toolbelt {

static const char *LogLevelAsString(LogLevel level) {
  switch (level) {
  case LogLevel::kVerboseDebug:
    return " V";
  case LogLevel::kDebug:
    return " D";
  case LogLevel::kInfo:
    return " I";
  case LogLevel::kWarning:
    return " W";
  case LogLevel::kError:
    return " E";
  case LogLevel::kFatal:
    return " F";
  }
  return " U";
}

color::Color Logger::ColorForLogLevel(LogLevel level) {
  switch (theme_) {
  case LogTheme::kLight:
    switch (level) {
    case LogLevel::kVerboseDebug:
      return color::BoldCyan();
    case LogLevel::kDebug:
      return color::BoldGreen();
    case LogLevel::kInfo:
      return color::BoldBlue();
    case LogLevel::kWarning:
      return color::BoldYellow();
    case LogLevel::kError:
      return color::BoldRed();
    case LogLevel::kFatal:
      return color::BoldRed();
    }
    break;
  case LogTheme::kDark:
    switch (level) {
    case LogLevel::kVerboseDebug:
      return color::BoldGreen();
    case LogLevel::kDebug:
      return color::BoldGreen();
    case LogLevel::kInfo:
      return color::BoldNormal();
    case LogLevel::kWarning:
      return color::BoldMagenta();
    case LogLevel::kError:
      return color::BoldRed();
    case LogLevel::kFatal:
      return color::BoldRed();
    }
    break;
  default:
    break;
  }
  return color::BoldCyan();
}

color::Color Logger::BackgroundColorForLogLevel(LogLevel level) {
  switch (theme_) {
  case LogTheme::kLight:
    switch (level) {
    case LogLevel::kVerboseDebug:
      return color::BackgroundBoldCyan();
    case LogLevel::kDebug:
      return color::BackgroundBoldGreen();
    case LogLevel::kInfo:
      return color::BackgroundBoldBlue();
    case LogLevel::kWarning:
      return color::BackgroundBoldYellow();
    case LogLevel::kError:
      return color::BackgroundBoldRed();
    case LogLevel::kFatal:
      return color::BackgroundBoldRed();
    }
    break;
  case LogTheme::kDark:
    switch (level) {
    case LogLevel::kVerboseDebug:
      return color::BackgroundBoldGreen();
    case LogLevel::kDebug:
      return color::BackgroundBoldGreen();
    case LogLevel::kInfo:
      return color::BackgroundBoldNormal();
    case LogLevel::kWarning:
      return color::BackgroundBoldMagenta();
    case LogLevel::kError:
      return color::BackgroundBoldRed();
    case LogLevel::kFatal:
      return color::BackgroundBoldRed();
    }
    break;
  default:
    break;
  }
  return color::BackgroundBoldCyan();
}

std::string Logger::ColorString(color::Color color) {
  if (display_mode_ == LogDisplayMode::kPlain) {
    return "";
  }
  return color::SetColor(color);
}

std::string Logger::NormalString() {
  if (display_mode_ == LogDisplayMode::kPlain) {
    return "";
  }
  return color::ResetColor();
}

void Logger::Log(LogLevel level, const char *fmt, ...) {
  if (!enabled_ || level < min_level_) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  VLog(level, fmt, ap);
  va_end(ap);
}

absl::Status Logger::SetTeeFile(const std::string &filename, bool truncate) {
  if (tee_stream_ != nullptr) {
    fclose(tee_stream_);
  }
  tee_stream_ = fopen(filename.c_str(), truncate ? "w" : "a");
  if (tee_stream_ == nullptr) {
    return absl::InternalError(absl::StrFormat("Failed to open tee file %s: %s",
                                               filename, strerror(errno)));
  }
  return absl::OkStatus();
}

void Logger::VLog(LogLevel level, const char *fmt, va_list ap) {
  if (level < min_level_) {
    return;
  }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
  size_t n = vsnprintf(buffer_, sizeof(buffer_), fmt, ap);
#pragma clang diagnostic pop

  // Strip final \n if present.  Refactoring from printf can leave
  // this in place.
  if (buffer_[n - 1] == '\n') {
    buffer_[n - 1] = '\0';
  }

  struct timespec now_ts;
  clock_gettime(CLOCK_REALTIME, &now_ts);
  uint64_t now_ns = now_ts.tv_sec * 1000000000LL + now_ts.tv_nsec;

  char timebuf[64];
  struct tm tm;
  n = strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
               localtime_r(&now_ts.tv_sec, &tm));
  snprintf(timebuf + n, sizeof(timebuf) - n, ".%09" PRIu64,
           now_ns % 1000000000);

  Log(level, now_ns, "", buffer_);
}

void Logger::Log(LogLevel level, uint64_t timestamp, const std::string &source,
                 std::string text) {
  if (!enabled_ || level < min_level_) {
    return;
  }

  // Strip final \n if present.  Refactoring from printf can leave
  // this in place.
  if (text[text.size() - 1] == '\n') {
    text = text.substr(0, text.size() - 1);
  }

  char timebuf[64];
  struct tm tm;
  time_t secs = timestamp / 1000000000LL;
  size_t n = strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
                      localtime_r(&secs, &tm));
  snprintf(timebuf + n, sizeof(timebuf) - n, ".%09" PRIu64,
           timestamp % 1000000000);

  switch (display_mode_) {
  case LogDisplayMode::kPlain:
    fprintf(output_stream_, "%s %s: %s: %s: %s\n", timebuf, subsystem_.c_str(),
            LogLevelAsString(level), source.c_str(), text.c_str());
    break;
  case LogDisplayMode::kColor: {
    color::Color color = ColorForLogLevel(level);
    fprintf(output_stream_, "%s%s %s: %s: %s: %s%s\n",
            ColorString(color).c_str(), timebuf, subsystem_.c_str(),
            LogLevelAsString(level), source.c_str(), text.c_str(),
            NormalString().c_str());
    break;
  }
  default:
    LogColumnar(timebuf, level, source, text);
    break;
  }

  if (tee_stream_ != nullptr) {
    fprintf(tee_stream_, "%s %s: %s: %s: %s\n", timebuf, subsystem_.c_str(),
            LogLevelAsString(level), source.c_str(), text.c_str());
    fflush(tee_stream_);
  }
  if (level == LogLevel::kFatal) {
    abort();
  }
}

void Logger::SetDisplayMode(int fd) {
  if (isatty(fd)) {
    struct winsize win;
    int e = ioctl(fd, TIOCGWINSZ, &win);
    if (e == 0) {
      screen_width_ = win.ws_col;
    }
    if (e != 0 || screen_width_ == 0) {
      display_mode_ = LogDisplayMode::kColor;
    } else {
      // Divide the screen into columns.
      column_widths_[0] = 30; // Timestamp.

      // Subsystem, with a max of 20.
      column_widths_[1] = int(subsystem_.size());
      if (column_widths_[1] > 20) {
        column_widths_[1] = 20;
      }
      column_widths_[2] = 3;  // Log level
      column_widths_[3] = 20; // Source
      ssize_t remaining = screen_width_;
      for (int i = 0; i < 4; i++) {
        remaining -= column_widths_[i] + 1;
      }
      if (remaining < 0) {
        remaining = 20;
      }
      column_widths_[4] = remaining - 1;
      display_mode_ = LogDisplayMode::kColumnar;
    }
  } else {
    display_mode_ = LogDisplayMode::kPlain;
  }
}

void Logger::SetTheme(LogTheme theme) {
  theme_ = theme;
  switch (theme) {
  case LogTheme::kLight:
    colors_[0] = MakeFixed(color::FixedColor::kCyan, color::kBold);
    colors_[1] = MakeFixed(color::FixedColor::kGreen, color::kBold);
    // Column 2 color depends on the log level.
    colors_[3] = MakeFixed(color::FixedColor::kMagenta, color::kBold);
    // Column 4 color depends on the log level.
    break;
  case LogTheme::kDark:
    colors_[0] = MakeFixed(color::FixedColor::kCyan, color::kBold);
    colors_[1] = MakeFixed(color::FixedColor::kGreen, color::kBold);
    // Column 2 color depends on the log level.
    colors_[3] = MakeFixed(color::FixedColor::kYellow, color::kBold);
    // Column 4 color depends on the log level.
    break;
  case LogTheme::kDefault:
#if defined(__APPLE__)
    SetTheme(LogTheme::kLight);
#else
    SetTheme(LogTheme::kDark);
#endif
    break;
  }
}

void Logger::LogColumnar(const char *timebuf, LogLevel level,
                         const std::string &source, const std::string &text) {
  std::string subsystem = subsystem_;
  if (subsystem_.size() > 20) {
    subsystem = subsystem.substr(0, 19);
  }
  std::string src = source;
  if (src.size() > 20) {
    src = src.substr(0, 19);
  }
  // clang-format off
  std::string prefix = absl::StrFormat("%s%-*s%s %s%-*s%s %s%s%-*s%s %s%-*s%s ",
            color::SetColor(colors_[0]), column_widths_[0], timebuf, color::ResetColor(),
            color::SetColor(colors_[1]), column_widths_[1], subsystem, color::ResetColor(),
            color::SetColor(BackgroundColorForLogLevel(level)), color::SetColor(color::BoldWhite()), column_widths_[2], LogLevelAsString(level), color::ResetColor(),
            color::SetColor(colors_[3]), column_widths_[3], src, color::ResetColor());
  // clang-format on
  bool first_line = true;
  size_t start = 0;
  int prefix_length = 0;
  for (int i = 0; i < 4; i++) {
    prefix_length += column_widths_[i] + 1;
  }
  for (;;) {
    std::string segment = text.substr(start);
    // Look for newlines in the segment and split there.
    size_t newline = segment.find('\n');
    if (newline != std::string::npos) {
      segment = segment.substr(0, newline);
    }
    if (segment.size() > column_widths_[4]) {
      segment = segment.substr(0, column_widths_[4]);
      // Move back to the first space to avoid splitting words.
      ssize_t i = segment.size() - 1;
      while (i > 0) {
        if (isspace(segment[i])) {
          break;
        }
        i--;
      }
      // If there is no space we just split the word.
      if (i != 0) {
        segment = segment.substr(0, i);
      }
    }
    // clang-format off.
    fprintf(output_stream_, "%-*s%s%-*s%s\n", prefix_length,
            first_line ? prefix.c_str() : "",
            color::SetColor(ColorForLogLevel(level)).c_str(),
            int(column_widths_[4]), segment.c_str(),
            color::ResetColor().c_str());
    // clang-format on
    start += segment.size();
    if (start >= text.size()) {
      break;
    }
    // Skip newlines at the end of the segment.
    while (start < text.size() && text[start] == '\n') {
      start++;
    }
    first_line = false;
    // Skip spaces for continuation line.
    while (start < text.size() && isspace(text[start])) {
      start++;
    }
  }
}

} // namespace toolbelt
