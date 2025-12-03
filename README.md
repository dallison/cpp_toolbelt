# cpp_toolbelt

A collection of useful C++ classes and utility functions for systems programming.

## Table of Contents

- [Installation](#installation)
- [Classes](#classes)
  - [FileDescriptor](#filedescriptor)
  - [Socket Classes](#socket-classes)
    - [InetAddress](#inetaddress)
    - [VirtualAddress](#virtualaddress)
    - [SocketAddress](#socketaddress)
    - [Socket](#socket)
    - [UnixSocket](#unixsocket)
    - [TCPSocket](#tcpsocket)
    - [UDPSocket](#udpsocket)
    - [VirtualStreamSocket](#virtualstreamsocket)
    - [StreamSocket](#streamsocket)
  - [Logger](#logger)
  - [MutexLock and RWLock](#mutexlock-and-rwlock)
  - [BitSet](#bitset)
  - [Pipe](#pipe)
  - [Table](#table)
  - [PayloadBuffer](#payloadbuffer)
  - [TriggerFd](#triggerfd)
- [Utility Functions](#utility-functions)
  - [Now](#now)
  - [Hexdump](#hexdump)
  - [PrintCurrentStack](#printcurrentstack)

## Installation

### Using Bazel

Add this to your `MODULE.bazel` or `WORKSPACE` file:

```python
http_archive(
    name = "toolbelt",
    urls = ["https://github.com/dallison/cpp_toolbelt/archive/refs/tags/A.B.C.tar.gz"],
    strip_prefix = "cpp_toolbelt-A.B.C",
)
```

Replace `A.B.C` with the version you want (e.g., `1.0.3`).

Add a dependency to your `BUILD.bazel` targets:

```python
deps = [
    # ...
    "@toolbelt//toolbelt",
],
```

### Using CMake

```cmake
include(FetchContent)
FetchContent_Declare(
    toolbelt
    GIT_REPOSITORY https://github.com/dallison/cpp_toolbelt.git
    GIT_TAG <version-tag>
)
FetchContent_MakeAvailable(toolbelt)

target_link_libraries(your_target PRIVATE toolbelt)
```

## Classes

### FileDescriptor

A reference-counted wrapper around UNIX file descriptors. Automatically closes the file descriptor when all references are destroyed.

**Header:** `toolbelt/fd.h`

#### API

```cpp
class FileDescriptor {
public:
    FileDescriptor();
    explicit FileDescriptor(int fd, bool owned = true);
    FileDescriptor(const FileDescriptor &f);
    FileDescriptor(FileDescriptor &&f);
    
    void Close();
    bool IsOpen() const;
    bool IsATTY() const;
    int RefCount() const;
    struct pollfd GetPollFd();
    bool Valid() const;
    int Fd() const;
    void SetFd(int fd, bool owned = true);
    void Reset();
    void Release();
    void ForceClose();
    bool IsNonBlocking() const;
    absl::Status SetNonBlocking();
    absl::Status SetCloseOnExec();
    absl::StatusOr<ssize_t> Read(void* buffer, size_t length, 
                                  const co::Coroutine* c = nullptr);
    absl::StatusOr<ssize_t> Write(const void* buffer, size_t length,
                                  const co::Coroutine* c = nullptr);
};
```

#### Example

```cpp
#include "toolbelt/fd.h"

// Create from an existing file descriptor
int fd = open("/path/to/file", O_RDONLY);
toolbelt::FileDescriptor file(fd);

// Copy creates a new reference (cheap, just increments ref count)
toolbelt::FileDescriptor file2 = file;
assert(file.RefCount() == 2);

// File descriptor is automatically closed when last reference is destroyed
file.Close();  // Still open, file2 still references it
// file2 goes out of scope -> file descriptor is closed

// Read from file descriptor
char buffer[1024];
auto result = file.Read(buffer, sizeof(buffer));
if (result.ok()) {
    size_t bytes_read = *result;
    // Process data...
}
```

### Socket Classes

The socket classes provide a high-level interface to various socket types. They are coroutine-aware and work with the [co coroutine library](https://github.com/dallison/co).

**Header:** `toolbelt/sockets.h`

#### InetAddress

Represents an IPv4 internet address and port.

```cpp
class InetAddress {
public:
    InetAddress();
    InetAddress(int port);  // INADDR_ANY with given port
    InetAddress(const in_addr &ip, int port);
    InetAddress(const std::string &hostname, int port);
    InetAddress(const struct sockaddr_in &addr);
    
    const sockaddr_in &GetAddress() const;
    socklen_t GetLength() const;
    bool Valid() const;
    in_addr IpAddress() const;  // Host byte order
    int Port() const;           // Host byte order
    void SetPort(int port);
    std::string ToString() const;
    
    static InetAddress BroadcastAddress(int port);
    static InetAddress AnyAddress(int port);
};
```

#### Example

```cpp
#include "toolbelt/sockets.h"

// Create address from hostname and port
toolbelt::InetAddress addr("localhost", 8080);

// Create address for any interface on port 8080
auto any_addr = toolbelt::InetAddress::AnyAddress(8080);

// Get address components
in_addr ip = addr.IpAddress();
int port = addr.Port();
std::string str = addr.ToString();  // "127.0.0.1:8080"
```

#### VirtualAddress

Represents a virtual socket address (VSOCK) used for communication between VMs and the host.

```cpp
class VirtualAddress {
public:
    VirtualAddress();
    VirtualAddress(uint32_t port);
    VirtualAddress(uint32_t cid, uint32_t port);
    
    uint32_t Cid() const;
    uint32_t Port() const;
    void SetPort(uint32_t port);
    std::string ToString() const;
    
    static VirtualAddress HypervisorAddress(uint32_t port);
    static VirtualAddress HostAddress(uint32_t port);
    static VirtualAddress AnyAddress(uint32_t port);
    #if defined(__linux__)
    static VirtualAddress LocalAddress(uint32_t port);
    #endif
};
```

#### SocketAddress

A variant type that can hold an `InetAddress`, `VirtualAddress`, or Unix socket path (string).

```cpp
class SocketAddress {
public:
    SocketAddress();
    SocketAddress(const InetAddress &addr);
    SocketAddress(const VirtualAddress &addr);
    SocketAddress(const std::string &addr);  // Unix socket path
    
    const InetAddress &GetInetAddress() const;
    const VirtualAddress &GetVirtualAddress() const;
    const std::string &GetUnixAddress() const;
    std::string ToString() const;
    bool Valid() const;
    int Type() const;  // kAddressInet, kAddressVirtual, or kAddressUnix
    int Port() const;
};
```

#### Socket

Base class for all socket types. Provides common functionality for sending and receiving data.

```cpp
class Socket {
public:
    void Close();
    bool Connected() const;
    absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                    const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                                 const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> ReceiveMessage(char *buffer, size_t buflen,
                                           const co::Coroutine *c = nullptr);
    absl::StatusOr<std::vector<char>> ReceiveVariableLengthMessage(
        const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> SendMessage(char *buffer, size_t length,
                                        const co::Coroutine *c = nullptr);
    absl::Status SetNonBlocking();
    FileDescriptor GetFileDescriptor() const;
    absl::Status SetCloseOnExec();
    bool IsNonBlocking() const;
    bool IsBlocking() const;
};
```

#### UnixSocket

A Unix Domain socket for inter-process communication.

```cpp
class UnixSocket : public Socket {
public:
    UnixSocket();
    explicit UnixSocket(int fd, bool connected = false);
    
    absl::Status Bind(const std::string &pathname, bool listen);
    absl::Status Connect(const std::string &pathname);
    absl::StatusOr<UnixSocket> Accept(const co::Coroutine *c = nullptr) const;
    absl::Status SendFds(const std::vector<FileDescriptor> &fds,
                        const co::Coroutine *c = nullptr);
    absl::Status ReceiveFds(std::vector<FileDescriptor> &fds,
                            const co::Coroutine *c = nullptr);
    std::string BoundAddress() const;
    absl::StatusOr<std::string> GetPeerName() const;
    absl::StatusOr<std::string> LocalAddress() const;
};
```

#### Example

```cpp
#include "toolbelt/sockets.h"

// Server side
toolbelt::UnixSocket server;
server.Bind("/tmp/mysocket", true);  // true = listen

// In a coroutine or event loop
auto client = server.Accept(coroutine);
if (client.ok()) {
    char buffer[1024];
    auto result = client->Receive(buffer, sizeof(buffer), coroutine);
    // Process received data...
}

// Client side
toolbelt::UnixSocket client;
client.Connect("/tmp/mysocket");
client.Send("Hello", 5, coroutine);
```

#### TCPSocket

A TCP socket for network communication.

```cpp
class TCPSocket : public NetworkSocket {
public:
    TCPSocket();
    explicit TCPSocket(int fd, bool connected = false);
    
    absl::Status Bind(const InetAddress &addr, bool listen);
    absl::StatusOr<TCPSocket> Accept(const co::Coroutine *c = nullptr) const;
    absl::StatusOr<InetAddress> LocalAddress(int port) const;
    absl::StatusOr<InetAddress> GetPeerName() const;
};
```

#### Example

```cpp
#include "toolbelt/sockets.h"

// Server
toolbelt::TCPSocket server;
toolbelt::InetAddress addr(8080);
server.Bind(addr, true);  // Listen on port 8080

// Accept connections
auto client = server.Accept(coroutine);
if (client.ok()) {
    char buffer[1024];
    auto result = client->Receive(buffer, sizeof(buffer), coroutine);
}

// Client
toolbelt::TCPSocket client;
toolbelt::InetAddress server_addr("example.com", 8080);
client.Connect(server_addr);
client.Send("Hello", 5, coroutine);
```

#### UDPSocket

A UDP socket for datagram communication.

```cpp
class UDPSocket : public NetworkSocket {
public:
    UDPSocket();
    explicit UDPSocket(int fd, bool connected = false);
    
    absl::Status Bind(const InetAddress &addr);
    absl::Status JoinMulticastGroup(const InetAddress &addr);
    absl::Status LeaveMulticastGroup(const InetAddress &addr);
    absl::Status SendTo(const InetAddress &addr, const void *buffer,
                       size_t length, const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> Receive(void *buffer, size_t buflen,
                                    const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> ReceiveFrom(InetAddress &sender, void *buffer,
                                        size_t buflen,
                                        const co::Coroutine *c = nullptr);
    absl::Status SetBroadcast();
    absl::Status SetMulticastLoop();
};
```

#### Example

```cpp
#include "toolbelt/sockets.h"

// Server
toolbelt::UDPSocket socket;
toolbelt::InetAddress addr(8080);
socket.Bind(addr);

toolbelt::InetAddress sender;
char buffer[1024];
auto result = socket.ReceiveFrom(sender, buffer, sizeof(buffer), coroutine);
if (result.ok()) {
    // Process datagram from sender
}

// Client
toolbelt::UDPSocket socket;
toolbelt::InetAddress server("example.com", 8080);
socket.SendTo(server, "Hello", 5, coroutine);
```

#### VirtualStreamSocket

A virtual stream socket for VM-to-host communication using VSOCK.

```cpp
class VirtualStreamSocket : public Socket {
public:
    VirtualStreamSocket();
    explicit VirtualStreamSocket(int fd, bool connected = false);
    
    absl::Status Connect(const VirtualAddress &addr);
    absl::Status Bind(const VirtualAddress &addr, bool listen);
    absl::StatusOr<VirtualStreamSocket> Accept(
        const co::Coroutine *c = nullptr) const;
    absl::StatusOr<VirtualAddress> LocalAddress(uint32_t port) const;
    const VirtualAddress &BoundAddress() const;
    absl::StatusOr<VirtualAddress> GetPeerName() const;
    uint32_t Cid() const;
};
```

#### StreamSocket

A type-erased wrapper that can hold a `TCPSocket`, `VirtualStreamSocket`, or `UnixSocket`. Useful when you need to work with different socket types uniformly.

```cpp
class StreamSocket {
public:
    StreamSocket();
    
    absl::Status Bind(const SocketAddress &addr, bool listen);
    absl::Status Connect(const SocketAddress &addr);
    absl::StatusOr<StreamSocket> Accept(const co::Coroutine *c = nullptr) const;
    
    TCPSocket &GetTCPSocket();
    VirtualStreamSocket &GetVirtualStreamSocket();
    UnixSocket &GetUnixSocket();
    
    SocketAddress BoundAddress() const;
    void Close();
    bool Connected() const;
    absl::StatusOr<ssize_t> Receive(char *buffer, size_t buflen,
                                    const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> Send(const char *buffer, size_t length,
                                 const co::Coroutine *c = nullptr);
    // ... other Socket methods
};
```

#### Example

```cpp
#include "toolbelt/sockets.h"

// Works with any socket type
toolbelt::StreamSocket socket;

// Can bind to TCP, Unix, or VSOCK addresses
toolbelt::SocketAddress addr = toolbelt::InetAddress(8080);
// or: toolbelt::SocketAddress addr("/tmp/socket");
// or: toolbelt::SocketAddress addr(toolbelt::VirtualAddress(1234));

socket.Bind(addr, true);
auto client = socket.Accept(coroutine);
```

### Logger

A level-aware logger that supports colored output, multiple output modes, and themes.

**Header:** `toolbelt/logging.h`

#### API

```cpp
enum class LogLevel {
    kVerboseDebug,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kFatal,
};

enum class LogDisplayMode {
    kPlain,
    kColor,
    kColumnar,
};

enum class LogTheme {
    kDefault,
    kLight,
    kDark,
};

class Logger {
public:
    Logger();
    Logger(const std::string &subsystem, bool enabled = true,
           LogTheme theme = LogTheme::kDefault,
           LogDisplayMode mode = LogDisplayMode::kPlain);
    Logger(LogLevel min);
    
    void Enable();
    void Disable();
    absl::Status SetTeeFile(const std::string &filename, bool truncate = true);
    void SetTeeStream(FILE *stream);
    void Log(LogLevel level, const char *fmt, ...);
    void VLog(LogLevel level, const char *fmt, va_list ap);
    void Log(LogLevel level, uint64_t timestamp,
             const std::string &source, std::string text);
    void SetTheme(LogTheme theme);
    void SetLogLevel(LogLevel l);
    void SetLogLevel(const std::string &s);  // "verbose", "debug", "info", etc.
    LogLevel GetLogLevel() const;
    void SetOutputStream(FILE *stream);
};
```

#### Example

```cpp
#include "toolbelt/logging.h"

// Create logger with subsystem name
toolbelt::Logger logger("myapp", true, toolbelt::LogTheme::kDefault,
                       toolbelt::LogDisplayMode::kColor);

// Set minimum log level
logger.SetLogLevel(toolbelt::LogLevel::kInfo);
// or
logger.SetLogLevel("info");

// Log messages
logger.Log(toolbelt::LogLevel::kInfo, "Application started");
logger.Log(toolbelt::LogLevel::kDebug, "Debug value: %d", 42);
logger.Log(toolbelt::LogLevel::kWarning, "Warning: %s", "Something happened");
logger.Log(toolbelt::LogLevel::kError, "Error code: %d", errno);

// Tee output to a file
logger.SetTeeFile("/var/log/myapp.log");

// Disable logging temporarily
logger.Disable();
// ... do something ...
logger.Enable();
```

### MutexLock and RWLock

RAII wrappers for pthread mutexes and read-write locks.

**Header:** `toolbelt/mutex.h`

#### API

```cpp
class MutexLock {
public:
    MutexLock(pthread_mutex_t *mutex);
    ~MutexLock();
};

class RWLock {
public:
    RWLock(pthread_rwlock_t *lock, bool read);
    ~RWLock();
    
    void ReadLock();
    void WriteLock();
    void Unlock();
};
```

#### Example

```cpp
#include "toolbelt/mutex.h"
#include <pthread.h>

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

{
    toolbelt::MutexLock lock(&mutex);
    // Critical section - mutex is automatically unlocked when lock goes out of scope
    // Do work...
}

pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

{
    toolbelt::RWLock lock(&rwlock, true);  // true = read lock
    // Multiple readers allowed
    // Read data...
}

{
    toolbelt::RWLock lock(&rwlock, false);  // false = write lock
    // Exclusive write access
    // Write data...
}
```

### BitSet

A fixed-size bitset that allows allocation of individual bits.

**Header:** `toolbelt/bitset.h`

#### API

```cpp
template <int Size>
class BitSet {
public:
    BitSet();
    void Init();
    absl::StatusOr<int> Allocate(const std::string &type);
    bool IsEmpty() const;
    void Set(int b);
    void Clear(int b);
    bool IsSet(int b) const;
};
```

#### Example

```cpp
#include "toolbelt/bitset.h"

// Create a bitset with 256 bits
toolbelt::BitSet<256> bitset;

// Allocate the first available bit
auto result = bitset.Allocate("resource");
if (result.ok()) {
    int bit_index = *result;
    // Use the bit...
    
    // Check if bit is set
    if (bitset.IsSet(bit_index)) {
        // ...
    }
    
    // Clear the bit when done
    bitset.Clear(bit_index);
}

// Check if all bits are clear
if (bitset.IsEmpty()) {
    // All resources freed
}
```

### Pipe

A pipe for inter-process or inter-coroutine communication. Supports both blocking and non-blocking I/O, and can work with coroutines.

**Header:** `toolbelt/pipe.h`

#### API

```cpp
class Pipe {
public:
    static absl::StatusOr<Pipe> Create();
    static absl::StatusOr<Pipe> CreateWithFlags(int flags);
    static absl::StatusOr<Pipe> Create(int r, int w);
    
    Pipe();
    Pipe(int r, int w);
    
    absl::Status Open(int flags = 0);
    FileDescriptor &ReadFd();
    FileDescriptor &WriteFd();
    void SetReadFd(int fd);
    void SetWriteFd(int fd);
    void Close();
    void ForceClose();
    absl::Status SetNonBlocking(bool read, bool write);
    absl::StatusOr<size_t> GetPipeSize();
    absl::Status SetPipeSize(size_t size);
    absl::StatusOr<ssize_t> Read(char *buffer, size_t length,
                                 const co::Coroutine *c = nullptr);
    absl::StatusOr<ssize_t> Write(const char *buffer, size_t length,
                                  const co::Coroutine *c = nullptr);
};

template <typename T>
class SharedPtrPipe : public Pipe {
public:
    static absl::StatusOr<SharedPtrPipe<T>> Create();
    static absl::StatusOr<SharedPtrPipe<T>> Create(int r, int w);
    
    absl::StatusOr<std::shared_ptr<T>> Read(const co::Coroutine *c = nullptr);
    absl::Status Write(std::shared_ptr<T> p, const co::Coroutine *c = nullptr);
};
```

#### Example

```cpp
#include "toolbelt/pipe.h"

// Create a pipe
auto pipe_result = toolbelt::Pipe::Create();
if (!pipe_result.ok()) {
    // Handle error
    return;
}
toolbelt::Pipe pipe = *pipe_result;

// Set non-blocking mode
pipe.SetNonBlocking(true, true);

// Write data (in a coroutine or thread)
char data[] = "Hello, World!";
auto write_result = pipe.Write(data, strlen(data), coroutine);

// Read data (in another coroutine or thread)
char buffer[1024];
auto read_result = pipe.Read(buffer, sizeof(buffer), coroutine);
if (read_result.ok()) {
    size_t bytes_read = *read_result;
    // Process data...
}

// SharedPtrPipe for sending shared_ptr objects (same process only)
auto shared_pipe = toolbelt::SharedPtrPipe<MyClass>::Create();
if (shared_pipe.ok()) {
    auto obj = std::make_shared<MyClass>();
    shared_pipe->Write(obj, coroutine);
    
    auto received = shared_pipe->Read(coroutine);
    if (received.ok()) {
        std::shared_ptr<MyClass> obj = *received;
        // Use object...
    }
}
```

### Table

A table formatter for displaying tabular data with colors and sorting.

**Header:** `toolbelt/table.h`

#### API

```cpp
class Table {
public:
    struct Cell {
        std::string data;
        color::Color color;
    };
    
    Table(const std::vector<std::string> titles, ssize_t sort_column = 0,
          std::function<bool(const std::string &, const std::string &)> comp = nullptr);
    
    void AddRow(const std::vector<std::string> cells);
    void AddRow(const std::vector<std::string> cells, color::Color color);
    void AddRowWithColors(const std::vector<Cell> cells);
    void AddRow();
    void SetCell(size_t col, Cell &&cell);
    void Print(int width, std::ostream &os);
    void Clear();
    void SortBy(size_t column,
                 std::function<bool(const std::string &, const std::string &)> comp);
    void SortBy(size_t column);
    
    static Cell MakeCell(std::string data, color::Color color = {...});
};
```

#### Example

```cpp
#include "toolbelt/table.h"
#include <iostream>

// Create table with column titles
toolbelt::Table table({"Name", "Age", "City"}, 0);  // Sort by first column

// Add rows
table.AddRow({"Alice", "30", "New York"});
table.AddRow({"Bob", "25", "London"});
table.AddRow({"Charlie", "35", "Paris"});

// Add row with color
auto red = toolbelt::color::Color{.mod = toolbelt::color::kRed};
table.AddRow({"David", "40", "Tokyo"}, red);

// Add row with per-cell colors
std::vector<toolbelt::Table::Cell> cells = {
    toolbelt::Table::MakeCell("Eve", toolbelt::color::Color{.mod = toolbelt::color::kGreen}),
    toolbelt::Table::MakeCell("28"),
    toolbelt::Table::MakeCell("Berlin")
};
table.AddRowWithColors(cells);

// Sort by age column (column 1)
table.SortBy(1, [](const std::string &a, const std::string &b) {
    return std::stoi(a) < std::stoi(b);
});

// Print table
table.Print(80, std::cout);
```

### PayloadBuffer

A memory buffer with built-in allocation and deallocation. Useful for message serialization and wire protocols. Supports both fixed-size and resizable buffers, with optional bitmap-based small block allocator.

**Header:** `toolbelt/payload_buffer.h`

#### Key Types

```cpp
using BufferOffset = uint32_t;

struct PayloadBuffer {
    // Constructors
    PayloadBuffer(uint32_t size, bool bitmap_allocator = true);
    PayloadBuffer(uint32_t initial_size, Resizer r, bool bitmap_allocator = true);
    
    // Allocation
    static void *Allocate(PayloadBuffer **buffer, uint32_t n,
                         bool clear = true, bool enable_small_block = true);
    void Free(void *p);
    static void *Realloc(PayloadBuffer **buffer, void *p, uint32_t n,
                        bool clear = true, bool enable_small_block = true);
    
    // String operations
    static char *SetString(PayloadBuffer **self, const char *s, size_t len,
                          BufferOffset header_offset);
    std::string GetString(BufferOffset header_offset) const;
    std::string_view GetStringView(BufferOffset header_offset) const;
    
    // Vector operations
    template <typename T>
    static void VectorPush(PayloadBuffer **self, VectorHeader *hdr, T v,
                          bool enable_small_block = true);
    template <typename T>
    static void VectorReserve(PayloadBuffer **self, VectorHeader *hdr, size_t n,
                             bool enable_small_block = true);
    template <typename T>
    T VectorGet(const VectorHeader *hdr, size_t index) const;
    
    // Address conversion
    template <typename T = void>
    T *ToAddress(BufferOffset offset, size_t size = 0);
    template <typename T = void>
    BufferOffset ToOffset(T *addr, size_t size = 0);
    
    // Utilities
    size_t Size() const;
    void Dump(std::ostream &os);
    bool IsValidMagic() const;
    bool IsMoveable() const;
};
```

#### Example

```cpp
#include "toolbelt/payload_buffer.h"

// Create a fixed-size buffer
char buffer_memory[4096];
toolbelt::PayloadBuffer *pb = new (buffer_memory) toolbelt::PayloadBuffer(4096);

// Allocate memory in the buffer
void *data = toolbelt::PayloadBuffer::Allocate(&pb, 100);
// Use data...

// Free memory
pb->Free(data);

// Create a resizable buffer
auto resizer = [](toolbelt::PayloadBuffer **b, size_t old_size, size_t new_size) {
    char *new_mem = new char[new_size];
    memcpy(new_mem, *b, old_size);
    delete[] reinterpret_cast<char*>(*b);
    *b = reinterpret_cast<toolbelt::PayloadBuffer*>(new_mem);
};
toolbelt::PayloadBuffer *resizable = new toolbelt::PayloadBuffer(1024, resizer);

// Allocate string
toolbelt::BufferOffset str_offset = 100;
toolbelt::PayloadBuffer::SetString(&resizable, "Hello", 5, str_offset);
std::string str = resizable->GetString(str_offset);

// Vector operations
toolbelt::VectorHeader vec_hdr = {0, 0};
toolbelt::PayloadBuffer::VectorPush<int>(&resizable, &vec_hdr, 42);
toolbelt::PayloadBuffer::VectorPush<int>(&resizable, &vec_hdr, 100);
int value = resizable->VectorGet<int>(&vec_hdr, 0);  // Returns 42
```

### TriggerFd

A file descriptor that can be used to trigger events. Implemented as an `eventfd` on Linux or a pipe on other systems.

**Header:** `toolbelt/triggerfd.h`

#### API

```cpp
class TriggerFd {
public:
    TriggerFd();
    TriggerFd(const FileDescriptor &poll_fd, const FileDescriptor &trigger_fd);
    
    absl::Status Open();
    static absl::StatusOr<TriggerFd> Create();
    static absl::StatusOr<TriggerFd> Create(const FileDescriptor &poll_fd,
                                           const FileDescriptor &trigger_fd);
    
    void Close();
    void SetPollFd(FileDescriptor fd);
    void SetTriggerFd(FileDescriptor fd);
    void Trigger();
    bool Clear();  // Clears trigger and returns true if it was triggered
    FileDescriptor &GetPollFd();
    FileDescriptor &GetTriggerFd();
    void AddPollFd(std::vector<struct pollfd> &fds);
};
```

#### Example

```cpp
#include "toolbelt/triggerfd.h"
#include <sys/poll.h>

// Create a trigger file descriptor
auto trigger_result = toolbelt::TriggerFd::Create();
if (!trigger_result.ok()) {
    return;
}
toolbelt::TriggerFd trigger = *trigger_result;

// Add to poll set
std::vector<struct pollfd> fds;
trigger.AddPollFd(fds);

// In event loop
int result = poll(fds.data(), fds.size(), -1);
if (result > 0 && (fds[0].revents & POLLIN)) {
    if (trigger.Clear()) {
        // Trigger was set, handle event
    }
}

// Trigger from another thread/coroutine
trigger.Trigger();
```

## Utility Functions

### Now

Get the current monotonic time in nanoseconds.

**Header:** `toolbelt/clock.h`

```cpp
uint64_t Now();
```

#### Example

```cpp
#include "toolbelt/clock.h"

uint64_t start = toolbelt::Now();
// Do work...
uint64_t end = toolbelt::Now();
uint64_t elapsed_ns = end - start;
double elapsed_ms = elapsed_ns / 1e6;
```

### Hexdump

Dump memory in hexadecimal format.

**Header:** `toolbelt/hexdump.h`

```cpp
void Hexdump(const void* addr, size_t length, FILE* out = stdout);
```

#### Example

```cpp
#include "toolbelt/hexdump.h"

char buffer[64] = "Hello, World!";
toolbelt::Hexdump(buffer, sizeof(buffer));
// Output:
// 00000000  48 65 6c 6c 6f 2c 20 57  6f 72 6c 64 21 00 00 00  |Hello, World!...|
// ...
```

### PrintCurrentStack

Print the current stack trace.

**Header:** `toolbelt/stacktrace.h`

```cpp
void PrintCurrentStack(std::ostream &os);
```

#### Example

```cpp
#include "toolbelt/stacktrace.h"
#include <iostream>

void some_function() {
    // Print stack trace
    toolbelt::PrintCurrentStack(std::cerr);
}
```

## Dependencies

- [Abseil](https://github.com/abseil/abseil-cpp) - Status types, strings, formatting
- [co](https://github.com/dallison/co) - Coroutine library (for socket classes)

## License

See LICENSE file for licensing information.
