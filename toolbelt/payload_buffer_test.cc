#include "toolbelt/clock.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"
#include <cstddef>
#include <gtest/gtest.h>
#include <limits>
#include <sstream>

using PayloadBuffer = toolbelt::PayloadBuffer;
using BufferOffset = toolbelt::BufferOffset;
using VectorHeader = toolbelt::VectorHeader;
using Resizer = toolbelt::Resizer;

TEST(BufferTest, Simple) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 32);
  memset(addr, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, TwoAllocs) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 32);
  memset(addr, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  addr = PayloadBuffer::Allocate(&pb, 64);
  memset(addr, 0xda, 64);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, Free) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr1 = PayloadBuffer::Allocate(&pb, 32);
  memset(addr1, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr1 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  void *addr2 = PayloadBuffer::Allocate(&pb, 64);
  memset(addr2, 0xda, 64);

  pb->Free(addr1);

  pb->Dump(std::cout);
  std::cout << "Allocated " << addr2 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, FreeThenAlloc) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr1 = PayloadBuffer::Allocate(&pb, 32);
  memset(addr1, 0xda, 32);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr1 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  void *addr2 = PayloadBuffer::Allocate(&pb, 64);
  memset(addr2, 0xda, 64);

  pb->Free(addr1);

  // 20 bytes fits into the free block.
  void *addr3 = PayloadBuffer::Allocate(&pb, 20);
  memset(addr3, 0xda, 20);

  pb->Dump(std::cout);
  std::cout << "Allocated " << addr2 << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, SmallBlockAllocSimple) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  void *addr = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_NE(nullptr, addr);
  pb->Free(addr);

  // Allocate again and make sure it's the same address.
  void *addr2 = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_EQ(addr, addr2);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, SmallBlockAlloc) {
  char *buffer = (char *)calloc(1, 8192);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(8192);

  // Small block sizes are 16, 32, 64 and 128.
  // There are 16 16-byte blocks per run.
  for (int i = 0; i < 50; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 10);
    memset(addr, 0xda, 10);
  }

  // There are 8 32-byte blocks per run.
  for (int i = 0; i < 20; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 30);
    memset(addr, 0xdb, 30);
  }

  // There are 4 64-byte blocks per run.
  for (int i = 0; i < 10; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 50);
    memset(addr, 0xdc, 50);
  }

  // There are 2 128-byte blocks per run.
  for (int i = 0; i < 5; i++) {
    void *addr = PayloadBuffer::Allocate(&pb, 100);
    memset(addr, 0xdd, 100);
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, SmallBlockAllocFree) {
  char *buffer = (char *)calloc(1, 8192);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(8192);

  // Do a mix of sizes and free them.
  std::vector<void *> blocks;
  std::vector<size_t> sizes = {10, 30, 50, 100, 150};
  for (int i = 0; i < 50; i++) {
    size_t size = sizes[i % sizes.size()];
    void *addr = PayloadBuffer::Allocate(&pb, size);
    memset(addr, 0xda, size);
    blocks.push_back(addr);
  }
  // Free every 5th block.
  for (size_t i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      pb->Free(blocks[i]);
    }
  }
  // Now allocate every 5th block again.
  for (size_t i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      size_t size = sizes[i % sizes.size()];
      void *addr = PayloadBuffer::Allocate(&pb, size);
      memset(addr, 0xda, size);
      blocks[i] = addr;
    }
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, PrimeBitmapAllocatorReserveFailureReclaimsVectorHeader) {
  constexpr size_t kReserveAllocationSize =
      8 * sizeof(BufferOffset) + sizeof(uint64_t);
  constexpr size_t kSize = sizeof(PayloadBuffer) + kReserveAllocationSize;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  const BufferOffset initial_free_list = pb->free_list;
  toolbelt::FreeBlockHeader *initial_free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(initial_free_list);
  ASSERT_NE(nullptr, initial_free_block);
  const uint32_t initial_free_length = initial_free_block->length;

  EXPECT_FALSE(PayloadBuffer::PrimeBitmapAllocator(
      &pb, toolbelt::kBitmapRunSize1));
  EXPECT_EQ(0u, pb->bitmaps[0]);
  EXPECT_EQ(initial_free_list, pb->free_list);
  toolbelt::FreeBlockHeader *restored_free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(pb->free_list);
  ASSERT_NE(nullptr, restored_free_block);
  EXPECT_EQ(initial_free_length, restored_free_block->length);

  free(buffer);
}

TEST(BufferTest, PrimeBitmapAllocatorRunFailureRollsBackInitialization) {
  constexpr size_t kVectorHeaderAllocationSize =
      sizeof(VectorHeader) + sizeof(uint64_t);
  constexpr size_t kReserveAllocationSize =
      8 * sizeof(BufferOffset) + sizeof(uint64_t);
  constexpr size_t kSize =
      sizeof(PayloadBuffer) + kVectorHeaderAllocationSize +
      kReserveAllocationSize + sizeof(toolbelt::FreeBlockHeader);
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  const BufferOffset initial_free_list = pb->free_list;
  toolbelt::FreeBlockHeader *initial_free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(initial_free_list);
  ASSERT_NE(nullptr, initial_free_block);
  const uint32_t initial_free_length = initial_free_block->length;

  for (int attempt = 0; attempt < 2; attempt++) {
    EXPECT_FALSE(PayloadBuffer::PrimeBitmapAllocator(
        &pb, toolbelt::kBitmapRunSize1));
    EXPECT_EQ(0u, pb->bitmaps[0]);
    EXPECT_EQ(initial_free_list, pb->free_list);
    toolbelt::FreeBlockHeader *restored_free_block =
        pb->ToAddress<toolbelt::FreeBlockHeader>(pb->free_list);
    ASSERT_NE(nullptr, restored_free_block);
    EXPECT_EQ(initial_free_length, restored_free_block->length);
  }

  free(buffer);
}

TEST(BufferTest, LazyBitmapAllocatorRunFailureRollsBackInitialization) {
  constexpr size_t kVectorHeaderAllocationSize =
      sizeof(VectorHeader) + sizeof(uint64_t);
  constexpr size_t kReserveAllocationSize =
      8 * sizeof(BufferOffset) + sizeof(uint64_t);
  constexpr size_t kSize =
      sizeof(PayloadBuffer) + kVectorHeaderAllocationSize +
      kReserveAllocationSize + sizeof(toolbelt::FreeBlockHeader);
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  const BufferOffset initial_free_list = pb->free_list;
  toolbelt::FreeBlockHeader *initial_free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(initial_free_list);
  ASSERT_NE(nullptr, initial_free_block);
  const uint32_t initial_free_length = initial_free_block->length;

  for (int attempt = 0; attempt < 2; attempt++) {
    EXPECT_EQ(nullptr,
              PayloadBuffer::Allocate(&pb, toolbelt::kBitmapRunSize1));
    EXPECT_EQ(0u, pb->bitmaps[0]);
    EXPECT_EQ(initial_free_list, pb->free_list);
    toolbelt::FreeBlockHeader *restored_free_block =
        pb->ToAddress<toolbelt::FreeBlockHeader>(pb->free_list);
    ASSERT_NE(nullptr, restored_free_block);
    EXPECT_EQ(initial_free_length, restored_free_block->length);
  }

  free(buffer);
}

TEST(BufferTest, BitmapRunGrowthFailureReclaimsUnappendedRun) {
  constexpr size_t kSize = 8192;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  ASSERT_TRUE(
      PayloadBuffer::PrimeBitmapAllocator(&pb, toolbelt::kBitmapRunSize1));
  const BufferOffset bitmap_vector_offset = pb->bitmaps[0];
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(bitmap_vector_offset);
  ASSERT_NE(nullptr, hdr);
  const size_t bitmap_capacity =
      PayloadBuffer::DecodedSize(pb->ToAddress<BufferOffset>(hdr->data)) /
      sizeof(BufferOffset);
  ASSERT_GT(bitmap_capacity, hdr->num_elements);

  for (size_t i = hdr->num_elements; i < bitmap_capacity; i++) {
    toolbelt::BitMapRun *run = PayloadBuffer::AllocateBitMapRun(
        &pb, toolbelt::kBitmapRunSize1, toolbelt::kRunSize1);
    ASSERT_NE(nullptr, run);
    const BufferOffset run_offset = pb->ToOffset(run);
    hdr = pb->ToAddress<VectorHeader>(bitmap_vector_offset);
    ASSERT_TRUE(
        PayloadBuffer::VectorPush<BufferOffset>(&pb, hdr, run_offset, false));
  }
  hdr = pb->ToAddress<VectorHeader>(bitmap_vector_offset);
  ASSERT_EQ(bitmap_capacity, hdr->num_elements);
  const BufferOffset bitmap_data_offset = hdr->data;
  for (size_t i = 0; i < hdr->num_elements; i++) {
    toolbelt::BitMapRun *run =
        pb->ToAddress<toolbelt::BitMapRun>(pb->VectorGet<BufferOffset>(hdr, i));
    ASSERT_NE(nullptr, run);
    run->bits = run->num == 32 ? std::numeric_limits<uint32_t>::max()
                               : (uint32_t{1} << run->num) - 1;
    run->free = 0;
  }

  toolbelt::BitMapRun *probe_run = PayloadBuffer::AllocateBitMapRun(
      &pb, toolbelt::kBitmapRunSize1, toolbelt::kRunSize1);
  ASSERT_NE(nullptr, probe_run);
  const size_t run_allocation_size =
      PayloadBuffer::DecodedSize(reinterpret_cast<BufferOffset *>(probe_run)) +
      sizeof(uint64_t);
  pb->Free(probe_run);

  toolbelt::FreeBlockHeader *free_block = pb->FreeList();
  ASSERT_NE(nullptr, free_block);
  ASSERT_EQ(0u, free_block->next);
  ASSERT_GT(free_block->length, run_allocation_size + sizeof(uint64_t));
  const size_t drain_size =
      free_block->length - run_allocation_size - sizeof(uint64_t);
  ASSERT_EQ(0u, drain_size % sizeof(uint64_t));
  ASSERT_NE(nullptr, PayloadBuffer::Allocate(&pb, drain_size, false, false));

  const BufferOffset initial_free_list = pb->free_list;
  free_block = pb->FreeList();
  ASSERT_NE(nullptr, free_block);
  ASSERT_EQ(run_allocation_size, free_block->length);

  for (int attempt = 0; attempt < 2; attempt++) {
    EXPECT_EQ(nullptr,
              PayloadBuffer::Allocate(&pb, toolbelt::kBitmapRunSize1));
    hdr = pb->ToAddress<VectorHeader>(bitmap_vector_offset);
    ASSERT_NE(nullptr, hdr);
    EXPECT_EQ(bitmap_capacity, hdr->num_elements);
    EXPECT_EQ(bitmap_data_offset, hdr->data);
    EXPECT_EQ(initial_free_list, pb->free_list);
    free_block = pb->FreeList();
    ASSERT_NE(nullptr, free_block);
    EXPECT_EQ(run_allocation_size, free_block->length);
  }

  free(buffer);
}

// This performance test compares the performance of the small block allocator
// against the regular allocator.  It is a best-case test where we are not
// stressing the small block allocator by allocating more blocks than a single
// run.  We are also not stressing the free list in the regular allocator and it
// always be taking the block from the start of the free list (no
// fragmentation).
TEST(BufferTest, BestCasePerformance) {
  constexpr int kSize = 2 * 1024 * 1024;

  double total_small = 0;
  double total_large = 0;

  constexpr int kIterations = 10000;

  for (int iter = 0; iter < kIterations; iter++) {
    char *buffer = (char *)calloc(1, kSize);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 16));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 32));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 64));
    ASSERT_TRUE(PayloadBuffer::PrimeBitmapAllocator(&pb, 128));

    uint64_t small_start = toolbelt::Now();

    std::vector<void *> small_blocks;

    // 16 byte blocks.
    for (int i = 0; i < 32; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 10, false);
      small_blocks.push_back(addr);
    }

    // 32 byte blocks.
    for (int i = 0; i < 16; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 28, false);
      small_blocks.push_back(addr);
    }

    // 64 byte blocks.
    for (int i = 0; i < 8; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 60, false);
      small_blocks.push_back(addr);
    }

    // 128 byte blocks.
    for (int i = 0; i < 4; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 120, false);
      small_blocks.push_back(addr);
    }

    // Free all the blocks.
    for (auto addr : small_blocks) {
      pb->Free(addr);
    }

    uint64_t small_end = toolbelt::Now();

    // New buffer.
    free(buffer);
    buffer = (char *)calloc(1, kSize);
    pb = new (buffer) PayloadBuffer(kSize);

    // Now allocate by disabling the small block allocator.
    std::vector<void *> large_blocks;
    uint64_t large_start = toolbelt::Now();

    // 16 byte blocks.
    for (int i = 0; i < 32; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 10, false, false);
      large_blocks.push_back(addr);
    }

    // 32 byte blocks.
    for (int i = 0; i < 16; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 28, false, false);
      large_blocks.push_back(addr);
    }

    // 64 byte blocks.
    for (int i = 0; i < 8; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 60, false, false);
      large_blocks.push_back(addr);
    }

    // 128 byte blocks.
    for (int i = 0; i < 4; i++) {
      void *addr = PayloadBuffer::Allocate(&pb, 120, false, false);
      large_blocks.push_back(addr);
    }

    // Free them
    for (auto addr : large_blocks) {
      pb->Free(addr);
    }

    uint64_t large_end = toolbelt::Now();

    // Update totals.
    total_small += (small_end - small_start);
    total_large += (large_end - large_start);
    free(buffer);
  }

  std::cout << "Small block allocator: " << (total_small / kIterations) << " ns"
            << std::endl;
  std::cout << "Large block allocator: " << (total_large / kIterations) << " ns"
            << std::endl;
  // Ratio of small block allocator to large block allocator.
  std::cout << "Ratio: " << (total_large / total_small) << std::endl;
}

TEST(BufferTest, TypicalPerformance) {
  constexpr int kSize = 2 * 1024 * 1024;

  constexpr int kNumBlocks = 100;

  double total_small = 0;
  double total_large = 0;

  constexpr int kIterations = 100;
  std::vector<size_t> sizes;
  // Random sizes up to 128
  for (int i = 0; i < kNumBlocks; i++) {
    sizes.push_back((rand() % 127) + 1);
  }

  for (int iter = 0; iter < kIterations; iter++) {
    char *buffer = (char *)calloc(1, kSize);
    PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

    // No priming the small block allocator for this test.  It probably won't
    // be called in real life.

    // Allocate some small blocks (<= 128 bytes).
    std::vector<void *> small_blocks;
    uint64_t small_start = toolbelt::Now();
    for (int j = 0; j < 1000; j++) {
      int prev_size = int(small_blocks.size());
      for (int i = 0; i < kNumBlocks; i++) {
        void *addr = PayloadBuffer::Allocate(&pb, 10, false);
        small_blocks.push_back(addr);
      }
      // Free some of the blocks.
      for (size_t i = prev_size; i < small_blocks.size(); i++) {
        if (i % 8 == 0) {
          continue;
        }
        pb->Free(small_blocks[i]);
        small_blocks[i] = nullptr;
      }
    }
    uint64_t small_end = toolbelt::Now();

    total_small += (small_end - small_start);

    // New buffer.
    free(buffer);
    buffer = (char *)calloc(1, kSize);
    pb = new (buffer) PayloadBuffer(kSize);

    // Switch off small block alloctor.
    std::vector<void *> large_blocks;
    uint64_t large_start = toolbelt::Now();
    for (int j = 0; j < 1000; j++) {
      int prev_size = int(large_blocks.size());
      for (int i = 0; i < kNumBlocks; i++) {
        void *addr = PayloadBuffer::Allocate(&pb, 10, false,
                                             /*enable_small_block=*/false);
        large_blocks.push_back(addr);
      }
      // Free some of the blocks.
      for (size_t i = prev_size; i < large_blocks.size(); i++) {
        if (i % 8 == 0) {
          continue;
        }
        pb->Free(large_blocks[i]);
        large_blocks[i] = nullptr;
      }
    }
    uint64_t large_end = toolbelt::Now();
    total_large += (large_end - large_start);
    free(buffer);
  }

  std::cout << "Small block allocator: " << (total_small / kIterations) << " ns"
            << std::endl;
  std::cout << "Large block allocator: " << (total_large / kIterations) << " ns"
            << std::endl;
  std::cout << "Ratio: " << (total_large / total_small) << std::endl;
}

TEST(BufferTest, Many) {
  constexpr size_t kSize = 8192;
  char *buffer = (char *)calloc(1, kSize);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  std::vector<void *> addrs = PayloadBuffer::AllocateMany(&pb, 100, 10, true);
  ASSERT_EQ(10, addrs.size());
  // Print the addresses.
  for (auto addr : addrs) {
    std::cout << "Allocated " << addr << std::endl;
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  // Make sure we can free them.
  pb->Free(addrs[0]);
  pb->Free(addrs[2]);

  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);
  free(buffer);
}

TEST(BufferTest, String) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing an offset for the string.
  PayloadBuffer::AllocateMainMessage(&pb, 32);

  void *addr = pb->ToAddress(pb->message);
  std::cout << "Messsage allocated at " << addr << std::endl;
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);
  BufferOffset offset = pb->ToOffset(addr);

  char *s = PayloadBuffer::SetString(&pb, std::string("foobar"), offset);
  std::cout << "String allocated at " << (void *)s << std::endl;

  toolbelt::Hexdump(pb, pb->hwm);

  // Now put in a bigger string, replacing the old one.
  s = PayloadBuffer::SetString(&pb, std::string("foobar has been replaced"),
                               offset);
  std::cout << "New string allocated at " << (void *)s << std::endl;

  toolbelt::Hexdump(pb, pb->hwm);

  std::string rs = pb->GetString(offset);
  ASSERT_EQ("foobar has been replaced", rs);
  free(buffer);
}

TEST(BufferTest, Vector) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x12345678);
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  uint32_t v = pb->VectorGet<uint32_t>(hdr, 0);
  ASSERT_EQ(0x12345678, v);

  free(buffer);
}

TEST(BufferTest, VectorExpand) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);

  for (int i = 0; i < 3; i++) {
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
  }
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);
  for (int i = 0; i < 3; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }

  free(buffer);
}

TEST(BufferTest, VectorExpandMore) {
  char *buffer = (char *)calloc(1, 4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  // Allocate space for a message containing the VectorHeader.
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);
  std::cout << "Vector header: " << hdr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  pb->Dump(std::cout);

  for (int i = 0; i < 100; i++) {
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }
  toolbelt::Hexdump(pb, pb->hwm);

  for (int i = 0; i < 100; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }
  pb->Dump(std::cout);

  free(buffer);
}

TEST(BufferTest, VectorPushWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  constexpr int kCount = 200;
  for (int i = 0; i < kCount; i++) {
    VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
    PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, i + 1);
  }
  ASSERT_TRUE(resized);

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_EQ(kCount, hdr->num_elements);
  for (int i = 0; i < kCount; i++) {
    uint32_t v = pb->VectorGet<uint32_t>(hdr, i);
    ASSERT_EQ(i + 1, v);
  }

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, VectorReserveWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  PayloadBuffer::VectorReserve<uint32_t>(&pb, hdr, 500);
  ASSERT_TRUE(resized);

  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_NE(0u, hdr->data);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, VectorResizeWithResize) {
  char *buffer = (char *)calloc(256, 1);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized, &buffer](PayloadBuffer **p, size_t, size_t new_size) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        buffer = reinterpret_cast<char *>(*p);
        resized = true;
      });

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader));
  BufferOffset msg_offset = pb->message;

  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, 500);
  ASSERT_TRUE(resized);

  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_EQ(500u, hdr->num_elements);

  pb->~PayloadBuffer();
  free(buffer);
}

TEST(BufferTest, EmptyVectorZeroSizeOperationsSucceed) {
  constexpr size_t kSize = 256;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize, false);

  ASSERT_NE(nullptr,
            PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader)));
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(pb->message);

  EXPECT_TRUE(PayloadBuffer::VectorReserve<uint32_t>(&pb, hdr, 0, false));
  EXPECT_TRUE(PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, 0));
  EXPECT_EQ(0u, hdr->data);
  EXPECT_EQ(0u, hdr->num_elements);

  free(buffer);
}

TEST(BufferTest, VectorPushFixedBufferAllocationFailurePreservesHeader) {
  constexpr size_t kSize = 256;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize, false);

  ASSERT_NE(nullptr,
            PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader)));
  const BufferOffset msg_offset = pb->message;
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);

  toolbelt::FreeBlockHeader *free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(pb->free_list);
  ASSERT_NE(nullptr, free_block);
  ASSERT_NE(nullptr,
            PayloadBuffer::Allocate(&pb, free_block->length - sizeof(uint64_t),
                                    false, false));
  ASSERT_EQ(0u, pb->free_list);

  EXPECT_FALSE(
      PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x12345678, false));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  EXPECT_EQ(0u, hdr->data);
  EXPECT_EQ(0u, hdr->num_elements);

  free(buffer);
}

TEST(BufferTest, VectorPushFixedBufferGrowthFailurePreservesHeader) {
  constexpr size_t kSize = 256;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize, false);

  ASSERT_NE(nullptr,
            PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader)));
  const BufferOffset msg_offset = pb->message;
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);
  ASSERT_TRUE(
      PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x12345678, false));
  ASSERT_TRUE(
      PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x9abcdef0, false));

  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  const BufferOffset original_data = hdr->data;
  toolbelt::FreeBlockHeader *free_block =
      pb->ToAddress<toolbelt::FreeBlockHeader>(pb->free_list);
  ASSERT_NE(nullptr, free_block);
  ASSERT_NE(nullptr,
            PayloadBuffer::Allocate(&pb, free_block->length - sizeof(uint64_t),
                                    false, false));
  ASSERT_EQ(0u, pb->free_list);

  EXPECT_FALSE(
      PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0xdeadbeef, false));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  EXPECT_EQ(original_data, hdr->data);
  ASSERT_EQ(2u, hdr->num_elements);
  EXPECT_EQ(0x12345678u, pb->VectorGet<uint32_t>(hdr, 0));
  EXPECT_EQ(0x9abcdef0u, pb->VectorGet<uint32_t>(hdr, 1));

  free(buffer);
}

TEST(BufferTest, VectorReserveFixedBufferFailurePreservesHeader) {
  constexpr size_t kSize = 256;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize, false);

  ASSERT_NE(nullptr,
            PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader)));
  const BufferOffset msg_offset = pb->message;
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);

  EXPECT_FALSE(PayloadBuffer::VectorReserve<uint32_t>(&pb, hdr, kSize, false));
  EXPECT_EQ(0u, hdr->data);
  EXPECT_EQ(0u, hdr->num_elements);

  ASSERT_TRUE(
      PayloadBuffer::VectorPush<uint32_t>(&pb, hdr, 0x12345678, false));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  const BufferOffset original_data = hdr->data;

  EXPECT_FALSE(PayloadBuffer::VectorReserve<uint32_t>(&pb, hdr, kSize, false));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  EXPECT_EQ(original_data, hdr->data);
  ASSERT_EQ(1u, hdr->num_elements);
  EXPECT_EQ(0x12345678u, pb->VectorGet<uint32_t>(hdr, 0));

  free(buffer);
}

TEST(BufferTest, VectorResizeFixedBufferFailurePreservesHeader) {
  constexpr size_t kSize = 256;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize, false);

  ASSERT_NE(nullptr,
            PayloadBuffer::AllocateMainMessage(&pb, sizeof(VectorHeader)));
  const BufferOffset msg_offset = pb->message;
  VectorHeader *hdr = pb->ToAddress<VectorHeader>(msg_offset);

  EXPECT_FALSE(PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, kSize));
  EXPECT_EQ(0u, hdr->data);
  EXPECT_EQ(0u, hdr->num_elements);

  ASSERT_TRUE(PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, 2));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  const BufferOffset original_data = hdr->data;
  uint32_t *values = pb->ToAddress<uint32_t>(original_data, 2 * sizeof(uint32_t));
  ASSERT_NE(nullptr, values);
  values[0] = 0x12345678;
  values[1] = 0x9abcdef0;

  EXPECT_FALSE(PayloadBuffer::VectorResize<uint32_t>(&pb, hdr, kSize));
  hdr = pb->ToAddress<VectorHeader>(msg_offset);
  EXPECT_EQ(original_data, hdr->data);
  ASSERT_EQ(2u, hdr->num_elements);
  EXPECT_EQ(0x12345678u, pb->VectorGet<uint32_t>(hdr, 0));
  EXPECT_EQ(0x9abcdef0u, pb->VectorGet<uint32_t>(hdr, 1));

  free(buffer);
}

TEST(BufferTest, Resizeable) {
  char *buffer = (char *)calloc(1, 512);
  bool resized = false;
  PayloadBuffer *pb = new (buffer) PayloadBuffer(
      256, [&resized](PayloadBuffer **p, size_t, size_t new_size) {
        std::cout << "resize for " << new_size << std::endl;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-memaccess"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        resized = true;
      });
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, 64);

  void *addr = PayloadBuffer::Allocate(&pb, 130);
  ASSERT_NE(nullptr, addr);
  ASSERT_FALSE(resized);

  memset(addr, 0xda, 128);
  pb->Dump(std::cout);
  std::cout << "Allocated " << addr << std::endl;
  toolbelt::Hexdump(pb, pb->hwm);

  // This will cause a resize.
  addr = PayloadBuffer::Allocate(&pb, 256);
  ASSERT_NE(nullptr, addr);
  ASSERT_TRUE(resized);
  memset(addr, 0xdd, 128);

  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);

  // Don't free 'buffer' as it has already been freed by the call to realloc.
  // pb was constructed via placement-new on a malloc/realloc'd buffer, so we
  // must invoke the destructor explicitly and then free() the storage rather
  // than calling operator delete (which would be an alloc-dealloc mismatch).
  pb->~PayloadBuffer();
  free(pb);
}

TEST(BufferTest, ToAddressRejectsTypedReadPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  EXPECT_EQ(pb->ToAddress<uint32_t>(kSize - 2), nullptr);
  EXPECT_NE(pb->ToAddress<uint32_t>(kSize - sizeof(uint32_t)), nullptr);

  free(buffer);
}

TEST(BufferTest, StringHelpersRejectLengthHeaderPastEnd) {
  constexpr size_t kSize = 4093;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  toolbelt::StringHeader header = static_cast<BufferOffset>(4092);

  EXPECT_EQ(pb->StringSize(&header), 0u);
  EXPECT_EQ(pb->GetString(&header), "");

  free(buffer);
}

TEST(BufferTest, StringSizeRejectsBodyPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  toolbelt::StringHeader header =
      static_cast<BufferOffset>(kSize - sizeof(uint32_t));
  uint32_t declared = 1;
  memcpy(buffer + header, &declared, sizeof(declared));

  EXPECT_EQ(pb->StringData(&header), nullptr);
  EXPECT_EQ(pb->StringSize(&header), 0u);

  free(buffer);
}

TEST(BufferTest, EmptyStringAtBufferTailAccepted) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  toolbelt::StringHeader header =
      static_cast<BufferOffset>(kSize - sizeof(uint32_t));
  uint32_t declared = 0;
  memcpy(buffer + header, &declared, sizeof(declared));

  EXPECT_NE(pb->StringData(&header), nullptr);
  EXPECT_EQ(pb->StringSize(&header), 0u);
  EXPECT_EQ(pb->GetString(&header), "");
  EXPECT_EQ(pb->GetStringView(&header), "");

  free(buffer);
}

TEST(BufferTest, StringWithinBoundsAcceptsValidString) {
  char *buffer = (char *)calloc(4096, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  PayloadBuffer::AllocateMainMessage(&pb, 32);
  BufferOffset offset = pb->ToOffset(pb->ToAddress(pb->message));
  PayloadBuffer::SetString(&pb, std::string("foobar"), offset);

  EXPECT_TRUE(pb->StringWithinBounds(offset));

  free(buffer);
}

TEST(BufferTest, StringWithinBoundsAcceptsUnsetString) {
  char *buffer = (char *)calloc(4096, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  PayloadBuffer::AllocateMainMessage(&pb, sizeof(BufferOffset));

  EXPECT_TRUE(pb->StringWithinBounds(pb->message));

  free(buffer);
}

TEST(BufferTest, SetStringFixedBufferFailurePreservesHeader) {
  constexpr uint32_t kBufferSize = 4096;
  char *buffer = (char *)calloc(kBufferSize, 1);
  PayloadBuffer *pb =
      new (buffer) PayloadBuffer(kBufferSize, /*bitmap_allocator=*/false);
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(toolbelt::StringHeader));

  const uint32_t free_len = pb->FreeList()->length;
  constexpr uint32_t kRemainingBytes = 2 * sizeof(uint64_t);
  ASSERT_GT(free_len, kRemainingBytes + sizeof(uint64_t));
  const uint32_t drain = free_len - kRemainingBytes - sizeof(uint64_t);
  ASSERT_NE(PayloadBuffer::Allocate(&pb, drain), nullptr);

  EXPECT_EQ(PayloadBuffer::SetString(&pb, "too large", pb->message), nullptr);
  EXPECT_EQ(*pb->ToAddress<toolbelt::StringHeader>(pb->message),
            BufferOffset(0));
  EXPECT_TRUE(PayloadBuffer::AllocateString(&pb, 9, pb->message).empty());
  EXPECT_EQ(*pb->ToAddress<toolbelt::StringHeader>(pb->message),
            BufferOffset(0));

  free(buffer);
}

TEST(BufferTest, StringReallocFixedBufferFailurePreservesValue) {
  constexpr uint32_t kBufferSize = 4096;
  char *buffer = (char *)calloc(kBufferSize, 1);
  PayloadBuffer *pb =
      new (buffer) PayloadBuffer(kBufferSize, /*bitmap_allocator=*/false);
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(toolbelt::StringHeader));
  ASSERT_NE(PayloadBuffer::SetString(&pb, "x", pb->message), nullptr);

  const BufferOffset original_offset =
      *pb->ToAddress<toolbelt::StringHeader>(pb->message);
  ASSERT_NE(original_offset, BufferOffset(0));

  const uint32_t free_len = pb->FreeList()->length;
  constexpr uint32_t kRemainingBytes = 2 * sizeof(uint64_t);
  ASSERT_GT(free_len, kRemainingBytes + sizeof(uint64_t));
  const uint32_t drain = free_len - kRemainingBytes - sizeof(uint64_t);
  ASSERT_NE(PayloadBuffer::Allocate(&pb, drain), nullptr);

  EXPECT_EQ(PayloadBuffer::SetString(&pb, "too large", pb->message), nullptr);
  EXPECT_EQ(*pb->ToAddress<toolbelt::StringHeader>(pb->message),
            original_offset);
  EXPECT_EQ(pb->GetString(pb->ToAddress<toolbelt::StringHeader>(pb->message)),
            "x");
  EXPECT_TRUE(PayloadBuffer::AllocateString(&pb, 9, pb->message).empty());
  EXPECT_EQ(*pb->ToAddress<toolbelt::StringHeader>(pb->message),
            original_offset);
  EXPECT_EQ(pb->GetString(pb->ToAddress<toolbelt::StringHeader>(pb->message)),
            "x");

  free(buffer);
}

TEST(BufferTest, StringWithinBoundsRejectsNullHeader) {
  char *buffer = (char *)calloc(4096, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  EXPECT_FALSE(
      pb->StringWithinBounds(static_cast<const toolbelt::StringHeader *>(nullptr)));

  free(buffer);
}

TEST(BufferTest, StringReadersReturnEmptyForNullHeader) {
  char *buffer = (char *)calloc(4096, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  const toolbelt::StringHeader *header = nullptr;

  EXPECT_EQ(pb->GetString(header), "");
  EXPECT_EQ(pb->GetStringView(header), "");
  EXPECT_EQ(pb->StringSize(header), 0u);
  EXPECT_EQ(pb->StringData(header), nullptr);

  free(buffer);
}

TEST(BufferTest, StringReadersRejectHeaderOffsetPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  const BufferOffset straddling = static_cast<BufferOffset>(kSize - 2);

  EXPECT_EQ(pb->GetString(straddling), "");
  EXPECT_EQ(pb->GetStringView(straddling), "");
  EXPECT_EQ(pb->StringSize(straddling), 0u);
  EXPECT_EQ(pb->StringData(straddling), nullptr);

  const BufferOffset unset = static_cast<BufferOffset>(0);

  EXPECT_EQ(pb->GetString(unset), "");
  EXPECT_EQ(pb->GetStringView(unset), "");
  EXPECT_EQ(pb->StringSize(unset), 0u);
  EXPECT_EQ(pb->StringData(unset), nullptr);

  free(buffer);
}

TEST(BufferTest, StringWritersRejectHeaderOffsetPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb =
      new (buffer) PayloadBuffer(kSize, /*bitmap_allocator=*/false);
  const uint32_t free_len = pb->FreeList()->length;

  const BufferOffset straddling = static_cast<BufferOffset>(kSize - 2);

  EXPECT_EQ(PayloadBuffer::SetString(&pb, "x", 1, straddling), nullptr);
  EXPECT_TRUE(PayloadBuffer::AllocateString(&pb, 1, straddling).empty());
  PayloadBuffer::ClearString(&pb, straddling);

  const BufferOffset unset = static_cast<BufferOffset>(0);

  EXPECT_EQ(PayloadBuffer::SetString(&pb, "x", 1, unset), nullptr);
  EXPECT_TRUE(PayloadBuffer::AllocateString(&pb, 1, unset).empty());
  PayloadBuffer::ClearString(&pb, unset);

  EXPECT_EQ(pb->FreeList()->length, free_len);

  free(buffer);
}

TEST(BufferTest, AllocateStringReturnsWritableSpanAndStoresOffset) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb =
      new (buffer) PayloadBuffer(kSize, /*bitmap_allocator=*/false);
  PayloadBuffer::AllocateMainMessage(&pb, sizeof(toolbelt::StringHeader));

  absl::Span<char> str = PayloadBuffer::AllocateString(&pb, 3, pb->message);
  ASSERT_EQ(str.size(), 3u);
  EXPECT_NE(*pb->ToAddress<toolbelt::StringHeader>(pb->message),
            BufferOffset(0));

  memcpy(str.data(), "abc", 3);
  EXPECT_EQ(pb->GetString(pb->message), "abc");

  absl::Span<char> grown = PayloadBuffer::AllocateString(&pb, 5, pb->message);
  ASSERT_EQ(grown.size(), 5u);

  memcpy(grown.data(), "abcde", 5);
  EXPECT_EQ(pb->GetString(pb->message), "abcde");

  free(buffer);
}

TEST(BufferTest, StringWithinBoundsRejectsBodyOffsetPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  toolbelt::StringHeader header = static_cast<BufferOffset>(kSize - 2);

  EXPECT_FALSE(pb->StringWithinBounds(&header));

  free(buffer);
}

TEST(BufferTest, StringWithinBoundsRejectsBodyPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  toolbelt::StringHeader header =
      static_cast<BufferOffset>(kSize - sizeof(uint32_t));
  uint32_t declared = 1;
  memcpy(buffer + header, &declared, sizeof(declared));

  EXPECT_FALSE(pb->StringWithinBounds(&header));

  free(buffer);
}

TEST(BufferTest, VectorGetRejectsIndexPastEnd) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  VectorHeader hdr;
  hdr.num_elements = 2;
  hdr.data = static_cast<BufferOffset>(kSize - sizeof(uint32_t));

  EXPECT_EQ(pb->VectorGet<uint32_t>(&hdr, 1), 0u);

  free(buffer);
}

TEST(BufferTest, ToOffsetAndToAddressAgreeOnTrailingExtent) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  uint32_t *p =
      reinterpret_cast<uint32_t *>(reinterpret_cast<char *>(pb) + (kSize - 2));

  EXPECT_EQ(pb->ToAddress<uint32_t>(kSize - 2), nullptr);
  EXPECT_EQ(pb->ToOffset(p), 0u);

  free(buffer);
}

TEST(BufferTest, ToAddressVoidStartOnlyBoundary) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  EXPECT_NE(pb->ToAddress<void>(kSize - 1), nullptr);
  EXPECT_EQ(pb->ToAddress<void>(kSize), nullptr);

  free(buffer);
}

TEST(BufferTest, ToAddressAndToOffsetRejectOutOfRangeInputs) {
  constexpr size_t kSize = 4096;
  char *buffer = (char *)calloc(kSize, 1);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);
  char *other_buffer = (char *)calloc(kSize, 1);

  const BufferOffset far_offset = std::numeric_limits<BufferOffset>::max();
  EXPECT_EQ(pb->ToAddress<uint32_t>(far_offset), nullptr);
  EXPECT_EQ(pb->ToAddress<void>(far_offset), nullptr);

  const uint32_t *foreign_address =
      reinterpret_cast<const uint32_t *>(other_buffer);
  EXPECT_EQ(pb->ToOffset(foreign_address), 0u);

  free(other_buffer);
  free(buffer);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
