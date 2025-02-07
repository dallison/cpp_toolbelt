#include "toolbelt/clock.h"
#include "toolbelt/hexdump.h"
#include "toolbelt/payload_buffer.h"
#include <gtest/gtest.h>
#include <sstream>

using PayloadBuffer = toolbelt::PayloadBuffer;
using BufferOffset = toolbelt::BufferOffset;
using VectorHeader = toolbelt::VectorHeader;
using Resizer = toolbelt::Resizer;

TEST(BufferTest, Simple) {
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(4096);

  void *addr = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_NE(nullptr, addr);
  pb->Free(addr);

  // Allocate again and make sure it's the same address.
  void *addr2 = PayloadBuffer::Allocate(&pb, 16);
  ASSERT_EQ(addr, addr2);
}

TEST(BufferTest, SmallBlockAlloc) {
  char *buffer = (char *)malloc(8192);
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
}

TEST(BufferTest, SmallBlockAllocFree) {
  char *buffer = (char *)malloc(8192);
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
  for (int i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      pb->Free(blocks[i]);
    }
  }
  // Now allocate every 5th block again.
  for (int i = 0; i < blocks.size(); i++) {
    if (i % 5 == 0) {
      size_t size = sizes[i % sizes.size()];
      void *addr = PayloadBuffer::Allocate(&pb, size);
      memset(addr, 0xda, size);
      blocks[i] = addr;
    }
  }
  pb->Dump(std::cout);
  toolbelt::Hexdump(pb, pb->hwm);
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
    char *buffer = (char *)malloc(kSize);
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
    buffer = (char *)malloc(kSize);
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
    char *buffer = (char *)malloc(kSize);
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
      for (int i = prev_size; i < small_blocks.size(); i++) {
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
    buffer = (char *)malloc(kSize);
    pb = new (buffer) PayloadBuffer(kSize);

    // Switch off small block alloctor.
    std::vector<void *> large_blocks;
    uint64_t large_start = toolbelt::Now();
    for (int j = 0; j < 1000; j++) {
      int prev_size = int(large_blocks.size());
      for (int i = 0; i < kNumBlocks; i++) {
        void *addr = PayloadBuffer::Allocate(&pb, 10, false, /*enable_small_block=*/false);
        large_blocks.push_back(addr);
      }
      // Free some of the blocks.
      for (int i = prev_size; i < large_blocks.size(); i++) {
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
  char *buffer = (char *)malloc(kSize);
  PayloadBuffer *pb = new (buffer) PayloadBuffer(kSize);

  std::vector<void *> addrs =
      PayloadBuffer::AllocateMany(&pb, 100, 10, true);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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
  char *buffer = (char *)malloc(4096);
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

TEST(BufferTest, Resizeable) {
  char *buffer = (char *)malloc(512);
  bool resized = false;
  PayloadBuffer *pb = new (buffer)
      PayloadBuffer(256, [&resized](PayloadBuffer **p, size_t old_size, size_t new_size) {
        std::cout << "resize for " << new_size << std::endl;
        *p = reinterpret_cast<PayloadBuffer *>(realloc(*p, new_size));
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
  delete pb;
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
