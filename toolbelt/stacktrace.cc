
#include "absl/debugging/stacktrace.h"
#include "absl/base/optimization.h"
#include "absl/debugging/symbolize.h"
#include <iomanip>
#include <iostream>

namespace toolbelt {

void PrintCurrentStack(std::ostream &os) {
  os << "--- Stack Trace Capture (Deepest Function) ---\n";

  constexpr int kMaxFrames = 50;

  // 1. Capture the raw stack addresses
  void *stack[kMaxFrames];
  int depth = absl::GetStackTrace(stack, kMaxFrames, 0);

  // 2. Resolve addresses to human-readable symbol names
  os << "Captured " << depth << " stack frames:\n";

  // Buffer to hold the symbolized name
  char symbolized_name[1024];

  for (int i = 0; i < depth; ++i) {
    // Attempt to symbolize the address
    if (absl::Symbolize(stack[i], symbolized_name, sizeof(symbolized_name))) {
      // Success: Print the frame index, address, and resolved symbol name
      os << "#" << std::setw(2) << std::left << i << " [0x" << std::hex
         << std::setw(16) << stack[i] << std::dec << "] " << symbolized_name
         << "\n";
    } else {
      // Failure: Symbolization failed (e.g., address not in a symbol table)
      os << "#" << std::setw(2) << std::left << i << " [0x" << std::hex
         << std::setw(16) << stack[i] << std::dec << "] "
         << "<unresolved>\n";
    }
  }
  os << "----------------------------------------------\n";
}

} // namespace toolbelt
