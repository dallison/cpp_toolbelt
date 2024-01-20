// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "pipe.h"
#include <gtest/gtest.h>

template <typename T> using SharedPtrPipe = toolbelt::SharedPtrPipe<T>;

struct TestStruct {
  TestStruct() = default;
  TestStruct(int x, int y) : a(x), b(y) {}
  int a;
  int b;
};

TEST(PipeTest, Create) {
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_TRUE(p.ok());
}

TEST(PipeTest, WriteAndRead) {
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_TRUE(p.ok());
  auto pipe = std::move(*p);

  auto t = std::make_shared<TestStruct>(1, 2);
  ASSERT_EQ(1, t.use_count());

  auto w_status = pipe.Write(t);
  ASSERT_TRUE(w_status.ok());
  ASSERT_EQ(2, t.use_count());

  auto t2 = pipe.Read();
  ASSERT_TRUE(t2.ok());

  auto rt = std::move(*t2);
  ASSERT_EQ(3, rt.use_count());

  ASSERT_EQ(rt->a, t->a);
  ASSERT_EQ(rt->b, t->b);
}
