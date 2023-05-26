# cpp_toolbelt
Collection of useful C++ classes and utility functions

I find these useful when writing C++ code.

The classes are:

1. FileDescriptor: a reference counted UNIX file descriptor representing an open file
2. InetAddress: an interet address (IPv4 for now)
3. Socket: a general socket
4. UnixSocket: a UNIX Domain socket
5. TCPSocket: a TCP socket
6. UDPSocket: a UDP socket
7. Logger: a level-aware logger that prints in color on TTYs
8. MutexLock: an RAII class for handling pthread mutexes
9. BitSet: a fixed size set of bits that allows allocation.

In addition, the following functions are provided:

1. Hexdump: dump memory in hex
2. Now: get the current nanosecond monotonic time.

The Socket classes are coroutine aware and need my
[coroutine library](https://github.com/dallison/cocpp)

Enjoy!

  
