// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_CLOCK_H
#define __TOOLBELT_CLOCK_H

#include <cstdint>
#include <time.h>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace toolbelt {

#if defined(__APPLE__)
namespace {
mach_timebase_info_data_t timebase;
bool timebase_set = false;
uint64_t current_timebase;
} // namespace
#endif

// Current monotonic time in nanoseconds.
inline uint64_t Now() {
#if defined(__APPLE__)
  if (!timebase_set) {
    mach_timebase_info(&timebase);
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    current_timebase = (static_cast<uint64_t>(tp.tv_sec) * 1000000000LL +
                        static_cast<uint64_t>(tp.tv_nsec)) -
                       mach_absolute_time() * timebase.numer / timebase.denom;
    timebase_set = true;
  }
  return current_timebase +
         mach_absolute_time() * timebase.numer / timebase.denom;
#else
  struct timespec tp;
  clock_gettime(CLOCK_MONOTONIC, &tp);
  return static_cast<uint64_t>(tp.tv_sec) * 1000000000LL +
         static_cast<uint64_t>(tp.tv_nsec);
#endif
}
} // namespace toolbelt

#endif //  __CLOCK_H
