// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/color.h"
#include "absl/strings/str_format.h"

namespace toolbelt::color {

std::string SetColor(const Color &c) {
  if (c.fixed != FixedColor::kNotSet) {
    const char *mod = "";
    if ((c.mod & kBold) != 0) {
      mod = ";1";
    }
    int fg_base = 30;
    int bg_base = 40;
    if ((c.mod & kBright) != 0) {
      fg_base = 90;
      bg_base = 100;
    }

    int color_code =
        (((c.mod & kBackground) != 0) ? bg_base : fg_base) + int(c.fixed);
    return absl::StrFormat("\033[%d%sm", color_code, mod);
  }

  if ((c.mod & k8bit) != 0) {
    int color_code = (((c.mod & kBackground) != 0) ? 48 : 38);
    return absl::StrFormat("\033[%d;5;%dm", color_code, c.eight);
  }

  // RGB color.
  if ((c.mod & kRGB) != 0) {
    int color_code = (((c.mod & kBackground) != 0) ? 48 : 38);
    return absl::StrFormat("\033[%d;2;%d;%d;%dm", color_code, c.r, c.g, c.b);
  }

  return "";
}

std::string ResetColor() {
  static std::string reset = "\033[0m";
  return reset;
}
} // namespace toolbelt::color