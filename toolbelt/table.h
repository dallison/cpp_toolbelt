#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace toolbelt {

class Table {
public:
  enum class FixedColor {
    kNotSet = -1,
    kBlack = 0,
    kRed,
    kGreen,
    kBlue,
    kYellow,
    kMagenta,
    kCyan,
    kWhite,
  };

  using Modifier = int;
  static constexpr Modifier kNormal = 0;
  static constexpr Modifier kBold = 1;
  static constexpr Modifier kBackground = 2;
  static constexpr Modifier kRGB = 8;
  static constexpr Modifier k8bit = 16;

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

  struct Cell {
    std::string data;
    Color color;
  };

  Table(const std::vector<std::string> titles, ssize_t sort_column = 0,
        std::function<bool(const std::string &, const std::string &)> comp =
            nullptr);
  ~Table();

  void AddRow(const std::vector<std::string> cells);
  void AddRow(const std::vector<std::string> cells, Color color);
  void AddRowWithColors(const std::vector<Cell> cells);
  void AddRow();
  void SetCell(size_t col, Cell &&cell);

  void Print(int width, std::ostream &os);
  void Clear();

  // Sort data using the comparison function, which must correspond to that
  // needed by std::sort.
  void
  SortBy(size_t column,
         std::function<bool(const std::string &, const std::string &)> comp) {
    sort_column_ = column;
    if (comp == nullptr) {
      // Sort by string.
      sorter_ = [](const std::string &a, const std::string &b) -> bool {
        return a < b;
      };
    } else {
      sorter_ = std::move(comp);
    }
  }

  // Sort data using the column.  Comparison is done by string comparison.
  void SortBy(size_t column) { SortBy(column, nullptr); }

  static Color MakeFixedColor(FixedColor color, Modifier mod = kNormal) {
    return Color{.mod = mod, .fixed = color};
  }

  static Color MakeRGB(int r, int g, int b) {
    return Color{.mod = kRGB, .r = r, .g = g, .b = b};
  }

  // See https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit
  static Color Make8Bit(int x) { return Color{.mod = k8bit, .eight = x}; }

  static Cell MakeCell(std::string data,
                       Color color = {.mod = kNormal,
                                      .fixed = FixedColor::kNotSet}) {
    return Cell({.data = std::move(data), .color = color});
  }

  static Color Black() { return MakeFixedColor(FixedColor::kGreen); }
  static Color Red() { return MakeFixedColor(FixedColor::kRed); }
  static Color Green() { return MakeFixedColor(FixedColor::kGreen); }
  static Color Blue() { return MakeFixedColor(FixedColor::kBlue); }
  static Color Yellow() { return MakeFixedColor(FixedColor::kYellow); }
  static Color Magenta() { return MakeFixedColor(FixedColor::kMagenta); }
  static Color Cyan() { return MakeFixedColor(FixedColor::kCyan); }
  static Color White() { return MakeFixedColor(FixedColor::kWhite); }

  static Color BoldBlack() { return MakeFixedColor(FixedColor::kGreen, kBold); }
  static Color BoldRed() { return MakeFixedColor(FixedColor::kRed, kBold); }
  static Color BoldGreen() { return MakeFixedColor(FixedColor::kGreen, kBold); }
  static Color BoldBlue() { return MakeFixedColor(FixedColor::kBlue, kBold); }
  static Color BoldYellow() {
    return MakeFixedColor(FixedColor::kYellow, kBold);
  }
  static Color BoldMagenta() {
    return MakeFixedColor(FixedColor::kMagenta, kBold);
  }
  static Color BoldCyan() { return MakeFixedColor(FixedColor::kCyan, kBold); }
  static Color BoldWhite() { return MakeFixedColor(FixedColor::kWhite, kBold); }

  static std::string SetColor(const Color &c);
  static const char *ResetColor();

private:
  struct Column {
    std::string title;
    int width;
    std::vector<Cell> cells;
  };

  void Render(int width);
  void Sort();

  void AddCell(size_t col, const Cell &cell);

  std::vector<Column> cols_;
  int num_rows_ = 0;

  size_t sort_column_;
  std::function<bool(const std::string &, const std::string &)> sorter_;
};

} // namespace toolbelt
