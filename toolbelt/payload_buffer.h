#pragma once

#include "absl/types/span.h"
#include <functional>
#include <iostream>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <string_view>
#include "toolbelt/hexdump.h"

namespace toolbelt {

constexpr uint32_t kFixedBufferMagic = 0xe5f6f1c4;
constexpr uint32_t kMovableBufferMagic = 0xc5f6f1c4;

// Bottom bit is set for enabling bitmap allocator.
constexpr uint32_t kBitMapMask = 0xfffffffe;
constexpr uint32_t kBitMapFlag = 1;

using BufferOffset = uint32_t;

struct FreeBlockHeader {
  uint32_t length;   // Length of the free block, including the header.
  BufferOffset next; // Absolute offset into buffer for next free block.
};

// The 'data' member refers to a block of allocated memory in the
// buffer.  Since it's been allocated using Allocate, the preceding
// 8 bytes contains the length of the block in bytes (little endian).
// To get the count of the number of elements in the allocated block
// (the vector's capacity), divide this by sizeof(T) where T is the
// type of the values stored in the vector.
struct VectorHeader {
  uint32_t num_elements; // Number of populated elements.
  BufferOffset data;     // Absolute offset of vector data.
};

struct PayloadBuffer;

// A string is held in an allocated block as a 4 byte little endian
// length field, followed immediately by the string data.  The block
// length is aligned to a multiple of 4 bytes, so that can't be
// used to hold the actual length of the string.
// A StringHeader is the absolute offset into the buffer of the
// length field of the string.
using StringHeader = BufferOffset;

// The resizer function is called when the buffer needs to be expanded.
// Its responsibility is to:
// 1. Allocate memory for the new buffer with the size given.
// 2. Copy the current buffer contents to the new memory
// 3. Set *buffer to the address of the new buffer.
// 4. Freeing up the old buffer if necessary.
//
// It is not responsible for adding the new memory to the free list in
// the buffer to make it available for allocation.  If the buffer
// is allocated using malloc, the resize would be a simple call
// to realloc.  However, care must be taken to preserve the
// contents of the resizer function.
using Resizer =
    std::function<void(PayloadBuffer **, size_t old_size, size_t new_size)>;

// BitMap allocator.  In order to reduce fragmentation and speed up allocation
// of small blocks, we use a bitmap allocator for a fixed number of small block
// sizes.  Each BitMapRun refers to a run of blocks of the same size.  It
// contains a bitmap with a bit for each block in the run.  The PayloadBuffer
// header contains the offset to a vector header that holds vector of offsets to
// BitMapRun objects.  The index into this vector is stored in the 8 bytes
// preceding an allocated small block to avoid searching during free.

struct BitMapRun {
  uint32_t bits; // Bit per chunk.
  uint8_t size;  // Size of each chunk.
  uint8_t num;   // Number of chunks in run.
  uint8_t free;  // Number of free chunks.
  // The memory for the run is immediately after the BitMapRun.

  // Allocate a chunk from the run.  If we are out of chunks, a new
  // run is allocated and the chunk is taken from that.
  static void *Allocate(PayloadBuffer **pb, int index, uint32_t n, int size,
                        int num, bool clear = true);
  static void Free(PayloadBuffer *pb, int index, int bitmap_index, int bitnum);
};

// Default bitmap allocator runs.
inline constexpr size_t kNumBitmapRuns = 4;

// Run size for each block size.   This is the size of the run, that is, how
// many individual blocks are in a run.  There is a limit of 32 since we use an
// unsigned int for the bitmap.  A whole run is allocated at once so there is
// overhead in the allocation of a run.  Tune this to balance the performance vs
// message size.
inline constexpr int kRunSize1 = 20;
inline constexpr int kRunSize2 = 10;
inline constexpr int kRunSize3 = 6;
inline constexpr int kRunSize4 = 2;

// Block size for each bitmap run.  Each bitmap run allocates blocks of the same
// size in a contiguous block of memory in the buffer.  The size of each block
// in a run is below.
inline constexpr int kBitmapRunSize1 = 16;
inline constexpr int kBitmapRunSize2 = 32;
inline constexpr int kBitmapRunSize3 = 64;
inline constexpr int kBitmapRunSize4 = 128;

// In order to allow free to work without searching, we use the 8 bytes
// preceding the allocated block in the run to store the size of the block, the
// index into the BitMapRun vector and the bit number in the bitmap.  In order
// to distinguish this between small blocks and regular blocks allocated from
// the free list, we use a negative number (top bit set) containing:
// 1. Bit 31 set
// 2. Bits 30-26 (5 bits) contain the bit number into the bitmap.
// 3. Bits 25-8 (18 bits) contain the index into the vector of bitmap runs.
// 3. Bits 7-0 (8 bits) contain the size of the block.

// Shifts for size encoding for small blocks.
inline constexpr int kBitmpRunBitNumShift = 26;
inline constexpr int kBitmapRunBitMapShift = 8;
inline constexpr int kBitmapRunSizeShift = 0;

// Masks
inline constexpr uint32_t kBitmapRunBitNumMask = 0x1f;
inline constexpr uint32_t kBitmapRunBitMapMask = 0x3ffff;
inline constexpr uint32_t kBitmapRunSizeMask = 0xff;

// This is a buffer that holds the contents of a message.
// It is located at the first address of the actual buffer with the
// reset of the buffer memory following it.
//
struct PayloadBuffer {
  uint32_t magic;         // Magic to identify wireformat.
  BufferOffset message;   // Offset for the message.
  uint32_t hwm;           // Offset one beyond the highest used.
  uint32_t full_size;     // Full size of buffer.
  BufferOffset free_list; // Heap free list.
  BufferOffset metadata;  // Offset to message metadata.
  BufferOffset
      bitmaps[kNumBitmapRuns]; // Offset to VectorHeader for BitMapRun offsets.

  // Initialize a new PayloadBuffer at this with a message of the
  // given size.  This is a fixed size buffer.
  PayloadBuffer(uint32_t size, bool bitmap_allocator = true)
      : magic(kFixedBufferMagic | (bitmap_allocator ? kBitMapFlag : 0)),
        message(0), hwm(0), full_size(size), metadata(0) {
    for (int i = 0; i < kNumBitmapRuns; i++) {
      bitmaps[i] = 0;
    }
    InitFreeList();
  }

  // This is a variable sized buffer with the resizer being a function to
  // allocate new memory.  A pointer to the resizer function is stored in the
  // memory immediately above the header.  We can't store the actual
  // std::function in the memory because the memory will be deleted during the
  // call to the std::function which invalidates its this pointer.  Instead we
  // copy the std::function to the heap and store a raw pointer to it.  The heap
  // copy will be destructed when the payload buffer is destructed.
  // This implies that you need to destruct the payload buffer to avoid
  // a memory leak.  This is only necessary for resizable buffers.  Fixed
  // size buffers don't need to be destructed.
  PayloadBuffer(uint32_t initial_size, Resizer r, bool bitmap_allocator = true)
      : magic(kMovableBufferMagic | (bitmap_allocator ? kBitMapFlag : 0)),
        message(0), hwm(0), full_size(initial_size), metadata(0) {
    for (int i = 0; i < kNumBitmapRuns; i++) {
      bitmaps[i] = 0;
    }
    InitFreeList();
    SetResizer(std::move(r));
  }

  ~PayloadBuffer() {
    if (IsMoveable()) {
      // Destruct the resizer.
      Resizer **addr = reinterpret_cast<Resizer **>(this + 1);
      delete *addr;
    }
  }

  bool BitmapsEnabled() const { return (magic & kBitMapFlag) != 0; }

  void SetResizer(Resizer r) {
    // Place a pointer to the resizer function in the buffer just after the
    // header.
    Resizer **addr = reinterpret_cast<Resizer **>(this + 1);
    Resizer *on_heap = new Resizer(std::move(r)); // Moved to heap memory.
    *addr = on_heap;                              // Place address in memory.
  }

  Resizer *GetResizer() {
    if (!IsMoveable()) {
      return nullptr;
    }
    return *reinterpret_cast<Resizer **>(this + 1);
  }

  size_t Size() const { return size_t(hwm); }

  // Allocate space for the main message in the buffer and set the
  // 'message' field to its offset.
  static void *AllocateMainMessage(PayloadBuffer **self, size_t size);

  // Allocate space for the message metadata and copy it in.
  static void AllocateMetadata(PayloadBuffer **self, void *md, size_t size);
  // Prime the bitmap runs for the given size.  This pre-allocates the runs
  // for the size to remove the initial allocation time overhead for the
  // first allocation.  Returns true if successful.
  static bool PrimeBitmapAllocator(PayloadBuffer **self, size_t size);
  void SetPresenceBit(uint32_t bit, uint32_t offset) {
    uint32_t word = bit / 32;
    bit %= 32;
    uint32_t *p = ToAddress<uint32_t>(offset);
    p[word] |= (1 << bit);
  }

  void ClearPresenceBit(uint32_t bit, uint32_t offset) {
    uint32_t word = bit / 32;
    bit %= 32;
    uint32_t *p = ToAddress<uint32_t>(offset);
    p[word] &= ~(1 << bit);
  }

  bool IsPresent(uint32_t bit, uint32_t offset) const {
    uint32_t word = bit / 64;
    bit %= 64;
    const uint32_t *p = ToAddress<const uint32_t>(offset);
    return (p[word] & (1 << bit)) != 0;
  }

  static uint32_t DecodeSize(BufferOffset* addr) {
    // Length is 64 bits long but we only need the bottom 32 bits of it.
    uint32_t *p = reinterpret_cast<uint32_t *>(addr) - 2;
    if ((*p & (1U << 31)) == 0) {
      return *p;
    }
    return *p & kBitmapRunSizeMask;
  }

  template <typename MessageType>
  static MessageType *NewMessage(PayloadBuffer **self, uint32_t size,
                                 BufferOffset offset);

  // Allocate space for the string.
  // 'header_offset' is the offset into the buffer of the StringHeader.
  // The string is copied in.

  // C-string style (allows for no allocation of std::string).
  static char *SetString(PayloadBuffer **self, const char *s, size_t len,
                         BufferOffset header_offset);

  template <typename Str>
  static char *SetString(PayloadBuffer **self, Str s,
                         BufferOffset header_offset) {
    return SetString(self, s.data(), s.size(), header_offset);
  }

  static void ClearString(PayloadBuffer **self, BufferOffset header_offset);

  static absl::Span<char> AllocateString(PayloadBuffer **self, size_t len,
                                         BufferOffset header_offset,
                                         bool clear = false);

  bool IsNull(BufferOffset offset) {
    BufferOffset *p = ToAddress<BufferOffset>(offset);
    return *p == 0;
  }

  template <typename T> void Set(BufferOffset offset, T v);
  template <typename T> T &Get(BufferOffset offset);

  template <typename T>
  static void VectorPush(PayloadBuffer **self, VectorHeader *hdr, T v,
                         bool enable_small_block = true);

  template <typename T>
  static void VectorReserve(PayloadBuffer **self, VectorHeader *hdr, size_t n,
                            bool enable_small_block = true);

  template <typename T>
  static void VectorResize(PayloadBuffer **self, VectorHeader *hdr, size_t n);

  template <typename T>
  static void VectorClear(PayloadBuffer **self, VectorHeader *hdr);

  // 'header_offset' is the offset into the buffer StringHeader.
  std::string GetString(BufferOffset header_offset) const {
    return GetString(ToAddress<const BufferOffset>(header_offset));
  }
  std::string_view GetStringView(BufferOffset header_offset) const {
    return GetStringView(ToAddress<const BufferOffset>(header_offset));
  }
  size_t StringSize(BufferOffset header_offset) const {
    return StringSize(ToAddress<const BufferOffset>(header_offset));
  }

  const char *StringData(BufferOffset header_offset) const {
    return StringData(ToAddress<const BufferOffset>(header_offset));
  }

  std::string GetString(const StringHeader *addr) const;
  std::string_view GetStringView(const StringHeader *addr) const;
  size_t StringSize(const StringHeader *addr) const;
  const char *StringData(const StringHeader *addr) const;

  template <typename T>
  T VectorGet(const VectorHeader *hdr, size_t index) const;

  void Dump(std::ostream &os);
  void DumpFreeList(std::ostream &os);
  void CheckFreeList();

  void InitFreeList();
  FreeBlockHeader *FreeList() { return ToAddress<FreeBlockHeader>(free_list); }

  // Allocate some memory in the buffer.  The buffer might move.
  static void *Allocate(PayloadBuffer **buffer, uint32_t n,
                        bool clear = true, bool enable_small_block = true);
  void Free(void *p);
  static void *Realloc(PayloadBuffer **buffer, void *p, uint32_t n,
                       bool clear = true,
                       bool enable_small_block = true);
  static BufferOffset AllocateBitMapRunVector(PayloadBuffer **self);
  static void *AllocateSmallBlock(PayloadBuffer **pb, uint32_t size, int index,
                                  bool clear = true);
  static void FreeSmallBlock(PayloadBuffer *pb, int index, int bitmap_index,
                             int bitnum);

  // Allocate 'n' items of size 'size' in the buffer.  The buffer might move.
  // Each allocation is capable of being freed
  // individually. The addresses of the allocated items are returned in a
  // vector.
  static std::vector<void *> AllocateMany(PayloadBuffer **buffer, uint32_t size,
                                          uint32_t n,
                                          bool clear = true);

  bool IsValidMagic() const {
    uint32_t m = magic & kBitMapMask;
    return m == kFixedBufferMagic || m == kMovableBufferMagic;
  }
  bool IsMoveable() const {
    return (magic & kBitMapMask) == kMovableBufferMagic;
  }

  bool IsValidAddress(const void *addr, size_t size) const {
    if (size == 0) {
      size = full_size;
    }
    return addr >= reinterpret_cast<const char *>(this) &&
           addr < reinterpret_cast<const char *>(this) + size;
  }

  // Given the address of a block, return the size of the block.  This is
  // in the previous 8 bytes but might be encoded if the block is a small
  // block.
  static uint32_t DecodedSize(BufferOffset* addr) {
      BufferOffset* p = addr - 2;
      if (*p & (1U << 31)) {
          return *p & kBitmapRunSizeMask;
      }
      return *p;
  }

  template <typename T = void>
  T *ToAddress(BufferOffset offset, size_t size = 0) {
    if (offset == 0) {
      return nullptr;
    }
    if (!IsValidMagic()) {
      return nullptr;
    }
    // Validate that we don't go outside the buffer.
    char *addr = reinterpret_cast<char *>(this) + offset;
    if (!IsValidAddress(addr, size)) {
      return nullptr;
    }

    return reinterpret_cast<T *>(addr);
  }

  template <typename T = void> BufferOffset ToOffset(T *addr, size_t size = 0) {
    if (addr == reinterpret_cast<T *>(this) || addr == nullptr) {
      return 0;
    }
    if (!IsValidMagic()) {
      return 0;
    }
    if (!IsValidAddress(addr, size)) {
      return 0;
    }
    return reinterpret_cast<const char *>(addr) -
           reinterpret_cast<const char *>(this);
  }

  template <typename T = void>
  const T *ToAddress(BufferOffset offset, size_t size = 0) const {
    if (offset == 0) {
      return nullptr;
    }
    if (!IsValidMagic()) {
      return nullptr;
    }
    // Validate that we don't go outside the buffer.
    const char *addr = reinterpret_cast<const char *>(this) + offset;
    if (!IsValidAddress(addr, size)) {
      return nullptr;
    }

    return reinterpret_cast<const T *>(reinterpret_cast<const char *>(this) +
                                       offset);
  }

  template <typename T = void>
  BufferOffset ToOffset(const T *addr, size_t size = 0) const {
    if (addr == reinterpret_cast<const T *>(this) || addr == nullptr) {
      return 0;
    }
    if (!IsValidMagic()) {
      return 0;
    }
    if (!IsValidAddress(addr, size)) {
      return 0;
    }
    return reinterpret_cast<const char *>(addr) -
           reinterpret_cast<const char *>(this);
  }

  void InsertNewFreeBlockAtEnd(FreeBlockHeader *free_block,
                               FreeBlockHeader *prev, uint32_t length);

  void MergeWithAboveIfPossible(FreeBlockHeader *alloc_block,
                                FreeBlockHeader *alloc_header,
                                FreeBlockHeader *free_block,
                                BufferOffset *next_ptr, size_t alloc_length);

  void ShrinkBlock(FreeBlockHeader *alloc_block, uint32_t orig_length,
                   uint32_t new_length, uint64_t *len_ptr);

  uint64_t *MergeWithFreeBlockBelow(void *alloc_block, FreeBlockHeader *prev,
                                    FreeBlockHeader *free_block,
                                    uint32_t new_length, uint32_t orig_length,
                                    bool clear);

  void ExpandIntoFreeBlockAbove(FreeBlockHeader *free_block,
                                uint32_t new_length, uint32_t len_diff,
                                uint32_t free_remaining, uint64_t *len_ptr,
                                BufferOffset *next_ptr, bool clear);
  uint32_t TakeStartOfFreeBlock(FreeBlockHeader *block, uint32_t num_bytes,
                                uint32_t full_length, FreeBlockHeader *prev);

  void UpdateHWM(void *p) { UpdateHWM(ToOffset(p)); }

  void UpdateHWM(BufferOffset off) {
    if (off > hwm) {
      hwm = off;
    }
  }
  static BitMapRun *AllocateBitMapRun(PayloadBuffer **self, uint32_t size,
                                      uint32_t num);
};

template <>
inline char *PayloadBuffer::SetString(PayloadBuffer **self, const char *s,
                                      BufferOffset header_offset) {
  return SetString(self, s, strlen(s), header_offset);
}

template <typename T> inline void PayloadBuffer::Set(BufferOffset offset, T v) {
  T *addr = ToAddress<T>(offset);
  *addr = v;
}

template <typename T> inline T &PayloadBuffer::Get(BufferOffset offset) {
  T *addr = ToAddress<T>(offset);
  return *addr;
}

template <typename T>
inline void PayloadBuffer::VectorPush(PayloadBuffer **self, VectorHeader *hdr,
                                      T v, bool enable_small_block) {
  // hdr points to a VectorHeader:
  // uint32_t num_elements;     - number of elements in the vector
  // BufferOffset data;         - BufferOffset to vector contents
  // The vector contents is allocated in the buffer.  It is preceded
  // by the block size (in bytes).
  uint32_t total_size = hdr->num_elements * sizeof(T);
  if (hdr->data == 0) {
    // The vector is empty, allocate it with a default size of 2.
    void *vecp = Allocate(self, 2 * sizeof(T), true, enable_small_block);
    hdr->data = (*self)->ToOffset(vecp);
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    if (current_size == total_size) {
      // Need to double the size of the memory.
      void *vecp = Realloc(self, block, 2 * hdr->num_elements * sizeof(T),
                           true, enable_small_block);
      hdr->data = (*self)->ToOffset(vecp);
    }
  }
  // Get address of next location in vector.
  T *valuep = (*self)->ToAddress<T>(hdr->data) + hdr->num_elements;
  // Assign value.
  *valuep = v;
  // Increment the number of elements.
  hdr->num_elements++;
}

template <typename T>
inline void PayloadBuffer::VectorReserve(PayloadBuffer **self,
                                         VectorHeader *hdr, size_t n,
                                         bool enable_small_block) {
  if (hdr->data == 0) {
    void *vecp = Allocate(self, n * sizeof(T), false, enable_small_block);
    hdr->data = (*self)->ToOffset(vecp);
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    if (current_size < n * sizeof(T)) {
      // Need to expand the memory to the size given.
      void *vecp =
          Realloc(self, block, n * sizeof(T), false, enable_small_block);
      hdr->data = (*self)->ToOffset(vecp);
    }
  }
}

template <typename T>
inline void PayloadBuffer::VectorResize(PayloadBuffer **self, VectorHeader *hdr,
                                        size_t n) {
  if (hdr->data == 0) {
    void *vecp = Allocate(self, n * sizeof(T));
    hdr->data = (*self)->ToOffset(vecp);
  } else {
    // Vector has some values in it.  Retrieve the total size from
    // the allocated block header (before the start of the memory)
    uint32_t *block = (*self)->ToAddress<uint32_t>(hdr->data);
    uint32_t current_size = DecodeSize(block);
    if (current_size < n * sizeof(T)) {
      // Need to expand the memory to the size given.
      void *vecp = Realloc(self, block, n * sizeof(T), 8);
      hdr->data = (*self)->ToOffset(vecp);
    }
  }
  hdr->num_elements = n;
}

template <typename T>
inline void PayloadBuffer::VectorClear(PayloadBuffer **self,
                                       VectorHeader *hdr) {
  if (hdr->data != 0) {
    (*self)->Free((*self)->ToAddress<void>(hdr->data));
  }
  hdr->data = 0;
  hdr->num_elements = 0;
}

template <typename T>
inline T PayloadBuffer::VectorGet(const VectorHeader *hdr, size_t index) const {
  if (index >= hdr->num_elements) {
    return static_cast<T>(0);
  }
  const T *addr = ToAddress<const T>(hdr->data);
  if (addr == nullptr) {
    return static_cast<T>(0);
  }
  return addr[index];
}

template <typename MessageType>
inline MessageType *PayloadBuffer::NewMessage(PayloadBuffer **self,
                                              uint32_t size,
                                              BufferOffset offset) {
  void *msg = Allocate(self, size);
  BufferOffset *p = (*self)->ToAddress<BufferOffset>(offset);
  *p = (*self)->ToOffset(msg);
  return reinterpret_cast<MessageType *>(msg);
}
} // namespace toolbelt
