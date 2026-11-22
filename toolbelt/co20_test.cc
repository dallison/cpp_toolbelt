// Copyright 2023-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "absl/status/status_matchers.h"
#include "co/coroutine_cpp20.h"
#include "gtest/gtest.h"
#include "toolbelt/co20.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

namespace toolbelt {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;

// ASSERT_* expands to a plain return, which is illegal in a Task coroutine.
// Use EXPECT_* (or check Status and co_return) inside co20 bodies.

TEST(Co20Test, PipeReadAndWrite) {
  ::co20::Scheduler scheduler;
  auto pipe_or = Pipe::Create();
  ASSERT_THAT(pipe_or, IsOk());
  Pipe pipe = std::move(*pipe_or);
  ASSERT_THAT(pipe.SetNonBlocking(true, true), IsOk());

  std::string received;
  scheduler.Spawn(
      [&pipe, &received]() -> ::co20::Task {
        char buffer[64] = {};
        auto n = co_await await::Read(pipe, buffer, 5);
        EXPECT_THAT(n, IsOkAndHolds(5));
        if (n.ok()) {
          received.assign(buffer, static_cast<size_t>(*n));
        }
        co_return;
      },
      "reader");

  scheduler.Spawn(
      [&pipe]() -> ::co20::Task {
        const char *msg = "hello";
        auto n = co_await await::Write(pipe, msg, 5);
        EXPECT_THAT(n, IsOkAndHolds(5));
        co_return;
      },
      "writer");

  scheduler.Run();
  EXPECT_EQ(received, "hello");
}

TEST(Co20Test, SharedPtrPipe) {
  ::co20::Scheduler scheduler;
  auto pipe_or = SharedPtrPipe<std::string>::Create();
  ASSERT_THAT(pipe_or, IsOk());
  SharedPtrPipe<std::string> pipe = std::move(*pipe_or);
  ASSERT_THAT(pipe.SetNonBlocking(true, true), IsOk());

  std::shared_ptr<std::string> received;
  scheduler.Spawn(
      [&pipe, &received]() -> ::co20::Task {
        auto result = co_await await::Read(pipe);
        EXPECT_THAT(result, IsOk());
        if (result.ok()) {
          received = std::move(*result);
        }
        co_return;
      },
      "reader");

  scheduler.Spawn(
      [&pipe]() -> ::co20::Task {
        auto status =
            co_await await::Write(pipe, std::make_shared<std::string>("world"));
        EXPECT_THAT(status, IsOk());
        co_return;
      },
      "writer");

  scheduler.Run();
  ASSERT_NE(received, nullptr);
  EXPECT_EQ(*received, "world");
}

TEST(Co20Test, UnixSocketSendReceive) {
  char tmp[] = "/tmp/toolbelt_co20_XXXXXX";
  int fd = mkstemp(tmp);
  ASSERT_NE(-1, fd);
  std::string socket_name = tmp;
  close(fd);
  unlink(socket_name.c_str());

  ::co20::Scheduler scheduler;

  UnixSocket listener;
  ASSERT_THAT(listener.Bind(socket_name, /*listen=*/true), IsOk());
  ASSERT_THAT(listener.SetNonBlocking(), IsOk());

  std::string received;
  scheduler.Spawn(
      [&listener, &received]() -> ::co20::Task {
        auto client = co_await await::Accept(listener);
        EXPECT_THAT(client, IsOk());
        if (!client.ok()) {
          co_return;
        }
        EXPECT_THAT(client->SetNonBlocking(), IsOk());
        char buffer[64] = {};
        auto n = co_await await::Receive(*client, buffer, 5);
        EXPECT_THAT(n, IsOkAndHolds(5));
        if (n.ok()) {
          received.assign(buffer, static_cast<size_t>(*n));
        }
        co_return;
      },
      "server");

  scheduler.Spawn(
      [&socket_name]() -> ::co20::Task {
        UnixSocket client;
        EXPECT_THAT(client.Connect(socket_name), IsOk());
        EXPECT_THAT(client.SetNonBlocking(), IsOk());
        co_await ::co20::Yield();
        auto n = co_await await::Send(client, "hello", 5);
        EXPECT_THAT(n, IsOkAndHolds(5));
        co_return;
      },
      "client");

  scheduler.Run();
  EXPECT_EQ(received, "hello");
  unlink(socket_name.c_str());
}

TEST(Co20Test, FileDescriptorReadWrite) {
  ::co20::Scheduler scheduler;
  auto pipe_or = Pipe::Create();
  ASSERT_THAT(pipe_or, IsOk());
  Pipe pipe = std::move(*pipe_or);
  ASSERT_THAT(pipe.SetNonBlocking(true, true), IsOk());

  std::string received;
  scheduler.Spawn(
      [&pipe, &received]() -> ::co20::Task {
        char buffer[64] = {};
        auto n = co_await await::Read(pipe.ReadFd(), buffer, 4);
        EXPECT_THAT(n, IsOkAndHolds(4));
        if (n.ok()) {
          received.assign(buffer, static_cast<size_t>(*n));
        }
        co_return;
      },
      "reader");

  scheduler.Spawn(
      [&pipe]() -> ::co20::Task {
        auto n = co_await await::Write(pipe.WriteFd(), "abcd", 4);
        EXPECT_THAT(n, IsOkAndHolds(4));
        co_return;
      },
      "writer");

  scheduler.Run();
  EXPECT_EQ(received, "abcd");
}

TEST(Co20Test, ConcurrentPipesDoNotBlockScheduler) {
  // If await::Read used AsyncWaiter::Wait (blocking poll) instead of
  // co_await Wait, the reader would stall the whole scheduler and the writer
  // would never run.
  ::co20::Scheduler scheduler;
  auto pipe_or = Pipe::Create();
  ASSERT_THAT(pipe_or, IsOk());
  Pipe pipe = std::move(*pipe_or);
  ASSERT_THAT(pipe.SetNonBlocking(true, true), IsOk());

  bool reader_done = false;
  bool writer_ran = false;

  scheduler.Spawn(
      [&]() -> ::co20::Task {
        char buffer[8] = {};
        auto n = co_await await::Read(pipe, buffer, 1);
        EXPECT_THAT(n, IsOkAndHolds(1));
        reader_done = true;
        co_return;
      },
      "reader");

  scheduler.Spawn(
      [&]() -> ::co20::Task {
        writer_ran = true;
        auto n = co_await await::Write(pipe, "x", 1);
        EXPECT_THAT(n, IsOkAndHolds(1));
        co_return;
      },
      "writer");

  scheduler.Run();
  EXPECT_TRUE(writer_ran);
  EXPECT_TRUE(reader_done);
}

} // namespace
} // namespace toolbelt
