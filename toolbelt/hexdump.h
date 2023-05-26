// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __HEXDUMP_H
#define __HEXDUMP_H

#include <stddef.h>

namespace toolbelt {
// Almost the first thing I write in a new project is a hexdump
// function.  Very useful.
void Hexdump(const void* addr, size_t length);
}  // namespace toolbelt

#endif  // __HEXDUMP_H