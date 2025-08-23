// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#ifndef __TOOLBELT_HEXDUMP_H
#define __TOOLBELT_HEXDUMP_H

#include <stddef.h>
#include <stdio.h>

namespace toolbelt {
// Almost the first thing I write in a new project is a hexdump
// function.  Very useful.
void Hexdump(const void* addr, size_t length, FILE* out = stdout);
}  // namespace toolbelt

#endif  //  __TOOLBELT_HEXDUMP_H