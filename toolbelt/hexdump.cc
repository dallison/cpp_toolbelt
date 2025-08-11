// Copyright 2023 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "hexdump.h"

#include <cctype>
#include <cstdio>

namespace toolbelt {

void Hexdump(const void *addr, size_t length, FILE* out) {
  const char *p = reinterpret_cast<const char *>(addr);
  length = (length + 15) & ~15;
  while (length > 0) {
    fprintf(out, "%p ", p);
    for (int i = 0; i < 16; i++) {
     fprintf(out, "%02X ", p[i] & 0xff);
    }
    fprintf(out, "  ");
    for (int i = 0; i < 16; i++) {
      if (isprint(p[i])) {
        fprintf(out, "%c", p[i]);
      } else {
        fprintf(out, ".");
      }
    }
    fprintf(out, "\n");
    p += 16;
    length -= 16;
  }
}

}  // namespace toolbelt
