// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "absl/status/status_matchers.h"
#include "co/coroutine.h"
#include "pipe.h"
#include <gtest/gtest.h>

#define VAR(a) a##__COUNTER__
#define EVAL_AND_ASSERT_OK(expr) EVAL_AND_ASSERT_OK2(VAR(r_), expr)

#define EVAL_AND_ASSERT_OK2(result, expr)                                      \
  ({                                                                           \
    auto result = (expr);                                                      \
    if (!result.ok()) {                                                        \
      std::cerr << result.status() << std::endl;                               \
    }                                                                          \
    ASSERT_OK(result);                                                         \
    std::move(*result);                                                        \
  })

#define ASSERT_OK(e) ASSERT_THAT(e, ::absl_testing::IsOk())

template <typename T> using SharedPtrPipe = toolbelt::SharedPtrPipe<T>;

struct TestStruct {
  TestStruct() = default;
  TestStruct(int x, int y) : a(x), b(y) {}
  int a;
  int b;
};

TEST(PipeTest, Create) {
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_OK(p);
}

TEST(PipeTest, SharedPtrWriteAndRead) {
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  auto t = std::make_shared<TestStruct>(1, 2);
  ASSERT_EQ(1, t.use_count());

  auto w_status = pipe.Write(t);
  ASSERT_OK(w_status);
  ASSERT_EQ(2, t.use_count());

  auto t2 = pipe.Read();
  ASSERT_OK(t2);

  auto rt = std::move(*t2);
  ASSERT_EQ(2, rt.use_count());

  ASSERT_EQ(rt->a, t->a);
  ASSERT_EQ(rt->b, t->b);
}

TEST(PipeTest, CoroutinePipeReadAndWrite) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  co::Coroutine reader(scheduler, [&pipe](co::Coroutine *c) {
    char buffer[20];
    auto r = pipe.Read(buffer, 5, c);
    ASSERT_OK(r);
    ASSERT_EQ(*r, 5);
    ASSERT_STREQ(buffer, "Hello");
  });
  co::Coroutine writer(scheduler, [&pipe](co::Coroutine *c) {
    const char *msg = "Hello";
    auto s = pipe.Write(msg, 5, c);
    ASSERT_OK(s);
    ASSERT_EQ(*s, 5);
  });
  scheduler.Run();
}

TEST(PipeTest, CoroutinePipeReadAndWriteNonblocking) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);
  ASSERT_OK(pipe.SetNonBlocking(true, true));

  co::Coroutine reader(scheduler, [&pipe](co::Coroutine *c) {
    char buffer[20];
    auto r = pipe.Read(buffer, 5, c);
    ASSERT_OK(r);
    ASSERT_EQ(*r, 5);
    ASSERT_STREQ(buffer, "Hello");
  });
  co::Coroutine writer(scheduler, [&pipe](co::Coroutine *c) {
    const char *msg = "Hello";
    auto s = pipe.Write(msg, 5, c);
    ASSERT_OK(s);
    ASSERT_EQ(*s, 5);
  });
  scheduler.Run();
}

TEST(PipeTest, CoroutinePtrPipeReadAndWrite) {
  co::CoroutineScheduler scheduler;
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  co::Coroutine reader(scheduler, [&pipe](co::Coroutine *c) {
    auto t = pipe.Read(c);
    ASSERT_OK(t);
    ASSERT_EQ(1, (*t)->a);
    ASSERT_EQ(2, (*t)->b);
  });
  co::Coroutine writer(scheduler, [&pipe](co::Coroutine *c) {
    auto t = std::make_shared<TestStruct>(1, 2);
    ASSERT_OK(pipe.Write(t, c));
  });
  scheduler.Run();
}

TEST(PipeTest, CoroutinePtrPipeReadAndWriteNonblocking) {
  co::CoroutineScheduler scheduler;
  auto p = SharedPtrPipe<TestStruct>::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);
  ASSERT_OK(pipe.SetNonBlocking(true, true));

  co::Coroutine reader(scheduler, [&pipe](co::Coroutine *c) {
    auto t = pipe.Read(c);
    ASSERT_OK(t);
    ASSERT_EQ(1, (*t)->a);
    ASSERT_EQ(2, (*t)->b);
  });
  co::Coroutine writer(scheduler, [&pipe](co::Coroutine *c) {
    auto t = std::make_shared<TestStruct>(1, 2);
    ASSERT_OK(pipe.Write(t, c));
  });
  scheduler.Run();
}

TEST(PipeTest, CoroutineFullPipeReadAndWrite) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = kPipeSize / kMessageSize;

  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      ASSERT_STREQ(buffer, "1234");
    }
  });
  co::Coroutine writer(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  scheduler.Run();
}

TEST(PipeTest, CoroutineOverFullPipeReadAndWrite) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = 10 * kPipeSize / kMessageSize;

  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      ASSERT_STREQ(buffer, "1234");
    }
  });
  co::Coroutine writer(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  scheduler.Run();
}

TEST(PipeTest, CoroutineFullPipeReadAndWriteNonblocking) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = kPipeSize / kMessageSize;

  ASSERT_OK(pipe.SetNonBlocking(true, true));
  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      ASSERT_STREQ(buffer, "1234");
    }
  });
  co::Coroutine writer(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  scheduler.Run();
}

TEST(PipeTest, CoroutineOverFullPipeReadAndWriteNonblocking) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = 10 * kPipeSize / kMessageSize;

  ASSERT_OK(pipe.SetNonBlocking(true, true));
  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      ASSERT_STREQ(buffer, "1234");
    }
  });
  co::Coroutine writer(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  scheduler.Run();
}

TEST(PipeTest, CoroutinePipeReadAndMultiWrite) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  co::Coroutine reader(scheduler, [&pipe](co::Coroutine *c) {
    char buffer[20];
    auto r = pipe.Read(buffer, 5, c);
    ASSERT_OK(r);
    ASSERT_EQ(*r, 5);
    ASSERT_STREQ(buffer, "12345");

    r = pipe.Read(buffer, 5, c);
    ASSERT_OK(r);
    ASSERT_EQ(*r, 5);
    ASSERT_STREQ(buffer, "54321");
  });

  co::Coroutine writer1(scheduler, [&pipe](co::Coroutine *c) {
    const char *msg = "12345";
    auto s = pipe.Write(msg, 5, c);
    ASSERT_OK(s);
    ASSERT_EQ(*s, 5);
  });

  co::Coroutine writer2(scheduler, [&pipe](co::Coroutine *c) {
    const char *msg = "54321";
    auto s = pipe.Write(msg, 5, c);
    ASSERT_OK(s);
    ASSERT_EQ(*s, 5);
  });
  scheduler.Run();
}

TEST(PipeTest, CoroutineOverFullPipeReadAndWriteMultiwriter) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = 10 * kPipeSize / kMessageSize;

  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages * 2; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      // Can be in either order.
      bool ok = (strcmp(buffer, "1234") == 0) || (strcmp(buffer, "4321") == 0);
      ASSERT_TRUE(ok);
    }
  });

  co::Coroutine writer1(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  co::Coroutine writer2(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "4321";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });
  scheduler.Run();
}


TEST(PipeTest, CoroutineOverFullPipeReadAndWriteMultiwriterNonblocking) {
  co::CoroutineScheduler scheduler;
  auto p = toolbelt::Pipe::Create();
  ASSERT_OK(p);
  auto pipe = std::move(*p);

  constexpr int kPipeSize = 65536;
  constexpr int kMessageSize = 4;
  constexpr int kNumMessages = 10 * kPipeSize / kMessageSize;

  ASSERT_OK(pipe.SetNonBlocking(true, true));
  co::Coroutine reader(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages * 2; i++) {
      char buffer[20];
      auto r = pipe.Read(buffer, kMessageSize, c);
      ASSERT_OK(r);
      ASSERT_EQ(*r, kMessageSize);
      // Can be in either order.
      bool ok = (strcmp(buffer, "1234") == 0) || (strcmp(buffer, "4321") == 0);
      ASSERT_TRUE(ok);
    }
  });

  co::Coroutine writer1(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "1234";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });

  co::Coroutine writer2(scheduler, [&pipe, kMessageSize](co::Coroutine *c) {
    for (int i = 0; i < kNumMessages; i++) {
      const char *msg = "4321";
      auto s = pipe.Write(msg, kMessageSize, c);
      ASSERT_OK(s);
      ASSERT_EQ(*s, kMessageSize);
    }
  });
  scheduler.Run();
}
