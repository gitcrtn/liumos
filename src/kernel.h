#pragma once

#include "kernel.h"
#include "liumos.h"
#include "paging.h"
#include "phys_page_allocator.h"

KernelPhysPageAllocator& GetKernelPhysPageAllocator();
uint64_t GetKernelStraightMappingBase();
void kprintf(const char* fmt, ...);
void kprintbuf(const char* desc,
               const volatile void* data,
               size_t start,
               size_t end);

template <typename PhysType = uint64_t, typename VirtType>
PhysType v2p(VirtType v) {
  return reinterpret_cast<PhysType>(liumos->kernel_pml4->v2pWithOffset(
      reinterpret_cast<uint64_t>(v),
      liumos->cpu_features->kernel_phys_page_map_begin));
}

template <typename T>
T AllocKernelMemory(uint64_t byte_size) {
  return liumos->kernel_heap_allocator->AllocPages<T>(
      ByteSizeToPageSize(byte_size));
}

template <typename T>
T AllocMemoryForMappedIO(uint64_t byte_size) {
  uint64_t num_of_pages = ByteSizeToPageSize(byte_size);
  return liumos->kernel_heap_allocator->MapPages<T>(
      GetKernelPhysPageAllocator().AllocPages<uint64_t>(num_of_pages),
      num_of_pages, kPageAttrMemMappedIO);
}

template <typename T>
T MapMemoryForIO(uint64_t phys_addr, uint64_t byte_size) {
  return liumos->kernel_heap_allocator->MapPages<T>(
      phys_addr, ByteSizeToPageSize(byte_size), kPageAttrMemMappedIO);
}
