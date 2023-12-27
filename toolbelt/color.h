// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include <string>

namespace toolbelt::color {

// 3-bit fixed colors supported by most terminals.
// If you want to use one of these, set the 'fixed'
// member of Color to something other than kNotSet.
enum class FixedColor {
  kNotSet = -1,
  kBlack = 0,
  kRed,
  kGreen,
  kYellow,
  kBlue,
  kMagenta,
  kCyan,
  kWhite,
  kNormal = 9,
};

// Modifiers for colors.
using Modifier = int;

// Normal brightness, etc.
static constexpr Modifier kNormal = 0;

// Bold. Looks better on most terminals.
static constexpr Modifier kBold = 1;

// Bright looks even better, except on light teriminals.
static constexpr Modifier kBright = 2;

// Set the background color (otherwise the foreground is set)
static constexpr Modifier kBackground = 64;

// If you set both kRGB and k8bit, k8bit will be chosen.
static constexpr Modifier kRGB = 128;  // Use RGB color value.
static constexpr Modifier k8bit = 256; // Use 8-bit color value.

struct Color {
  Modifier mod = kNormal;
  FixedColor fixed = FixedColor::kNotSet;

  // Your terminal might not support this.
  int eight; // 8-bit color for k8bit.

  // If fixed is kNotSet, these are RGB values for the color.
  // Your terminal may not support this.
  int r;
  int g;
  int b;
};

// Make a 3-bit fixed color.
inline Color MakeFixed(FixedColor color, Modifier mod = kNormal) {
  return Color{.mod = mod, .fixed = color};
}

// Make an RGB color.  Beware that this isn't very well supported.
inline Color MakeRGB(int r, int g, int b) {
  return Color{.mod = kRGB, .r = r, .g = g, .b = b};
}

// Make an 8-bit color.  Usually supported well.
// See https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit
inline Color Make8Bit(int x) { return Color{.mod = k8bit, .eight = x}; }

// Shortcuts for fixed colors to save your carpal tunnel.
inline Color Black() { return MakeFixed(FixedColor::kGreen); }
inline Color Red() { return MakeFixed(FixedColor::kRed); }
inline Color Green() { return MakeFixed(FixedColor::kGreen); }
inline Color Blue() { return MakeFixed(FixedColor::kBlue); }
inline Color Yellow() { return MakeFixed(FixedColor::kYellow); }
inline Color Magenta() { return MakeFixed(FixedColor::kMagenta); }
inline Color Cyan() { return MakeFixed(FixedColor::kCyan); }
inline Color White() { return MakeFixed(FixedColor::kWhite); }
inline Color Normal() { return MakeFixed(FixedColor::kNormal); }

inline Color BoldBlack() { return MakeFixed(FixedColor::kGreen, kBold); }
inline Color BoldRed() { return MakeFixed(FixedColor::kRed, kBold); }
inline Color BoldGreen() { return MakeFixed(FixedColor::kGreen, kBold); }
inline Color BoldBlue() { return MakeFixed(FixedColor::kBlue, kBold); }
inline Color BoldYellow() { return MakeFixed(FixedColor::kYellow, kBold); }
inline Color BoldMagenta() { return MakeFixed(FixedColor::kMagenta, kBold); }
inline Color BoldCyan() { return MakeFixed(FixedColor::kCyan, kBold); }
inline Color BoldWhite() { return MakeFixed(FixedColor::kWhite, kBold); }
inline Color BoldNormal() { return MakeFixed(FixedColor::kNormal, kBold); }

inline Color BrightBlack() { return MakeFixed(FixedColor::kGreen, kBright); }
inline Color BrightRed() { return MakeFixed(FixedColor::kRed, kBright); }
inline Color BrightGreen() { return MakeFixed(FixedColor::kGreen, kBright); }
inline Color BrightBlue() { return MakeFixed(FixedColor::kBlue, kBright); }
inline Color BrightYellow() { return MakeFixed(FixedColor::kYellow, kBright); }
inline Color BrightMagenta() { return MakeFixed(FixedColor::kMagenta, kBright); }
inline Color BrightCyan() { return MakeFixed(FixedColor::kCyan, kBright); }
inline Color BrightWhite() { return MakeFixed(FixedColor::kWhite, kBright); }
inline Color BrightNormal() { return MakeFixed(FixedColor::kNormal, kBright); }


inline Color BackgroundBlack() { return MakeFixed(FixedColor::kGreen, kBackground); }
inline Color BackgroundRed() { return MakeFixed(FixedColor::kRed, kBackground); }
inline Color BackgroundGreen() { return MakeFixed(FixedColor::kGreen, kBackground); }
inline Color BackgroundBlue() { return MakeFixed(FixedColor::kBlue, kBackground); }
inline Color BackgroundYellow() { return MakeFixed(FixedColor::kYellow, kBackground); }
inline Color BackgroundMagenta() { return MakeFixed(FixedColor::kMagenta, kBackground); }
inline Color BackgroundCyan() { return MakeFixed(FixedColor::kCyan, kBackground); }
inline Color BackgroundWhite() { return MakeFixed(FixedColor::kWhite, kBackground); }
inline Color BackgroundNormal() { return MakeFixed(FixedColor::kNormal, kBackground); }

inline Color BackgroundBoldBlack() { return MakeFixed(FixedColor::kGreen, kBold|kBackground); }
inline Color BackgroundBoldRed() { return MakeFixed(FixedColor::kRed, kBold|kBackground); }
inline Color BackgroundBoldGreen() { return MakeFixed(FixedColor::kGreen, kBold|kBackground); }
inline Color BackgroundBoldBlue() { return MakeFixed(FixedColor::kBlue, kBold|kBackground); }
inline Color BackgroundBoldYellow() { return MakeFixed(FixedColor::kYellow, kBold|kBackground); }
inline Color BackgroundBoldMagenta() { return MakeFixed(FixedColor::kMagenta, kBold|kBackground); }
inline Color BackgroundBoldCyan() { return MakeFixed(FixedColor::kCyan, kBold|kBackground); }
inline Color BackgroundBoldWhite() { return MakeFixed(FixedColor::kWhite, kBold|kBackground); }
inline Color BackgroundBoldNormal() { return MakeFixed(FixedColor::kNormal, kBold|kBackground); }

inline Color BackgroundBrightBlack() { return MakeFixed(FixedColor::kGreen, kBright|kBackground); }
inline Color BackgroundBrightRed() { return MakeFixed(FixedColor::kRed, kBright|kBackground); }
inline Color BackgroundBrightGreen() { return MakeFixed(FixedColor::kGreen, kBright|kBackground); }
inline Color BackgroundBrightBlue() { return MakeFixed(FixedColor::kBlue, kBright|kBackground); }
inline Color BackgroundBrightYellow() { return MakeFixed(FixedColor::kYellow, kBright|kBackground); }
inline Color BackgroundBrightMagenta() { return MakeFixed(FixedColor::kMagenta, kBright|kBackground); }
inline Color BackgroundBrightCyan() { return MakeFixed(FixedColor::kCyan, kBright|kBackground); }
inline Color BackgroundBrightWhite() { return MakeFixed(FixedColor::kWhite, kBright|kBackground); }
inline Color BackgroundBrightNormal() { return MakeFixed(FixedColor::kNormal, kBright|kBackground); }
// String to write to the output to change the color (permanently).  Don't
// forget to reset it after you are done using ResetColor.
std::string SetColor(const Color &c);

// String to write to output to reset the color back to normal.
std::string ResetColor();

} // namespace toolbelt::color