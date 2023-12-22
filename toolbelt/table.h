// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#pragma once

#include "toolbelt/color.h"

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace toolbelt {

class Table {
public:
  struct Cell {
    std::string data;
    color::Color color;
  };

  Table(const std::vector<std::string> titles, ssize_t sort_column = 0,
        std::function<bool(const std::string &, const std::string &)> comp =
            nullptr);
  ~Table();

  void AddRow(const std::vector<std::string> cells);
  void AddRow(const std::vector<std::string> cells, color::Color color);
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

  static Cell MakeCell(std::string data,
                color::Color color = {.mod = color::kNormal,
                                      .fixed = color::FixedColor::kNotSet}) {
    return Cell({.data = std::move(data), .color = color});
  }

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
