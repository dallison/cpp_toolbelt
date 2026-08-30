// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "hexdump.h"

#include <cctype>
#include <cstdio>

namespace toolbelt {

void Hexdump(const void *addr, size_t length, FILE* out) {
  const char *p = reinterpret_cast<const char *>(addr);
  while (length > 0) {
    const size_t row_length = length < 16U ? length : 16U;
    fprintf(out, "%p ", static_cast<const void *>(p));
    for (size_t i = 0; i < row_length; i++) {
      fprintf(out, "%02X ", static_cast<unsigned char>(p[i]) & 0xffU);
    }
    for (size_t i = row_length; i < 16U; ++i) {
      fprintf(out, "   ");
    }
    fprintf(out, "  ");
    for (size_t i = 0; i < row_length; i++) {
      if (isprint(static_cast<unsigned char>(p[i]))) {
        fprintf(out, "%c", p[i]);
      } else {
        fprintf(out, ".");
      }
    }
    fprintf(out, "\n");
    p += row_length;
    length -= row_length;
  }
}

}  // namespace toolbelt
