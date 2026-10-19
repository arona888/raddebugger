// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#if OS_MAC && ARCH_ARM64
Test(mac_dmn_shared_cache_protection)
{
  task_dyld_info_data_t dyld = {0};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  kern_return_t kr = task_info(mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyld, &count);
  TestCheck(kr == KERN_SUCCESS);
  if(kr == KERN_SUCCESS)
  {
    struct dyld_all_image_infos *images = (void *)dyld.all_image_info_addr;
    U64 address = arm64_address_from_pointer((U64)images->notification);
    MAC_DMN_Entity self = {.port = mach_task_self()};
    mach_vm_address_t base = 0;
    mach_vm_size_t size = 0;
    vm_region_submap_short_info_data_64_t info = {0};
    B32 found = mac_dmn_leaf_region(&self, address, &base, &size, &info);
    TestCheck(found);
    TestCheck(!info.is_submap && (info.protection & VM_PROT_EXECUTE));
    TestCheck(base <= address && address-base < size);
  }
}

Test(mac_dmn_shared_cache_image_ownership)
{
  Dl_info a = {0}, b = {0};
  B32 found_a = dladdr((void *)&getpid, &a);
  B32 found_b = dladdr((void *)&pthread_create, &b);
  TestCheck(found_a && found_b && a.dli_fbase != b.dli_fbase);
  if(found_a && found_b && a.dli_fbase != b.dli_fbase)
  {
    if((U64)a.dli_fbase > (U64)b.dli_fbase) { Swap(Dl_info, a, b); }
    TestCheck(((struct mach_header_64 *)a.dli_fbase)->flags & MH_DYLIB_IN_CACHE);
    if(mac_dmn == 0) { dmn_init(); }
    MAC_DMN_Entity self = {.port = mach_task_self()};
    DMN_EventList events = {0};
    mac_dmn_image(arena, &events, &self, (U64)a.dli_fbase, (char *)a.dli_fname);
    TestCheck(events.count == 1);
    if(events.first)
    {
      MAC_DMN_Entity *module = mac_dmn_entity(events.first->v.module, MAC_DMN_Kind_Module);
      TestCheck(module != 0);
      if(module)
      {
        Rng1U64 range = r1u64(module->base, module->base + module->info.vsize);
        TestCheck(contains_1u64(range, (U64)a.dli_saddr));
        TestCheck(!contains_1u64(range, (U64)b.dli_saddr));
        mac_dmn_release(module);
      }
    }
  }
}
#endif
