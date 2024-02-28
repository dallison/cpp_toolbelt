// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "absl/strings/str_format.h"
#include "toolbelt/table.h"
#include <gtest/gtest.h>
#include <sys/stat.h>

#include <sys/ioctl.h>
#include <termios.h>

using Table = toolbelt::Table;
namespace color = toolbelt::color;

TEST(FdTest, WithPadding) {
  Table table({"name", "rank", "serial", "address"});

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow({"Dave", "Captain", "1234", "here"});
  table.AddRow({"John", "Major", "4321", "there"});
  table.Print(win.ws_col, std::cout);
}

TEST(FdTest, SortDefault) {
  Table table({"name", "rank", "serial", "address"});

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow({"Bob", "Major", "4321", "there"});
  table.AddRow({"Zeb", "Captain", "1234", "here"});
  table.AddRow({"Alex", "Corporal", "43221", "also"});
  table.Print(win.ws_col, std::cout);
}

TEST(FdTest, SortNumber) {
  Table table({"name", "rank", "serial", "address"}, 2,
              [](const std::string &a, const std::string &b) -> bool {
                return atoi(a.c_str()) < atoi(b.c_str());
              });

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow({"Bob", "Major", "4321", "there"});
  table.AddRow({"Zeb", "Captain", "1234", "here"});
  table.AddRow({"Alex", "Corporal", "43221", "also"});
  table.Print(win.ws_col, std::cout);
}

TEST(FdTest, TooWide) {
  Table table({"name", "rank", "serial", "address"});

  table.AddRow({"Dave", "Captain", "1234", "here"});
  table.AddRow({"John", "Major", "4321", "there"});
  table.Print(20, std::cout);
}

TEST(FdTest, Colors) {
  Table table({"subsystem", "admin", "oper"});

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow();
  table.SetCell(0, Table::MakeCell("navigation", color::BoldMagenta()));
  table.SetCell(1, Table::MakeCell("online", color::Green()));
  table.SetCell(2, Table::MakeCell("offline", color::BoldRed()));

  table.AddRow({"localization", "offline", "broken"}, color::BoldCyan());
  table.Print(win.ws_col, std::cout);
}

// This may not work on your terminal.  It doesn't on Apple terminal on my
// mac.
TEST(FdTest, RGB) {
  Table table({"subsystem", "admin", "oper"});

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow();
  table.SetCell(0,
                Table::MakeCell("navigation", color::MakeRGB(128, 128, 128)));
  table.SetCell(1, Table::MakeCell("online", color::MakeRGB(0, 200, 0)));
  table.SetCell(2, Table::MakeCell("offline", color::MakeRGB(1, 1, 128)));
  table.Print(win.ws_col, std::cout);
}

// This seems to be more widely supported.
TEST(FdTest, EightBit) {
  Table table({"subsystem", "admin", "oper"});

  struct winsize win = {0};
  ioctl(0, TIOCGWINSZ, &win); // Might fail.

  table.AddRow();
  table.SetCell(0, Table::MakeCell("navigation", color::Make8Bit(196)));
  table.SetCell(1, Table::MakeCell("online", color::Make8Bit(82)));
  table.SetCell(2, Table::MakeCell("offline", color::Make8Bit(33)));
  table.Print(win.ws_col, std::cout);
}