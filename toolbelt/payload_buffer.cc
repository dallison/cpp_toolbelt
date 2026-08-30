#include "toolbelt/payload_buffer.h"
#include <assert.h>
#include <cstring>
#include <vector>

namespace toolbelt {
using payload_buffer_detail::FitsInU32;
using payload_buffer_detail::ToU32;

static constexpr struct BitmapRunInfo {
  int num;
  uint32_t size;
} bitmp_run_infos[kNumBitmapRuns] = {
    {kRunSize1, static_cast<uint32_t>(kBitmapRunSize1)},
    {kRunSize2, static_cast<uint32_t>(kBitmapRunSize2)},
    {kRunSize3, static_cast<uint32_t>(kBitmapRunSize3)},
    {kRunSize4, static_cast<uint32_t>(kBitmapRunSize4)},
};

inline int BitmapRunIndex(uint32_t n) {
  for (size_t i = 0; i < kNumBitmapRuns; i++) {
    if (n <= bitmp_run_infos[i].size) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

inline int BitmapRunIndexFromEncodedSize(uint32_t n) {
  if ((n & (1U << 31)) == 0) {
    // Not a small block since the high bit is not set.
    return -1;
  }
  n &= kBitmapRunSizeMask;
  for (size_t i = 0; i < kNumBitmapRuns; i++) {
    if (n <= bitmp_run_infos[i].size) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void *PayloadBuffer::AllocateMainMessage(PayloadBuffer **self, size_t size) {
  if (!FitsInU32(size)) {
    return nullptr;
  }
  void *msg = Allocate(self, ToU32(size), 8, true);
  (*self)->message = (*self)->ToOffset(msg);
  return msg;
}

void PayloadBuffer::AllocateMetadata(PayloadBuffer **self, void *md,
                                     size_t size) {
  if (!FitsInU32(size)) {
    return;
  }
  void *m = Allocate(self, ToU32(size), 1, false);
  memcpy(m, md, size);
  (*self)->metadata = (*self)->ToOffset(m);
}

char *PayloadBuffer::SetString(PayloadBuffer **self, const char *s, size_t len,
                               BufferOffset header_offset) {
  // Get address of the string header
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  if (hdr == nullptr) {
    return nullptr;
  }
  void *str = nullptr;
  if (len > payload_buffer_detail::kMaxU32 - sizeof(uint32_t)) {
    return nullptr;
  }
  const size_t allocation_size = len + sizeof(uint32_t);
  const uint32_t allocation_bytes = ToU32(allocation_size);

  // Load the pointer and convert to address.
  BufferOffset str_ptr = *hdr;
  void *old_str = (*self)->ToAddress(str_ptr);

  // If this contains a valid (non-zero) offset, reallocate the
  // data it points to, otherwise allocate new data.
  if (old_str != nullptr) {
    str = Realloc(self, old_str, allocation_bytes, 4, false);
  } else {
    str = Allocate(self, allocation_bytes, 4, false);
  }
  if (str == nullptr) {
    return nullptr;
  }
  uint32_t *p = reinterpret_cast<uint32_t *>(str);
  p[0] = ToU32(len);
  memcpy(p + 1, s, len);

  // The buffer may have moved.  Reassign the address of the string
  // back into the header.
  BufferOffset *oldp = (*self)->ToAddress<BufferOffset>(header_offset);
  if (oldp == nullptr) {
    (*self)->Free(str);
    return nullptr;
  }
  *oldp = (*self)->ToOffset(str);
  return reinterpret_cast<char *>(str);
}

void PayloadBuffer::ClearString(PayloadBuffer **self,
                                BufferOffset header_offset) {
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  if (hdr == nullptr) {
    return;
  }
  if (*hdr != 0) {
    (*self)->Free((*self)->ToAddress(*hdr));
    // Free doesn't move the buffer so the address is still valid.
    *hdr = 0;
  }
}

// 'addr' is the address of the pointer to the string data.
std::string PayloadBuffer::GetString(const StringHeader *addr) const {
  if (addr == nullptr) {
    return "";
  }
  const uint32_t *p = ToAddress<uint32_t>(*addr);
  if (p == nullptr || (*p > 0 && !IsValidAddress(p + 1, *p))) {
    return "";
  }
  return std::string(reinterpret_cast<const char *>(p + 1), *p);
}

std::string_view PayloadBuffer::GetStringView(const StringHeader *addr) const {
  if (addr == nullptr) {
    return "";
  }
  const uint32_t *p = ToAddress<uint32_t>(*addr);
  if (p == nullptr || (*p > 0 && !IsValidAddress(p + 1, *p))) {
    return "";
  }
  return std::string_view(reinterpret_cast<const char *>(p + 1), *p);
}

size_t PayloadBuffer::StringSize(const StringHeader *addr) const {
  if (addr == nullptr) {
    return 0;
  }
  const uint32_t *p = ToAddress<uint32_t>(*addr);
  if (p == nullptr || (*p > 0 && !IsValidAddress(p + 1, *p))) {
    return 0;
  }
  return size_t(*p);
}

const char *PayloadBuffer::StringData(const StringHeader *addr) const {
  if (addr == nullptr) {
    return nullptr;
  }
  const uint32_t *p = ToAddress<uint32_t>(*addr);
  if (p == nullptr || (*p > 0 && !IsValidAddress(p + 1, *p))) {
    return nullptr;
  }
  return reinterpret_cast<const char *>(p + 1);
}

bool PayloadBuffer::StringWithinBounds(const StringHeader *addr) const {
  if (addr == nullptr) {
    return false;
  }
  // An unset string has a zero body offset and serializes as empty.
  if (*addr == 0) {
    return true;
  }
  const uint32_t *p = ToAddress<uint32_t>(*addr);
  return p != nullptr && (*p == 0 || IsValidAddress(p + 1, *p));
}

absl::Span<char> PayloadBuffer::AllocateString(PayloadBuffer **self, size_t len,
                                               BufferOffset header_offset,
                                               bool clear) {
  // Get address of the string header
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  if (hdr == nullptr) {
    return {};
  }
  void *str = nullptr;
  if (len > payload_buffer_detail::kMaxU32 - sizeof(uint32_t)) {
    return {};
  }
  const size_t allocation_size = len + sizeof(uint32_t);
  const uint32_t allocation_bytes = ToU32(allocation_size);

  // Load the pointer and convert to address.
  BufferOffset str_ptr = *hdr;
  void *old_str = (*self)->ToAddress(str_ptr);

  // If this contains a valid (non-zero) offset, reallocate the
  // data it points to, otherwise allocate new data.
  if (old_str != nullptr) {
    str = Realloc(self, old_str, allocation_bytes, 4, clear);
  } else {
    str = Allocate(self, allocation_bytes, 4, clear);
  }
  if (str == nullptr) {
    return {};
  }
  uint32_t *p = reinterpret_cast<uint32_t *>(str);
  p[0] = ToU32(len);

  // The buffer may have moved.  Reassign the address of the string
  // back into the header.
  BufferOffset *oldp = (*self)->ToAddress<BufferOffset>(header_offset);
  if (oldp == nullptr) {
    (*self)->Free(str);
    return {};
  }
  *oldp = (*self)->ToOffset(str);
  // The span returned is the string data, not the address of the length.
  return absl::Span<char>(reinterpret_cast<char *>(str) + 4, len);
}

void PayloadBuffer::Dump(std::ostream &os) {
  os << "PayloadBuffer: " << this << std::endl;
  os << "  magic: "
     << (!IsValidMagic() ? "invalid" : (IsMoveable() ? "moveable" : "fixed"))
     << std::endl;
  os << "  bitmaps: " << ((magic & kBitMapFlag) ? "enabled" : "disabled")
     << std::endl;
  os << "  hwm: " << hwm << " " << ToAddress(hwm) << std::endl;
  os << "  full_size: " << full_size << std::endl;
  os << "  metadata: " << metadata << " " << ToAddress(metadata) << std::endl;
  os << "  free_list: " << free_list << " " << ToAddress(free_list)
     << std::endl;
  os << "  message: " << message << " " << ToAddress(message) << std::endl;
  for (size_t i = 0; i < kNumBitmapRuns; i++) {
    os << "  bitmaps[" << i << "]: " << bitmaps[i] << " "
       << ToAddress(bitmaps[i]) << std::endl;
  }
  DumpFreeList(os);
}

void PayloadBuffer::DumpFreeList(std::ostream &os) {
  FreeBlockHeader *block = ToAddress<FreeBlockHeader>(free_list);
  while (block != nullptr) {
    os << "Free block @" << block << ": length: " << block->length << " 0x"
       << std::hex << block->length << std::dec;
    os << ", next: " << block->next << " " << ToAddress(block->next)
       << std::endl;
    block = ToAddress<FreeBlockHeader>(block->next);
  }
}

void PayloadBuffer::CheckFreeList() {
  FreeBlockHeader *block = ToAddress<FreeBlockHeader>(free_list);
  while (block != nullptr) {
    if (block->length == 0) {
      std::cerr << "Zero length free block @" << block << std::endl;
      abort();
    }
    block = ToAddress<FreeBlockHeader>(block->next);
  }
}

void PayloadBuffer::InitFreeList() {
  char *end_of_header = reinterpret_cast<char *>(this + 1);
  size_t header_size = sizeof(PayloadBuffer);
  if (IsMoveable()) {
    end_of_header +=
        sizeof(Resizer *); // Room for resizer function for movable buffers.
    header_size += sizeof(Resizer *);
  }
  FreeBlockHeader *f = reinterpret_cast<FreeBlockHeader *>(end_of_header);
  assert(header_size <= full_size);
  f->length = full_size - ToU32(header_size);
  f->next = 0;
  free_list = ToOffset(f);
  hwm = free_list;
}

uint32_t PayloadBuffer::TakeStartOfFreeBlock(FreeBlockHeader *block,
                                             uint32_t num_bytes,
                                             uint32_t length,
                                             FreeBlockHeader *prev) {
  uint32_t rem = block->length - length;
  if (rem >= sizeof(FreeBlockHeader)) {
    FreeBlockHeader *next = reinterpret_cast<FreeBlockHeader *>(
        reinterpret_cast<char *>(block) + length);
    next->length = rem;
    next->next = block->next;
    // Remove from free list.
    if (prev == nullptr) {
      // No previous free block, this becomes the first in the list.
      free_list = ToOffset(next);
    } else {
      // Chain to previous free block.
      prev->next = ToOffset(next);
    }
    UpdateHWM(next + 1);
  } else {
    // We have less than sizeof(FreeBlockHeader)
    // Take whole block.
    if (prev == nullptr) {
      free_list = block->next;
    } else {
      prev->next = block->next;
    }
    // Allocate whole block.
    num_bytes = block->length - sizeof(uint64_t);
    UpdateHWM(block->next + sizeof(FreeBlockHeader));
  }
  return num_bytes;
}

inline uint32_t AlignSize(uint32_t s,
                          uint32_t alignment = uint32_t(sizeof(uint64_t))) {
  return (s + (alignment - 1)) & ~(alignment - 1);
}

void *PayloadBuffer::Allocate(PayloadBuffer **buffer, uint32_t n,
                              bool clear,
                              bool enable_small_block) {
  if (n == 0) {
    return nullptr;
  }
  if (enable_small_block && (*buffer)->BitmapsEnabled()) {
    int small_block_index = BitmapRunIndex(n);
    if (small_block_index >= 0) {
      return AllocateSmallBlock(buffer, n, small_block_index, clear);
    }
  }
  if (n > payload_buffer_detail::kMaxU32 - 7U) {
    return nullptr;
  }
  n = AlignSize(n, 8); // Aligned.
  const size_t full_length = n + sizeof(uint64_t);
  if (!FitsInU32(full_length)) {
    return nullptr;
  }
  const uint32_t full_length_u32 = ToU32(full_length);
  FreeBlockHeader *free_block = (*buffer)->FreeList();
  FreeBlockHeader *prev = nullptr;
  for (;;) {
    if (free_block == nullptr) {
      // Out of memory.  If we have a resizer we can reallocate the buffer.
      Resizer *resizer = (*buffer)->GetResizer();
      if (resizer == nullptr) {
        // Really out of memory.
        return nullptr;
      }
      const size_t old_size = (*buffer)->full_size;
      if (old_size >= payload_buffer_detail::kMaxU32) {
        return nullptr;
      }
      size_t new_size =
          old_size > payload_buffer_detail::kMaxU32 / 2
              ? payload_buffer_detail::kMaxU32
              : old_size * 2;
      while (new_size < full_length) {
        new_size = new_size > payload_buffer_detail::kMaxU32 / 2
                       ? payload_buffer_detail::kMaxU32
                       : new_size * 2;
      }

      // Call the resizer.  This will move *buffer.
      (*resizer)(buffer, old_size, new_size);

      // Set the new size in the newly allocated bigger buffer.
      (*buffer)->full_size = ToU32(new_size);

      // OK, so now we have to find the end of the free list in the new block.
      // The old pointers refer to the deallocated memory.
      free_block = (*buffer)->FreeList();
      prev = nullptr;
      while (free_block != nullptr) {
        prev = free_block;
        free_block = (*buffer)->ToAddress<FreeBlockHeader>(free_block->next);
      }

      // 'prev' is either nullptr, which means we had no free list, or points
      // to the last free block header in the new buffer.
      // Expand the free list to include the new memory.
      char *start_of_new_memory =
          reinterpret_cast<char *>(*buffer) + static_cast<ptrdiff_t>(old_size);
      bool free_list_expanded = false;
      if (prev != nullptr) {
        char *end_of_free_list = reinterpret_cast<char *>(prev) + prev->length;
        if (start_of_new_memory == end_of_free_list) {
          // Last free block is right at the end of the memory, so edxpand it
          // include the new memory.  This is likely to be true.
          prev->length += ToU32(new_size - old_size);
          free_list_expanded = true;
        }
      }

      if (!free_list_expanded) {
        // Need to add a new free block to the end of the free list.
        FreeBlockHeader *new_block =
            reinterpret_cast<FreeBlockHeader *>(start_of_new_memory);
        new_block->next = 0;
        new_block->length = ToU32(new_size - old_size);
        if (prev == nullptr) {
          (*buffer)->free_list = (*buffer)->ToOffset(new_block);
        } else {
          prev->next = (*buffer)->ToOffset(new_block);
        }
      }
      return Allocate(buffer, n, clear);
    }
    if (free_block->length >= full_length_u32) {
      // Free block is big enough.  If there's enough room for the free block
      // header, take the lower part of the free block and keep the remainder
      // in the free list.
      n = (*buffer)->TakeStartOfFreeBlock(free_block, n, full_length_u32, prev);
      const uint64_t block_size = n;
      std::memcpy(free_block, &block_size, sizeof(block_size));
      void *addr =
          reinterpret_cast<void *>(reinterpret_cast<char *>(free_block) +
                                   sizeof(uint64_t));
      if (clear) {
        memset(addr, 0, full_length_u32 - sizeof(uint64_t));
      }
      return addr;
    }
    prev = free_block;
    free_block = (*buffer)->ToAddress<FreeBlockHeader>(free_block->next);
  }
}

std::vector<void *> PayloadBuffer::AllocateMany(PayloadBuffer **buffer,
                                                uint32_t size, uint32_t n,
                                                bool clear) {
  // Calculate space for the whole block.  This is n*aligned(size) +
  // n*sizeof(uint32_t).
  if (size > payload_buffer_detail::kMaxU32 - 7U) {
    return {};
  }
  const size_t item_length = AlignSize(size, 8) + sizeof(uint64_t);
  if (!payload_buffer_detail::ByteCountFitsInU32(n, item_length)) {
    return {};
  }
  const size_t full_length = n * item_length;
  if (!FitsInU32(full_length)) {
    return {};
  }
  void *start = Allocate(buffer, ToU32(full_length), 8, clear);
  if (start == nullptr) {
    return {}; // No memory.
  }
  if (clear) {
    memset(start, 0, full_length);
  }
  // Divide the block into freeable chunks, each of which is align(size) bytes
  // long.  The length of each block is stored immediately before the block.
  std::vector<void *> blocks;
  uint64_t *p = reinterpret_cast<uint64_t *>(start);
  for (uint32_t i = 0; i < n; i++) {
    uint64_t len = AlignSize(size, 8);
    p[0] = len;
    blocks.push_back(reinterpret_cast<void *>(p + 1));
    p = reinterpret_cast<uint64_t *>(reinterpret_cast<char *>(p) + len +
                                     sizeof(uint64_t));
  }
  return blocks;
}

void PayloadBuffer::MergeWithAboveIfPossible(FreeBlockHeader *alloc_block,
                                             FreeBlockHeader *alloc_header,
                                             FreeBlockHeader *free_block,
                                             BufferOffset *next_ptr,
                                             size_t alloc_length) {
  const uintptr_t alloc_addr =
      reinterpret_cast<uintptr_t>(alloc_block);
  const uintptr_t free_addr = reinterpret_cast<uintptr_t>(free_block);

  if (alloc_addr + alloc_length == free_addr) {
    // Merge with block above.
    alloc_header->next = free_block->next;
    const size_t merged_length =
        alloc_length + sizeof(uint64_t) + free_block->length;
    alloc_header->length = ToU32(merged_length);
    *next_ptr = ToOffset(alloc_header);
  } else {
    // Not adjacent to above; add to free list.
    // t points to the allocated block header which has its length set.
    alloc_header->length += sizeof(uint64_t);
    alloc_header->next = ToOffset(free_block);
    *next_ptr = ToOffset(alloc_header);
  }
}

static bool MergeWithBelowIfPossible(FreeBlockHeader *free_block,
                                     FreeBlockHeader *prev) {
  const uintptr_t prev_addr = reinterpret_cast<uintptr_t>(prev);
  if (prev_addr + prev->length == reinterpret_cast<uintptr_t>(free_block)) {
    // Lower block is adjacent.
    prev->next = free_block->next;
    prev->length += free_block->length;
    return true;
  }
  return false;
}

void PayloadBuffer::InsertNewFreeBlockAtEnd(FreeBlockHeader *free_block,
                                            FreeBlockHeader *prev,
                                            uint32_t length) {
  free_block->length = length;
  free_block->next = 0;
  if (prev == nullptr) {
    free_list = ToOffset(free_block);
  } else {
    prev->next = ToOffset(free_block);
  }
}

void PayloadBuffer::Free(void *p) {
  if (p == nullptr) {
    return;
  }
  // An allocated block has its length immediately before its address.
  uint64_t alloc_length = 0;
  std::memcpy(&alloc_length,
              reinterpret_cast<const char *>(p) - sizeof(uint64_t),
              sizeof(alloc_length));
  const int small_block_index =
      BitmapsEnabled()
          ? BitmapRunIndexFromEncodedSize(static_cast<uint32_t>(alloc_length))
          : -1;
  if (small_block_index >= 0) {
    const int bitnum = static_cast<int>(
        (alloc_length >> kBitmpRunBitNumShift) & kBitmapRunBitNumMask);
    const int bitmap_index = static_cast<int>(
        (alloc_length >> kBitmapRunBitMapShift) & kBitmapRunBitMapMask);

    FreeSmallBlock(this, small_block_index, bitmap_index, bitnum);
    return;
  }
  // Point to real start of allocated block.
  FreeBlockHeader *alloc_header = reinterpret_cast<FreeBlockHeader *>(
      reinterpret_cast<char *>(p) - sizeof(uint64_t));

  // Insert into free list by searching for the appropriate point in memory
  // sorted by address.
  FreeBlockHeader *free_block = FreeList();
  if (free_block == nullptr) {
    // No free list, this block becomes the only block.
    alloc_header->length =
        ToU32(alloc_length + static_cast<uint64_t>(sizeof(uint64_t)));
    alloc_header->next = 0;
    free_list = ToOffset(alloc_header);
    return;
  }
  FreeBlockHeader *prev = nullptr;
  while (free_block != nullptr) {
    BufferOffset *next_ptr;
    if (prev == nullptr) {
      next_ptr = &free_list;
    } else {
      next_ptr = &prev->next;
    }
    // If the current block (b) is at a higher address than t then we know
    // that we need to insert t before b.
    if (free_block > alloc_header) {
      // Found a free block after the one being freed.
      MergeWithAboveIfPossible(reinterpret_cast<FreeBlockHeader *>(p),
                               alloc_header, free_block, next_ptr,
                               alloc_length);

      // See if we can merge with prev.  If the block just freed is
      // immediately contiguous with the previous free block,
      // we can merge them,
      if (prev != nullptr) {
        MergeWithBelowIfPossible(alloc_header, prev);
      }
      // We're done.
      return;
    }
    // Look at the next free block, keeping track of the previous.
    prev = free_block;
    free_block = ToAddress<FreeBlockHeader>(free_block->next);
  }
  // We reached the end of the free list, insert free block at end.
  if (prev != nullptr) {
    if (MergeWithBelowIfPossible(alloc_header, prev)) {
      return;
    }
  }
  // Can't merge, insert a new free block at end.
  InsertNewFreeBlockAtEnd(alloc_header, prev,
                          alloc_header->length + sizeof(uint64_t));
}

void PayloadBuffer::ShrinkBlock(FreeBlockHeader *alloc_block,
                                uint32_t orig_length, uint32_t new_length,
                                uint64_t *len_ptr) {
  assert(new_length < orig_length);
  size_t rem = orig_length - new_length;
  if (rem >= sizeof(FreeBlockHeader)) {
    // If we are freeing enough to make a free block, free it, otherwise
    // there's nothing we can do and we just keep the block the same size.
    *len_ptr = new_length; // Change size of block.
    uint64_t *newp = reinterpret_cast<uint64_t *>(
        reinterpret_cast<char *>(alloc_block) + sizeof(uint64_t) + new_length);
    *newp = rem - sizeof(uint64_t); // Add header for free.
    Free(newp + 1);
  }
}

void PayloadBuffer::ExpandIntoFreeBlockAbove(
    FreeBlockHeader *free_block, uint32_t new_length, uint32_t len_diff,
    uint32_t free_remaining, uint64_t *len_ptr, BufferOffset *next_ptr,
    bool clear) {
  assert(free_remaining > sizeof(FreeBlockHeader));
  FreeBlockHeader *next = ToAddress<FreeBlockHeader>(free_block->next);

  // The free block has enough space.
  *len_ptr = new_length;
  FreeBlockHeader *new_block = reinterpret_cast<FreeBlockHeader *>(
      reinterpret_cast<char *>(free_block) + len_diff);
  new_block->length = free_remaining;
  new_block->next = ToOffset(next);
  *next_ptr = ToOffset(new_block);
  UpdateHWM(new_block);
  if (clear) {
    memset(free_block, 0, len_diff);
  }
}

uint64_t *PayloadBuffer::MergeWithFreeBlockBelow(
    void *alloc_block, FreeBlockHeader *prev, FreeBlockHeader *free_block,
    uint32_t new_length, uint32_t orig_length, bool clear) {
  BufferOffset *next_ptr;
  if (prev == nullptr) {
    next_ptr = &free_list;
  } else {
    next_ptr = &prev->next;
  }
  // Move FreeBlockHeader to end of allocated block.  This is inside
  // the combined free block and block being reallocated.
  FreeBlockHeader *next = ToAddress<FreeBlockHeader>(free_block->next);
  FreeBlockHeader *newb = reinterpret_cast<FreeBlockHeader *>(
      reinterpret_cast<char *>(free_block) + new_length + sizeof(uint64_t));
  newb->length = free_block->length + orig_length - new_length;
  newb->next = ToOffset(next);
  *next_ptr = ToOffset(newb);

  uint64_t *len_ptr = reinterpret_cast<uint64_t *>(free_block);
  *len_ptr = new_length;
  memmove(len_ptr + 1, alloc_block, orig_length);
  if (clear) {
    memset(reinterpret_cast<char *>(len_ptr) + sizeof(uint64_t) + orig_length,
           0, new_length - orig_length);
  }
  return len_ptr + 1;
}

void *PayloadBuffer::Realloc(PayloadBuffer **buffer, void *p, uint32_t n,
                             bool clear,
                             bool enable_small_block) {
  if (p == nullptr) {
    // No block to realloc, just call malloc.
    return Allocate(buffer, n, clear);
  }
  // The allocated block has its length immediately prior to its address.
  uint64_t *len_ptr = reinterpret_cast<uint64_t *>(p) - 1;
  const uint64_t orig_length = *len_ptr;
  if (enable_small_block && (*buffer)->BitmapsEnabled()) {
    const int small_block_index =
        BitmapRunIndexFromEncodedSize(static_cast<uint32_t>(orig_length));
    if (small_block_index >= 0) {
      const int decoded_length = static_cast<int>(
          (orig_length >> kBitmapRunSizeShift) & kBitmapRunSizeMask);
      // If the new size is in the same small block index we can just return the
      // original block.
      if (BitmapRunIndex(n) == small_block_index) {
        const int bitnum = static_cast<int>(
            (orig_length >> kBitmpRunBitNumShift) & kBitmapRunBitNumMask);
        const int bitmap_index = static_cast<int>(
            (orig_length >> kBitmapRunBitMapShift) & kBitmapRunBitMapMask);
        const uint64_t encoded_size =
            (1ULL << 31) |
            (static_cast<uint64_t>(bitmap_index) << kBitmapRunBitMapShift) |
            (static_cast<uint64_t>(bitnum) << kBitmpRunBitNumShift) |
            (static_cast<uint64_t>(n) & kBitmapRunSizeMask);

        *len_ptr = encoded_size;
        if (clear && n > static_cast<uint32_t>(decoded_length)) {
          memset(reinterpret_cast<char *>(p) + decoded_length, 0,
                 n - static_cast<uint32_t>(decoded_length));
        }
        return p;
      }
      // Need to free the old block and allocate a new one as the small block
      // index is different.
      const BufferOffset p_offset = (*buffer)->ToOffset(p);
      void *newp = Allocate(buffer, n, false, enable_small_block);
      if (newp == nullptr) {
        return nullptr;
      }
      // Re-derive p since Allocate may have triggered a buffer resize.
      p = (*buffer)->ToAddress(p_offset);
      memcpy(newp, p, static_cast<size_t>(decoded_length));
      if (clear && n > static_cast<uint32_t>(decoded_length)) {
        memset(reinterpret_cast<char *>(newp) + decoded_length, 0,
               n - static_cast<uint32_t>(decoded_length));
      }
      (*buffer)->Free(p);
      return newp;
    }
  }
  FreeBlockHeader *alloc_block = reinterpret_cast<FreeBlockHeader *>(
      reinterpret_cast<char *>(p) - sizeof(uint64_t));
  const uintptr_t alloc_addr = reinterpret_cast<uintptr_t>(p);

  if (n > payload_buffer_detail::kMaxU32 - 7U) {
    return nullptr;
  }
  n = AlignSize(n); // Aligned.
  if (n == orig_length) {
    // Same size as current block, nothing to do.
    return p;
  }
  if (n < orig_length) {
    // Decreasing in size.  Free the remaining part.
    (*buffer)->ShrinkBlock(alloc_block, ToU32(orig_length), n, len_ptr);
    return p;
  }

  // Increasing in size.
  // See if there's a free block immediately following allocated block.
  FreeBlockHeader *free_block = (*buffer)->FreeList();
  FreeBlockHeader *prev = nullptr;
  FreeBlockHeader *prev_prev = nullptr;
  while (free_block != nullptr) {
    BufferOffset *next_ptr;
    if (prev == nullptr) {
      next_ptr = &(*buffer)->free_list;
    } else {
      next_ptr = &prev->next;
    }
    if (free_block > alloc_block) {
      const uintptr_t free_addr = reinterpret_cast<uintptr_t>(free_block);
      const uint32_t diff = n - ToU32(orig_length);
      if (alloc_addr + orig_length == free_addr) {
        // There is a free block above.  See if has enough space.
        if (free_block->length > diff) {
          const uint32_t freelen = free_block->length - diff;
          if (freelen > sizeof(FreeBlockHeader)) {
            (*buffer)->ExpandIntoFreeBlockAbove(free_block, n, diff, freelen,
                                                len_ptr, next_ptr, clear);
            return p;
          }
        }
      }
      // Check for free block adjacent below.
      if (prev != nullptr) {
        const uintptr_t prev_addr = reinterpret_cast<uintptr_t>(prev);
        if (prev_addr + prev->length ==
                reinterpret_cast<uintptr_t>(alloc_block) &&
            prev->length >= diff) {
          // Previous free block is adjacent and has enough space in it.
          // Use start of new block as new address and place FreeBlockHeader
          // at newly free part.
          return (*buffer)->MergeWithFreeBlockBelow(p, prev_prev, prev, n,
                                                    ToU32(orig_length), clear);
        }
        // Block doesn't have enough space.
        break;
      }
    }
    prev_prev = prev;
    prev = free_block;
    free_block = (*buffer)->ToAddress<FreeBlockHeader>(free_block->next);
  }

  // If we get here we can't reuse the existing block.  We allocate a new
  // one, copy the memory and free the old block.  We are guaranteed that
  // the new block is larger than the original one since if it was smaller
  // we can always reuse the block.
  const BufferOffset p_offset = (*buffer)->ToOffset(p);
  void *newp = Allocate(buffer, n, false, enable_small_block);
  if (newp == nullptr) {
    return nullptr;
  }
  // Re-derive p since Allocate may have triggered a buffer resize.
  p = (*buffer)->ToAddress(p_offset);
  memcpy(newp, p, static_cast<size_t>(orig_length));
  if (clear) {
    memset(reinterpret_cast<char *>(newp) + orig_length, 0,
           n - ToU32(orig_length));
  }
  (*buffer)->Free(p);
  return newp;
}

static bool InitializeBitMapRunVector(PayloadBuffer **self, int index,
                                      uint32_t size, uint32_t num) {
  BufferOffset offset = (*self)->AllocateBitMapRunVector(self);
  if (offset == 0) {
    return false;
  }
  auto free_bitmap_vector = [self, offset]() {
    VectorHeader *hdr = (*self)->ToAddress<VectorHeader>(offset);
    PayloadBuffer::VectorClear<BufferOffset>(self, hdr);
    (*self)->Free((*self)->ToAddress<void>(offset));
  };

  BitMapRun *run = PayloadBuffer::AllocateBitMapRun(self, size, num);
  if (run == nullptr) {
    free_bitmap_vector();
    return false;
  }

  // Re-derive hdr since AllocateBitMapRun may have triggered a buffer resize.
  VectorHeader *hdr = (*self)->ToAddress<VectorHeader>(offset);
  BufferOffset run_offset = (*self)->ToOffset(run);
  if (!(*self)->VectorPush<BufferOffset>(self, hdr, run_offset, false)) {
    (*self)->Free((*self)->ToAddress<void>(run_offset));
    free_bitmap_vector();
    return false;
  }
  (*self)->bitmaps[index] = offset;
  return true;
}

bool PayloadBuffer::PrimeBitmapAllocator(PayloadBuffer **self, size_t size) {
  if (!FitsInU32(size)) {
    return false;
  }
  const int index = BitmapRunIndex(ToU32(size));
  if (index < 0) {
    return true;
  }
  if ((*self)->bitmaps[index] != 0) {
    return true;
  }
  return InitializeBitMapRunVector(
      self, index, bitmp_run_infos[index].size,
      static_cast<uint32_t>(bitmp_run_infos[index].num));
}

BufferOffset PayloadBuffer::AllocateBitMapRunVector(PayloadBuffer **self) {
  // Allocate space for the VectorHeader.  Although this is a small block, we
  // can't use the small block allocator because this is initializing it.
  void *hdr = Allocate(self, sizeof(VectorHeader), true, false);
  if (hdr == nullptr) {
    return 0;
  }
  BufferOffset hdr_offset = (*self)->ToOffset(hdr);

  // Preallocate space for 8 elements.
  if (!VectorReserve<BufferOffset>(self, reinterpret_cast<VectorHeader *>(hdr),
                                   8, false)) {
    (*self)->Free((*self)->ToAddress<void>(hdr_offset));
    return 0;
  }
  return hdr_offset;
}

BitMapRun *PayloadBuffer::AllocateBitMapRun(PayloadBuffer **self, uint32_t size,
                                            uint32_t num) {
  // It is important that this isn't a small block as it will infintely recurse.
  // The full size of the BitMapRun contains space for the blocks themselves,
  // each of which is prefixed by a 8 byte length.  The length is encoded
  // with data necessary for freeing it.
  BitMapRun *run = reinterpret_cast<BitMapRun *>(
      Allocate(self, sizeof(BitMapRun) + (size + 8) * num, false, false));
  if (run == nullptr) {
    return nullptr;
  }
  run->size = static_cast<uint8_t>(size);
  run->num = static_cast<uint8_t>(num);
  run->bits = 0;
  run->free = static_cast<uint8_t>(num); // All blocks are free.
  return run;
}

void *BitMapRun::Allocate(PayloadBuffer **pb, int index, uint32_t, int size,
                          int num, bool clear) {
  // Lazy init of vector.
  if ((*pb)->bitmaps[index] == 0) {
    if (!InitializeBitMapRunVector(pb, index, static_cast<uint32_t>(size),
                                   static_cast<uint32_t>(num))) {
      return nullptr;
    }
  }
  for (;;) {
    // Re-derive hdr each iteration since allocations below may trigger a
    // buffer resize (realloc), invalidating any previous pointer.
    VectorHeader *hdr =
        (*pb)->ToAddress<VectorHeader>((*pb)->bitmaps[static_cast<size_t>(index)]);
    // Go backwards through the elements as that is most likely to find a free
    // bit.
    for (int i = static_cast<int>(hdr->num_elements) - 1; i >= 0; i--) {
      BitMapRun *run = (*pb)->ToAddress<BitMapRun>(
          (*pb)->VectorGet<BufferOffset>(hdr, static_cast<size_t>(i)));
      if (run->free == 0) {
        continue;
      }
      // Fast path: there is a free bit in the run.
      int bit = ffs(static_cast<int>(~run->bits));
      assert(bit > 0 && bit <= run->num);
      bit--; // Convert to 0-based index.
      run->bits |= 1U << static_cast<unsigned>(bit);
      run->free--;

      // The address of the block is after the header and indexed by the bit
      // number times the size of the block plus 8 bytes for the length.  Then
      // we need the address after the length word.
      void *addr = reinterpret_cast<char *>(run) + sizeof(BitMapRun) +
                   static_cast<size_t>(bit) * (run->size + 8) + 8;
      // Write the encoded size of the block into the preceding 8 bytes.
      uint64_t *p = reinterpret_cast<uint64_t *>(addr) - 1;
      // Encode the length.
      const uint64_t encoded_size =
          (1ULL << 31) |
          (static_cast<uint64_t>(i) << kBitmapRunBitMapShift) |
          (static_cast<uint64_t>(bit) << kBitmpRunBitNumShift) |
          (static_cast<uint64_t>(size) & kBitmapRunSizeMask);
      *p = encoded_size;
      if (clear) {
        memset(addr, 0, static_cast<size_t>(size));
      }
      return addr;
    }
    // Slow path, no free bits in any run.  We need to allocate a new run.
    BitMapRun *run =
        PayloadBuffer::AllocateBitMapRun(pb, static_cast<uint32_t>(size),
                                         static_cast<uint32_t>(num));
    if (run == nullptr) {
      return nullptr;
    }
    // Re-derive hdr since AllocateBitMapRun may have triggered a buffer
    // resize, invalidating the previous pointer.
    hdr = (*pb)->ToAddress<VectorHeader>((*pb)->bitmaps[static_cast<size_t>(index)]);
    BufferOffset run_offset = (*pb)->ToOffset(run);
    if (!(*pb)->VectorPush<BufferOffset>(pb, hdr, run_offset, false)) {
      (*pb)->Free((*pb)->ToAddress<void>(run_offset));
      return nullptr;
    }
  }
}

void BitMapRun::Free(PayloadBuffer *pb, int index, int bitmap_index,
                     int bitnum) {
  // This is always fast path since we have all the information we need
  // to free the block.  We basically just clear a bit and increment the
  // free count.
  VectorHeader *hdr =
      pb->ToAddress<VectorHeader>(pb->bitmaps[static_cast<size_t>(index)]);
  assert(hdr != nullptr);
  BitMapRun *run = pb->ToAddress<BitMapRun>(
      pb->VectorGet<BufferOffset>(hdr, static_cast<size_t>(bitmap_index)));
  run->bits &= ~(1U << static_cast<unsigned>(bitnum));
  run->free++;
}

void *PayloadBuffer::AllocateSmallBlock(PayloadBuffer **pb, uint32_t size,
                                        int index, bool clear) {
  return BitMapRun::Allocate(
      pb, index, size,
      static_cast<int>(bitmp_run_infos[static_cast<size_t>(index)].size),
      bitmp_run_infos[static_cast<size_t>(index)].num, clear);
}

void PayloadBuffer::FreeSmallBlock(PayloadBuffer *pb, int index,
                                   int bitmap_index, int bitnum) {
  BitMapRun::Free(pb, index, bitmap_index, bitnum);
}
} // namespace toolbelt
