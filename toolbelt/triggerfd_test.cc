// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "toolbelt/triggerfd.h"
#include <gtest/gtest.h>
#include <poll.h>

namespace {

bool Readable(toolbelt::TriggerFd &t) {
  struct pollfd pfd = t.GetPollFd().GetPollFd();
  return ::poll(&pfd, 1, 0) == 1 && (pfd.revents & POLLIN) != 0;
}

TEST(TriggerFdTest, CreateAndClearUntriggered) {
  auto t = toolbelt::TriggerFd::Create();
  ASSERT_TRUE(t.ok());
  ASSERT_TRUE(t->GetPollFd().Valid());
  ASSERT_TRUE(t->GetTriggerFd().Valid());
  ASSERT_FALSE(Readable(*t));
  ASSERT_FALSE(t->Clear());
}

TEST(TriggerFdTest, TriggerThenClear) {
  auto t = toolbelt::TriggerFd::Create();
  ASSERT_TRUE(t.ok());
  t->Trigger();
  ASSERT_TRUE(Readable(*t));
  ASSERT_TRUE(t->Clear());
  ASSERT_FALSE(Readable(*t));
  ASSERT_FALSE(t->Clear());
}

TEST(TriggerFdTest, MultipleTriggersClearedAtOnce) {
  auto t = toolbelt::TriggerFd::Create();
  ASSERT_TRUE(t.ok());
  for (int i = 0; i < 10; i++) {
    t->Trigger();
  }
  ASSERT_TRUE(t->Clear());
  ASSERT_FALSE(Readable(*t));
}

TEST(TriggerFdTest, TriggerNeverBlocksWhenFull) {
  auto t = toolbelt::TriggerFd::Create();
  ASSERT_TRUE(t.ok());
  // Far more triggers than any pipe/socket buffer can hold.  The trigger fd is
  // nonblocking so this must not hang.
  for (int i = 0; i < 1000000; i++) {
    t->Trigger();
  }
  ASSERT_TRUE(t->Clear());
  ASSERT_FALSE(Readable(*t));
}

TEST(TriggerFdTest, Move) {
  auto t = toolbelt::TriggerFd::Create();
  ASSERT_TRUE(t.ok());
  toolbelt::TriggerFd moved = std::move(*t);
  ASSERT_FALSE(t->GetPollFd().Valid());
  moved.Trigger();
  ASSERT_TRUE(moved.Clear());
}

} // namespace
