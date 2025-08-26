#include "toolbelt/fd.h"

namespace toolbelt {

// Close all open file descriptor for which the predicate returns true.
void CloseAllFds(std::function<bool(int)> predicate) {
  struct rlimit lim;
  int e = getrlimit(RLIMIT_NOFILE, &lim);
  if (e == 0) {
    for (rlim_t fd = 0; fd < lim.rlim_cur; fd++) {
      if (fcntl(fd, F_GETFD) == 0 && predicate(fd) ) {
        (void)close(fd);
      }
    }
  }
}

}