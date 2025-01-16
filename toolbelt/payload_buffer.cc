#include "toolbelt/payload_buffer.h"
#include <assert.h>
#include <vector>

namespace toolbelt {
static constexpr struct BitmapRunInfo {
  int num;
  int size;
} bitmp_run_infos[kNumBitmapRuns] = {
    {kRunSize1, kBitmapRunSize1},
    {kRunSize2, kBitmapRunSize2},
    {kRunSize3, kBitmapRunSize3},
    {kRunSize4, kBitmapRunSize4},
};

inline int BitmapRunIndex(uint32_t n) {
  for (size_t i = 0; i < kNumBitmapRuns; i++) {
    if (n <= bitmp_run_infos[i].size) {
      return i;
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
      return i;
    }
  }
  return -1;
}

void *PayloadBuffer::AllocateMainMessage(PayloadBuffer **self, size_t size) {
  void *msg = Allocate(self, size, 8, true);
  (*self)->message = (*self)->ToOffset(msg);
  return msg;
}

void PayloadBuffer::AllocateMetadata(PayloadBuffer **self, void *md,
                                     size_t size) {
  void *m = Allocate(self, size, 1, false);
  memcpy(m, md, size);
  (*self)->metadata = (*self)->ToOffset(m);
}

char *PayloadBuffer::SetString(PayloadBuffer **self, const char *s, size_t len,
                               BufferOffset header_offset) {
  // Get address of the string header
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  void *str = nullptr;

  // Load the pointer and convert to address.
  BufferOffset str_ptr = *hdr;
  void *old_str = (*self)->ToAddress(str_ptr);

  // If this contains a valid (non-zero) offset, reallocate the
  // data it points to, otherwise allocate new data.
  if (old_str != nullptr) {
    str = Realloc(self, old_str, len + 4, 4, false);
  } else {
    str = Allocate(self, len + 4, 4, false);
  }
  uint32_t *p = reinterpret_cast<uint32_t *>(str);
  p[0] = uint32_t(len);
  memcpy(p + 1, s, len);

  // The buffer may have moved.  Reassign the address of the string
  // back into the header.
  BufferOffset *oldp = (*self)->ToAddress<BufferOffset>(header_offset);
  *oldp = (*self)->ToOffset(str);
  return reinterpret_cast<char *>(str);
}

void PayloadBuffer::ClearString(PayloadBuffer **self,
                                BufferOffset header_offset) {
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  if (*hdr != 0) {
    (*self)->Free((*self)->ToAddress(*hdr));
    // Free doesn't move the buffer so the address is still valid.
    *hdr = 0;
  }
}

// 'addr' is the address of the pointer to the string data.
std::string PayloadBuffer::GetString(const StringHeader *addr) const {
  const uint32_t *p = reinterpret_cast<const uint32_t *>(ToAddress(*addr));
  if (p == nullptr) {
    return "";
  }
  return std::string(reinterpret_cast<const char *>(p + 1), *p);
}

std::string_view PayloadBuffer::GetStringView(const StringHeader *addr) const {
  const uint32_t *p = reinterpret_cast<const uint32_t *>(ToAddress(*addr));
  if (p == nullptr) {
    return "";
  }
  return std::string_view(reinterpret_cast<const char *>(p + 1), *p);
}

size_t PayloadBuffer::StringSize(const StringHeader *addr) const {
  const uint32_t *p = reinterpret_cast<const uint32_t *>(ToAddress(*addr));
  if (p == nullptr) {
    return 0;
  }
  return size_t(*p);
}

const char *PayloadBuffer::StringData(const StringHeader *addr) const {
  const uint32_t *p = reinterpret_cast<const uint32_t *>(ToAddress(*addr));
  if (p == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<const char *>(p + 1);
}

absl::Span<char> PayloadBuffer::AllocateString(PayloadBuffer **self, size_t len,
                                               BufferOffset header_offset,
                                               bool clear) {
  // Get address of the string header
  BufferOffset *hdr = (*self)->ToAddress<BufferOffset>(header_offset);
  void *str = nullptr;

  // Load the pointer and convert to address.
  BufferOffset str_ptr = *hdr;
  void *old_str = (*self)->ToAddress(str_ptr);

  // If this contains a valid (non-zero) offset, reallocate the
  // data it points to, otherwise allocate new data.
  if (old_str != nullptr) {
    str = Realloc(self, old_str, len + 4, 4, clear);
  } else {
    str = Allocate(self, len + 4, 4, clear);
  }
  uint32_t *p = reinterpret_cast<uint32_t *>(str);
  p[0] = uint32_t(len);

  // The buffer may have moved.  Reassign the address of the string
  // back into the header.
  BufferOffset *oldp = (*self)->ToAddress<BufferOffset>(header_offset);
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
  for (int i = 0; i < kNumBitmapRuns; i++) {
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
  f->length = full_size - header_size;
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
    FreeBlockHeader *next =
        reinterpret_cast<FreeBlockHeader *>(uintptr_t(block) + length);
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
    num_bytes = block->length - sizeof(uint32_t);
    UpdateHWM(block->next + sizeof(FreeBlockHeader));
  }
  return num_bytes;
}

inline uint32_t AlignSize(uint32_t s,
                          uint32_t alignment = uint32_t(sizeof(uint64_t))) {
  return (s + (alignment - 1)) & ~(alignment - 1);
}

void *PayloadBuffer::Allocate(PayloadBuffer **buffer, uint32_t n,
                              uint32_t alignment, bool clear,
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
  n = AlignSize(n, alignment); // Aligned.
  size_t full_length = n + sizeof(uint32_t);
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
      size_t old_size = (*buffer)->full_size;
      size_t new_size = old_size * 2;
      while (new_size < full_length) {
        new_size *= 2;
      }

      // Call the resizer.  This will move *buffer.
      (*resizer)(buffer, old_size, new_size);

      // Set the new size in the newly allocated bigger buffer.
      (*buffer)->full_size = new_size;

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
      char *start_of_new_memory = reinterpret_cast<char *>(*buffer) + old_size;
      bool free_list_expanded = false;
      if (prev != nullptr) {
        char *end_of_free_list = reinterpret_cast<char *>(prev) + prev->length;
        if (start_of_new_memory == end_of_free_list) {
          // Last free block is right at the end of the memory, so edxpand it
          // include the new memory.  This is likely to be true.
          prev->length += new_size - old_size;
          free_list_expanded = true;
        }
      }

      if (!free_list_expanded) {
        // Need to add a new free block to the end of the free list.
        FreeBlockHeader *new_block =
            reinterpret_cast<FreeBlockHeader *>(start_of_new_memory);
        new_block->next = 0;
        new_block->length = new_size - old_size;
        if (prev == nullptr) {
          (*buffer)->free_list = (*buffer)->ToOffset(new_block);
        } else {
          prev->next = (*buffer)->ToOffset(new_block);
        }
      }
      return Allocate(buffer, n, alignment, clear);
    }
    if (free_block->length >= full_length) {
      // Free block is big enough.  If there's enough room for the free block
      // header, take the lower part of the free block and keep the remainder
      // in the free list.
      n = (*buffer)->TakeStartOfFreeBlock(free_block, n, full_length, prev);
      size_t *newblock = (size_t *)free_block; // Start of new block.
      *newblock = n;                           // Size of allocated block.
      void *addr =
          reinterpret_cast<void *>(uintptr_t(free_block) + sizeof(uint32_t));
      if (clear) {
        memset(addr, 0, full_length - 4);
      }
      return addr;
    }
    prev = free_block;
    free_block = (*buffer)->ToAddress<FreeBlockHeader>(free_block->next);
  }
}

std::vector<void *> PayloadBuffer::AllocateMany(PayloadBuffer **buffer,
                                                uint32_t size, uint32_t n,
                                                uint32_t alignment,
                                                bool clear) {
  // Calculate space for the whole block.  This is n*aligned(size) +
  // n*sizeof(uint32_t).
  size_t full_length = n * (AlignSize(size, alignment) + sizeof(uint32_t));
  void *start = Allocate(buffer, full_length, 8, clear);
  if (start == nullptr) {
    return {}; // No memory.
  }
  if (clear) {
    memset(start, 0, full_length);
  }
  // Divide the block into freeable chunks, each of which is align(size) bytes
  // long.  The length of each block is stored immediately before the block.
  std::vector<void *> blocks;
  uint32_t *p = reinterpret_cast<uint32_t *>(start);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t len = AlignSize(size, alignment);
    p[0] = len;
    blocks.push_back(reinterpret_cast<void *>(p + 1));
    p = reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(p) + len +
                                     sizeof(uint32_t));
  }
  return blocks;
}

void PayloadBuffer::MergeWithAboveIfPossible(FreeBlockHeader *alloc_block,
                                             FreeBlockHeader *alloc_header,
                                             FreeBlockHeader *free_block,
                                             BufferOffset *next_ptr,
                                             size_t alloc_length) {
  uintptr_t alloc_addr = (uintptr_t)alloc_block;
  uintptr_t free_addr = (uintptr_t)free_block;

  if (alloc_addr + alloc_length == free_addr) {
    // Merge with block above.
    alloc_header->next = free_block->next;
    alloc_header->length =
        alloc_length + sizeof(BufferOffset) + free_block->length;
    *next_ptr = ToOffset(alloc_header);
  } else {
    // Not adjacent to above; add to free list.
    // t points to the allocated block header which has its length set.
    alloc_header->length += sizeof(BufferOffset);
    alloc_header->next = ToOffset(free_block);
    *next_ptr = ToOffset(alloc_header);
  }
}

static bool MergeWithBelowIfPossible(FreeBlockHeader *free_block,
                                     FreeBlockHeader *prev) {
  uintptr_t prev_addr = (uintptr_t)prev;
  if (prev_addr + prev->length == (uintptr_t)free_block) {
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
  uint32_t alloc_length =
      *(reinterpret_cast<uint32_t *>(p) - 1); // Length of allocated block.
  int small_block_index =
      BitmapsEnabled() ? BitmapRunIndexFromEncodedSize(alloc_length) : -1;
  if (small_block_index >= 0) {
    int bitnum = (alloc_length >> kBitmpRunBitNumShift) & kBitmapRunBitNumMask;
    int bitmap_index =
        (alloc_length >> kBitmapRunBitMapShift) & kBitmapRunBitMapMask;

    FreeSmallBlock(this, small_block_index, bitmap_index, bitnum);
    return;
  }
  // Point to real start of allocated block.
  FreeBlockHeader *alloc_header =
      reinterpret_cast<FreeBlockHeader *>(uintptr_t(p) - sizeof(uint32_t));

  // Insert into free list by searching for the appropriate point in memory
  // sorted by address.
  FreeBlockHeader *free_block = FreeList();
  if (free_block == nullptr) {
    // No free list, this block becomes the only block.
    alloc_header->length = alloc_length + sizeof(size_t);
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
                          alloc_header->length + sizeof(BufferOffset));
}

void PayloadBuffer::ShrinkBlock(FreeBlockHeader *alloc_block,
                                uint32_t orig_length, uint32_t new_length,
                                uint32_t *len_ptr) {
  assert(new_length < orig_length);
  size_t rem = orig_length - new_length;
  if (rem >= sizeof(FreeBlockHeader)) {
    // If we are freeing enough to make a free block, free it, otherwise
    // there's nothing we can do and we just keep the block the same size.
    *len_ptr = new_length; // Change size of block.
    uint32_t *newp = reinterpret_cast<uint32_t *>(
        reinterpret_cast<char *>(alloc_block) + sizeof(uint32_t) + new_length);
    *newp = rem - sizeof(uint32_t); // Add header for free.
    Free(newp + 1);
  }
}

void PayloadBuffer::ExpandIntoFreeBlockAbove(
    FreeBlockHeader *free_block, uint32_t new_length, uint32_t len_diff,
    uint32_t free_remaining, uint32_t *len_ptr, BufferOffset *next_ptr,
    bool clear) {
  assert(free_remaining > sizeof(FreeBlockHeader));
  FreeBlockHeader *next = ToAddress<FreeBlockHeader>(free_block->next);

  // The free block has enough space.
  *len_ptr = new_length;
  FreeBlockHeader *new_block =
      reinterpret_cast<FreeBlockHeader *>(uintptr_t(free_block) + len_diff);
  new_block->length = free_remaining;
  new_block->next = ToOffset(next);
  *next_ptr = ToOffset(new_block);
  UpdateHWM(new_block);
  if (clear) {
    memset(free_block, 0, len_diff);
  }
}

uint32_t *PayloadBuffer::MergeWithFreeBlockBelow(
    void *alloc_block, FreeBlockHeader *prev, FreeBlockHeader *free_block,
    uint32_t new_length, uint32_t orig_length, bool clear) {
  uintptr_t free_addr = (uintptr_t)free_block;

  BufferOffset *next_ptr;
  if (prev == NULL) {
    next_ptr = &free_list;
  } else {
    next_ptr = &prev->next;
  }
  // Move FreeBlockHeader to end of allocated block.  This is inside
  // the combined free block and block being reallocated.
  FreeBlockHeader *next = ToAddress<FreeBlockHeader>(free_block->next);
  FreeBlockHeader *newb = reinterpret_cast<FreeBlockHeader *>(
      free_addr + new_length + sizeof(uint32_t));
  newb->length = free_block->length + orig_length - new_length;
  newb->next = ToOffset(next);
  *next_ptr = ToOffset(newb);

  uint32_t *len_ptr = reinterpret_cast<uint32_t *>(free_block);
  *len_ptr = new_length;
  memmove(len_ptr + 1, alloc_block, orig_length);
  if (clear) {
    memset(reinterpret_cast<char *>(len_ptr) + 4 + orig_length, 0,
           new_length - orig_length);
  }
  return len_ptr + 1;
}

void *PayloadBuffer::Realloc(PayloadBuffer **buffer, void *p, uint32_t n,
                             uint32_t alignment, bool clear,
                             bool enable_small_block) {
  if (p == NULL) {
    // No block to realloc, just call malloc.
    return Allocate(buffer, n, alignment, clear);
  }
  // The allocated block has its length immediately prior to its address.
  uint32_t *len_ptr = reinterpret_cast<uint32_t *>(p) - 1;
  uint32_t orig_length = *len_ptr;
  if (enable_small_block && (*buffer)->BitmapsEnabled()) {
    int small_block_index = BitmapRunIndexFromEncodedSize(orig_length);
    if (small_block_index >= 0) {
      int decoded_length =
          (orig_length >> kBitmapRunSizeShift) & kBitmapRunSizeMask;
      // If the new size is in the same small block index we can just return the
      // original block.
      if (BitmapRunIndex(n) == small_block_index) {
        int bitnum =
            (orig_length >> kBitmpRunBitNumShift) & kBitmapRunBitNumMask;
        int bitmap_index =
            (orig_length >> kBitmapRunBitMapShift) & kBitmapRunBitMapMask;
        int encoded_size = (1U << 31) | (bitmap_index << kBitmapRunBitMapShift) |
                           (bitnum << kBitmpRunBitNumShift) |
                           (n & kBitmapRunSizeMask);

        *len_ptr = encoded_size;
        if (clear && n > decoded_length) {
          memset(reinterpret_cast<char *>(p) + decoded_length, 0,
                 n - decoded_length);
        }
        return p;
      }
      // Need to free the old block and allocate a new one as the small block
      // index is different.
      void *newp = Allocate(buffer, n, alignment, false, enable_small_block);
      if (newp == NULL) {
        return NULL;
      }
      memcpy(newp, p, decoded_length);
      if (clear && n > decoded_length) {
        memset(reinterpret_cast<char *>(newp) + decoded_length, 0,
               n - decoded_length);
      }
      (*buffer)->Free(p);
      return newp;
    }
  }
  FreeBlockHeader *alloc_block =
      reinterpret_cast<FreeBlockHeader *>(uintptr_t(p) - sizeof(uint32_t));
  uintptr_t alloc_addr = (uintptr_t)p;

  n = AlignSize(n); // Aligned.
  if (n == orig_length) {
    // Same size as current block, nothing to do.
    return p;
  }
  if (n < orig_length) {
    // Decreasing in size.  Free the remaining part.
    (*buffer)->ShrinkBlock(alloc_block, orig_length, n, len_ptr);
    return p;
  }

  // Increasing in size.
  // See if there's a free block immediately following allocated block.
  FreeBlockHeader *free_block = (*buffer)->FreeList();
  FreeBlockHeader *prev = NULL;
  FreeBlockHeader *prev_prev = NULL;
  while (free_block != NULL) {
    BufferOffset *next_ptr;
    if (prev == NULL) {
      next_ptr = &(*buffer)->free_list;
    } else {
      next_ptr = &prev->next;
    }
    if (free_block > alloc_block) {
      uintptr_t free_addr = (uintptr_t)free_block;
      int diff = n - orig_length;
      if (alloc_addr + orig_length == free_addr) {
        // There is a free block above.  See if has enough space.
        if (free_block->length > diff) {
          ssize_t freelen = free_block->length - diff;
          if (freelen > sizeof(FreeBlockHeader)) {
            (*buffer)->ExpandIntoFreeBlockAbove(free_block, n, diff, freelen,
                                                len_ptr, next_ptr, clear);
            return p;
          }
        }
      }
      // Check for free block adjacent below.
      if (prev != NULL) {
        uintptr_t prev_addr = (uintptr_t)prev;
        if (prev_addr + prev->length == (uintptr_t)alloc_block &&
            prev->length >= diff) {
          // Previous free block is adjacent and has enough space in it.
          // Use start of new block as new address and place FreeBlockHeader
          // at newly free part.
          return (*buffer)->MergeWithFreeBlockBelow(p, prev_prev, prev, n,
                                                    orig_length, clear);
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
  void *newp = Allocate(buffer, n, alignment, false, enable_small_block);
  if (newp == NULL) {
    return NULL;
  }
  memcpy(newp, p, orig_length);
  if (clear) {
    memset(reinterpret_cast<char *>(newp) + orig_length, 0, n - orig_length);
  }
  (*buffer)->Free(p);
  return newp;
}
bool PayloadBuffer::PrimeBitmapAllocator(PayloadBuffer **self, size_t size) {
  int index = BitmapRunIndex(size);
  if (index < 0) {
    return true;
  }
  if ((*self)->bitmaps[index] != 0) {
    return true;
  }
  BufferOffset offset = (*self)->AllocateBitMapRunVector(self);
  if (offset == 0) {
    return false;
  }
  (*self)->bitmaps[index] = offset;
  VectorHeader *hdr = (*self)->ToAddress<VectorHeader>((*self)->bitmaps[index]);

  BitMapRun *run = PayloadBuffer::AllocateBitMapRun(
      self, bitmp_run_infos[index].size, bitmp_run_infos[index].num);
  if (run == nullptr) {
    return false;
  }
  // Add to the vector, this might move the vector contents but the header
  // will stay where it is.
  (*self)->VectorPush<BufferOffset>(self, hdr, (*self)->ToOffset(run), false);
  return true;
}

BufferOffset PayloadBuffer::AllocateBitMapRunVector(PayloadBuffer **self) {
  // Allocate space for the VectorHeader.  Although this is a small block, we
  // can't use the small block allocator because this is initializing it.
  void *hdr = Allocate(self, sizeof(VectorHeader), 4, true, false);
  if (hdr == nullptr) {
    return 0;
  }
  BufferOffset hdr_offset = (*self)->ToOffset(hdr);

  // Preallocate space for 8 elements.
  VectorReserve<BufferOffset>(self, reinterpret_cast<VectorHeader *>(hdr), 8,
                              false);
  return hdr_offset;
}

BitMapRun *PayloadBuffer::AllocateBitMapRun(PayloadBuffer **self, uint32_t size,
                                            uint32_t num) {
  // It is important that this isn't a small block as it will infintely recurse.
  // The full size of the BitMapRun contains space for the blocks themselves,
  // each of which is prefixed by a 4 byte length.  The length is encoded
  // with data necessary for freeing it.
  BitMapRun *run = reinterpret_cast<BitMapRun *>(
      Allocate(self, sizeof(BitMapRun) + (size + 4) * num, 4, false, false));
  if (run == nullptr) {
    return nullptr;
  }
  run->size = size;
  run->num = num;
  run->bits = 0;
  run->free = num; // All blocks are free.
  return run;
}

void *BitMapRun::Allocate(PayloadBuffer **pb, int index, uint32_t n, int size,
                          int num, bool clear) {
  // Lazy init of vector.
  if ((*pb)->bitmaps[index] == 0) {
    BufferOffset offset = (*pb)->AllocateBitMapRunVector(pb);
    if (offset == 0) {
      return nullptr;
    }
    (*pb)->bitmaps[index] = offset;
  }
  VectorHeader *hdr = (*pb)->ToAddress<VectorHeader>((*pb)->bitmaps[index]);
  for (;;) {
    // Go backwards through the elements as that is most likely to find a free
    // bit.
    for (int i = hdr->num_elements - 1; i >= 0; i--) {
      BitMapRun *run =
          (*pb)->ToAddress<BitMapRun>((*pb)->VectorGet<BufferOffset>(hdr, i));
      if (run->free == 0) {
        continue;
      }
      // Fast path: there is a free bit in the run.
      int bit = ffs(~run->bits);
      assert(bit > 0 && bit <= run->num);
      bit--; // Convert to 0-based index.
      run->bits |= 1U << bit;
      run->free--;

      // The address of the block is after the header and indexed by the bit
      // number times the size of the block plus 4 bytes for the length.  Then
      // we need the address after the length word.
      void *addr = reinterpret_cast<char *>(run) + sizeof(BitMapRun) +
                   bit * (run->size + 4) + 4;
      // Write the encoded size of the block into the preceding 4 bytes.
      uint32_t *p = reinterpret_cast<uint32_t *>(addr) - 1;
      // Encode the length.
      int encoded_size = (1U << 31) | (i << kBitmapRunBitMapShift) |
                         (bit << kBitmpRunBitNumShift) |
                         (size & kBitmapRunSizeMask);
      *p = encoded_size;
      if (clear) {
        memset(addr, 0, size);
      }
      return addr;
    }
    // Slow path, no free bits in any run.  We need to allocate a new run.
    BitMapRun *run = PayloadBuffer::AllocateBitMapRun(pb, size, num);
    if (run == nullptr) {
      return nullptr;
    }
    // Add to the vector, this might move the vector contents but the header
    // will stay where it is.
    (*pb)->VectorPush<BufferOffset>(pb, hdr, (*pb)->ToOffset(run), false);
  }
}

void BitMapRun::Free(PayloadBuffer *pb, int index, int bitmap_index,
                     int bitnum) {
  // This is always fast path since we have all the information we need
  // to free the block.  We basically just clear a bit and increment the
  // free count.
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->bitmaps[index]);
  assert(hdr != nullptr);
  BitMapRun *run =
      pb->ToAddress<BitMapRun>(pb->VectorGet<BufferOffset>(hdr, bitmap_index));
  run->bits &= ~(1U << bitnum);
  run->free++;
}

void *PayloadBuffer::AllocateSmallBlock(PayloadBuffer **pb, uint32_t size,
                                        int index, bool clear) {
  return BitMapRun::Allocate(pb, index, size, bitmp_run_infos[index].size,
                             bitmp_run_infos[index].num, clear);
}

void PayloadBuffer::FreeSmallBlock(PayloadBuffer *pb, int index,
                                   int bitmap_index, int bitnum) {
  BitMapRun::Free(pb, index, bitmap_index, bitnum);
}
} // namespace toolbelt
