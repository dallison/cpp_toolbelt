// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/table.h"
#include "absl/strings/str_format.h"
#include <iomanip>

namespace toolbelt {
Table::Table(
    const std::vector<std::string> titles, ssize_t sort_column,
    std::function<bool(const std::string &, const std::string &)> comp) {
  SortBy(sort_column, comp);
  for (auto &title : titles) {
    cols_.push_back({.title = title});
  }
}

Table::~Table() {}

void Table::AddRow() {
  for (auto &col : cols_) {
    col.cells.push_back({});
  }
  ++num_rows_;
}

void Table::AddRow(const std::vector<std::string> cells) {
  AddRow(cells, {.mod = color::kNormal});
}

void Table::SetCell(size_t col, Cell &&cell) {
  cols_[col].cells[num_rows_ - 1] = std::move(cell);
}

void Table::AddRow(const std::vector<std::string> cells, color::Color color) {
  size_t index = 0;
  for (auto &cell : cells) {
    if (index >= cols_.size()) {
      break;
    }
    AddCell(index, {.data = cell, .color = color});
    index++;
  }
  ++num_rows_;
}

void Table::AddRowWithColors(const std::vector<Cell> cells) {
  size_t index = 0;
  for (auto &cell : cells) {
    if (index >= cols_.size()) {
      break;
    }
    AddCell(index, cell);
    index++;
  }
  ++num_rows_;
}

void Table::AddCell(size_t col, const Cell &cell) {
  cols_[col].cells.push_back(cell);
}

void Table::Print(int width, std::ostream &os) {
  if (width == 0) {
    width = 80;
  }
  width -= 1; // Allow space for newline.

  // Calculate the widths for each column.
  Render(width);

  // Print titles.
  for (auto &col : cols_) {
    std::string title = col.title;
    if (title.size() > static_cast<size_t>(col.width)) {
      title = title.substr(0, col.width - 1);
    }
    os << std::left << std::setw(col.width) << std::setfill(' ') << title;
  }
  os << std::endl;
  // Print separator line.
  os << std::setw(width) << std::setfill('-') << "" << std::endl;

  // Print each row.
  for (int i = 0; i < num_rows_; i++) {
    for (auto &col : cols_) {
      std::string data = col.cells[i].data;
      if (data.size() > static_cast<size_t>(col.width)) {
        // Truncate if too wide.
        data = data.substr(0, col.width - 1);
      }
      os << std::left << color::SetColor(col.cells[i].color)
         << std::setw(col.width) << std::setfill(' ') << data
         << color::ResetColor();
    }
    os << std::endl;
  }
}

void Table::Clear() {
  for (auto &col : cols_) {
    col.cells.clear();
  }
  num_rows_ = 0;
}

void Table::Render(int width) {
  std::vector<size_t> max_widths(cols_.size());
  for (int i = 0; i < num_rows_; i++) {
    int col_index = 0;
    for (auto &col : cols_) {
      if (col.cells[i].data.size() > max_widths[col_index]) {
        max_widths[col_index] = col.cells[i].data.size();
      }
      col_index++;
    }
  }
  size_t total_width = 0;
  for (size_t w : max_widths) {
    total_width += w;
  }
  // Pad the column widths out to the width we have.
  ssize_t padding = width - total_width;
  padding /= cols_.size();
  int index = 0;
  for (auto &col : cols_) {
    col.width = max_widths[index] + padding;
    index++;
  }
  Sort();
}

void Table::Sort() {
  if (sort_column_ == -1ULL || sort_column_ >= cols_.size()) {
    return;
  }
  struct Index {
    size_t row;
    std::string data;
  };
  std::vector<Index> index(num_rows_);
  for (int i = 0; i < num_rows_; i++) {
    index[i] = {.row = static_cast<size_t>(i), .data = cols_[sort_column_].cells[i].data};
  }
  std::sort(index.begin(), index.end(), [this](const Index &a, const Index &b) {
    return sorter_(a.data, b.data);
  });
  // Now we have the index in the correct order, reorder the table cells.
  // We do this my moving all the cells into a new vector of columns in
  // the order specified by the index.  We then use the sorted vector
  // as the new columns in the table.
  std::vector<Column> sorted;
  sorted.reserve(cols_.size());
  for (auto &col : cols_) {
    sorted.push_back({.title = col.title, .width = col.width});
  }
  for (auto &i : index) {
    // i.row contains the row number for the next row to be inserted
    // into the new sorted table.
    for (size_t col = 0; col < cols_.size(); col++) {
      sorted[col].cells.push_back(std::move(cols_[col].cells[i.row]));
    }
  }
  cols_ = std::move(sorted);
}

} // namespace toolbelt