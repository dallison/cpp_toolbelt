// Copyright 2025 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "absl/strings/str_format.h"
#include "toolbelt/stacktrace.h"
#include <gtest/gtest.h>

TEST(StacktraceTest, PrintCurrentStack) {
  toolbelt::PrintCurrentStack(std::cout);
}

void foo() {
    toolbelt::PrintCurrentStack(std::cout);
}

void bar() {
    foo();
}

void baz() {
    bar();
}

TEST(StacktraceTest, PrintCurrentStackWithFunction) {
  baz();
}