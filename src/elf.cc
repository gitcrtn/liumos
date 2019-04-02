#include "lib/musl/include/elf.h"
#include "liumos.h"
#include "pmem.h"

struct PhdrInfo {
  const uint8_t* data;
  uint64_t vaddr;
  size_t map_size;
  size_t copy_size;
  void Clear() {
    data = nullptr;
    vaddr = 0;
    map_size = 0;
    copy_size = 0;
  }
};

struct PhdrMappingInfo {
  PhdrInfo code;
  PhdrInfo data;
  void Clear() {
    code.Clear();
    data.Clear();
  }
};

static const Elf64_Ehdr* EnsureLoadable(const uint8_t* buf) {
  if (strncmp(reinterpret_cast<const char*>(buf), ELFMAG, SELFMAG) != 0) {
    PutString("Not an ELF file\n");
    return nullptr;
  }
  if (buf[EI_CLASS] != ELFCLASS64) {
    PutString("Not an ELF Class 64n");
    return nullptr;
  }
  if (buf[EI_DATA] != ELFDATA2LSB) {
    PutString("Not an ELF Data 2LSB");
    return nullptr;
  }
  if (buf[EI_OSABI] != ELFOSABI_SYSV) {
    PutString("Not a SYSV ABI");
    return nullptr;
  }
  const Elf64_Ehdr* ehdr = reinterpret_cast<const Elf64_Ehdr*>(buf);
  if (ehdr->e_type != ET_EXEC) {
    PutString("Not an executable");
    return nullptr;
  }
  if (ehdr->e_machine != EM_X86_64) {
    PutString("Not for x86_64");
    return nullptr;
  }
  return ehdr;
}

static const Elf64_Ehdr* ParseProgramHeader(File& file,
                                            ProcessMappingInfo& proc_map_info,
                                            PhdrMappingInfo& phdr_map_info) {
  proc_map_info.Clear();
  phdr_map_info.Clear();
  const uint8_t* buf = file.GetBuf();
  assert(IsAlignedToPageSize(buf));
  const Elf64_Ehdr* ehdr = EnsureLoadable(buf);
  if (!ehdr)
    return nullptr;

  for (int i = 0; i < ehdr->e_phnum; i++) {
    const Elf64_Phdr* phdr = reinterpret_cast<const Elf64_Phdr*>(
        buf + ehdr->e_phoff + ehdr->e_phentsize * i);
    if (phdr->p_type != PT_LOAD)
      continue;

    assert(IsAlignedToPageSize(phdr->p_align));

    SegmentMapping* seg_map = nullptr;
    PhdrInfo* phdr_info = nullptr;
    if (phdr->p_flags & PF_X) {
      seg_map = &proc_map_info.code;
      phdr_info = &phdr_map_info.code;
    }
    if (phdr->p_flags & PF_W) {
      seg_map = &proc_map_info.data;
      phdr_info = &phdr_map_info.data;
    }
    assert(seg_map);
    uint64_t vaddr = FloorToPageAlignment(phdr->p_vaddr);
    seg_map->Set(vaddr, 0,
                 CeilToPageAlignment(phdr->p_memsz + (phdr->p_vaddr - vaddr)));
    assert(phdr_info);
    phdr_info->data = buf + FloorToPageAlignment(phdr->p_offset);
    phdr_info->vaddr = seg_map->GetVirtAddr();
    phdr_info->map_size = seg_map->GetMapSize();
    phdr_info->copy_size =
        phdr->p_filesz + (phdr->p_vaddr - seg_map->GetVirtAddr());
  }
  return ehdr;
}

static void LoadAndMapSegment(IA_PML4& page_root,
                              SegmentMapping& seg_map,
                              PhdrInfo& phdr_info,
                              uint64_t page_attr) {
  assert(seg_map.GetVirtAddr() == phdr_info.vaddr);
  assert(seg_map.GetMapSize() == phdr_info.map_size);
  assert(seg_map.GetPhysAddr());
  uint8_t* phys_buf = reinterpret_cast<uint8_t*>(seg_map.GetPhysAddr());
  memcpy(phys_buf, phdr_info.data, phdr_info.copy_size);
  bzero(phys_buf + phdr_info.copy_size,
        phdr_info.map_size - phdr_info.copy_size);
  seg_map.Map(page_root, page_attr);
}

static void LoadAndMap(IA_PML4& page_root,
                       ProcessMappingInfo& proc_map_info,
                       PhdrMappingInfo& phdr_map_info,
                       uint64_t base_attr) {
  uint64_t page_attr = kPageAttrPresent | base_attr;

  LoadAndMapSegment(page_root, proc_map_info.code, phdr_map_info.code,
                    page_attr);
  LoadAndMapSegment(page_root, proc_map_info.data, phdr_map_info.data,
                    page_attr | kPageAttrWritable);
  proc_map_info.stack.Map(page_root, page_attr | kPageAttrWritable);
}

Process& LoadELFAndCreateEphemeralProcess(File& file) {
  ExecutionContext& ctx =
      *liumos->kernel_heap_allocator->Alloc<ExecutionContext>();
  ProcessMappingInfo& map_info = ctx.GetProcessMappingInfo();
  PhdrMappingInfo phdr_map_info;
  IA_PML4& user_page_table = CreatePageTable();

  const Elf64_Ehdr* ehdr = ParseProgramHeader(file, map_info, phdr_map_info);
  assert(ehdr);

  map_info.code.SetPhysAddr(liumos->dram_allocator->AllocPages<uint64_t>(
      ByteSizeToPageSize(map_info.code.GetMapSize())));
  map_info.data.SetPhysAddr(liumos->dram_allocator->AllocPages<uint64_t>(
      ByteSizeToPageSize(map_info.data.GetMapSize())));

  const int kNumOfStackPages = 32;
  map_info.stack.Set(
      0xBEEF'0000,
      liumos->dram_allocator->AllocPages<uint64_t>(kNumOfStackPages),
      kNumOfStackPages << kPageSizeExponent);

  map_info.Print();
  LoadAndMap(user_page_table, map_info, phdr_map_info, kPageAttrUser);

  uint8_t* entry_point = reinterpret_cast<uint8_t*>(ehdr->e_entry);

  void* stack_pointer =
      reinterpret_cast<void*>(map_info.stack.GetVirtEndAddr());

  ctx.SetRegisters(reinterpret_cast<void (*)(void)>(entry_point),
                   GDT::kUserCS64Selector, stack_pointer, GDT::kUserDSSelector,
                   reinterpret_cast<uint64_t>(&user_page_table),
                   kRFlagsInterruptEnable,
                   liumos->kernel_heap_allocator->AllocPages<uint64_t>(
                       kKernelStackPagesForEachProcess,
                       kPageAttrPresent | kPageAttrWritable) +
                       kPageSize * kKernelStackPagesForEachProcess);
  Process& proc = liumos->proc_ctrl->Create();
  proc.InitAsEphemeralProcess(ctx);
  return proc;
}

Process& LoadELFAndCreatePersistentProcess(File& file,
                                           PersistentMemoryManager& pmem) {
  constexpr uint64_t kUserStackBaseAddr = 0xBEEF'0000;
  const int kNumOfStackPages = 32;
  PersistentProcessInfo& pp_info = *pmem.AllocPersistentProcessInfo();
  pp_info.Init();
  ExecutionContext& ctx = pp_info.GetContext(0);
  pp_info.SetValidContextIndex(0);
  ProcessMappingInfo& map_info = ctx.GetProcessMappingInfo();
  PhdrMappingInfo phdr_map_info;
  IA_PML4& user_page_table = CreatePageTable();

  const Elf64_Ehdr* ehdr = ParseProgramHeader(file, map_info, phdr_map_info);
  assert(ehdr);
  map_info.stack.Set(kUserStackBaseAddr, 0,
                     kNumOfStackPages << kPageSizeExponent);

  map_info.code.AllocSegmentFromPersistentMemory(pmem);
  map_info.data.AllocSegmentFromPersistentMemory(pmem);
  map_info.stack.AllocSegmentFromPersistentMemory(pmem);

  map_info.Print();
  LoadAndMap(user_page_table, map_info, phdr_map_info, kPageAttrUser);

  uint8_t* entry_point = reinterpret_cast<uint8_t*>(ehdr->e_entry);

  void* stack_pointer =
      reinterpret_cast<void*>(map_info.stack.GetVirtEndAddr());

  ctx.SetRegisters(reinterpret_cast<void (*)(void)>(entry_point),
                   GDT::kUserCS64Selector, stack_pointer, GDT::kUserDSSelector,
                   reinterpret_cast<uint64_t>(&user_page_table),
                   kRFlagsInterruptEnable, 0);

  ExecutionContext& working_ctx = pp_info.GetContext(1);
  working_ctx = ctx;
  ProcessMappingInfo& working_ctx_map = working_ctx.GetProcessMappingInfo();
  working_ctx_map.data.AllocSegmentFromPersistentMemory(pmem);
  working_ctx_map.stack.AllocSegmentFromPersistentMemory(pmem);

  return liumos->proc_ctrl->RestoreFromPersistentProcessInfo(pp_info);
}

void LoadKernelELF(File& file) {
  ProcessMappingInfo map_info;
  PhdrMappingInfo phdr_map_info;

  const Elf64_Ehdr* ehdr = ParseProgramHeader(file, map_info, phdr_map_info);
  assert(ehdr);

  map_info.code.SetPhysAddr(liumos->dram_allocator->AllocPages<uint64_t>(
      ByteSizeToPageSize(map_info.code.GetMapSize())));
  map_info.data.SetPhysAddr(liumos->dram_allocator->AllocPages<uint64_t>(
      ByteSizeToPageSize(map_info.data.GetMapSize())));

  constexpr uint64_t kNumOfKernelMainStackPages = 2;
  uint64_t kernel_main_stack_virtual_base = 0xFFFF'FFFF'4000'0000ULL;

  map_info.stack.Set(
      kernel_main_stack_virtual_base,
      liumos->dram_allocator->AllocPages<uint64_t>(kNumOfKernelMainStackPages),
      kNumOfKernelMainStackPages << kPageSizeExponent);

  LoadAndMap(GetKernelPML4(), map_info, phdr_map_info, 0);

  uint8_t* entry_point = reinterpret_cast<uint8_t*>(ehdr->e_entry);
  PutStringAndHex("Entry address: ", entry_point);

  uint64_t kernel_main_stack_pointer =
      kernel_main_stack_virtual_base +
      (kNumOfKernelMainStackPages << kPageSizeExponent);

  JumpToKernel(entry_point, liumos, kernel_main_stack_pointer);
}
