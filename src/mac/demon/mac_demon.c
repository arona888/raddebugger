// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal DMN_Handle
mac_dmn_handle(MAC_DMN_Entity *e)
{
  DMN_Handle h = {0};
  if(e) { h.u32[0] = (U32)(e - mac_dmn->entities); h.u32[1] = e->generation; }
  return h;
}

internal MAC_DMN_Entity *
mac_dmn_entity(DMN_Handle h, MAC_DMN_Kind kind)
{
  MAC_DMN_Entity *e = 0;
  if(h.u32[0] && h.u32[0] < mac_dmn->entity_count)
  {
    MAC_DMN_Entity *candidate = mac_dmn->entities + h.u32[0];
    if(candidate->generation == h.u32[1] && candidate->kind == kind) { e = candidate; }
  }
  return e;
}

internal MAC_DMN_Entity *
mac_dmn_alloc(MAC_DMN_Kind kind, MAC_DMN_Entity *parent)
{
  MAC_DMN_Entity *e = mac_dmn->free_entities;
  if(e) { mac_dmn->free_entities = e->next; }
  else { e = push_array(mac_dmn->arena, MAC_DMN_Entity, 1); mac_dmn->entity_count += 1; }
  U32 generation = e->generation + 1;
  MemoryZeroStruct(e);
  e->generation = generation ? generation : 1;
  e->kind = kind;
  e->parent = parent;
  return e;
}

internal void
mac_dmn_release(MAC_DMN_Entity *e)
{
  if(e->port) { mach_port_deallocate(mach_task_self(), e->port); }
  if(e->exception_port) { mach_port_destroy(mach_task_self(), e->exception_port); }
  if(e->path) { free(e->path); }
  e->kind = MAC_DMN_Kind_Null;
  e->port = 0;
  e->path = 0;
  e->next = mac_dmn->free_entities;
  mac_dmn->free_entities = e;
}

internal U64
mac_dmn_read(MAC_DMN_Entity *p, U64 address, U64 size, void *dst)
{
  U64 total = 0;
  while(p && total < size)
  {
    // Reading one mapped page at a time preserves partial reads across holes.
    mach_vm_size_t got = 0;
    U64 amount = Min(size - total, vm_page_size - ((address + total) & (vm_page_size - 1)));
    kern_return_t kr = mach_vm_read_overwrite(p->port, address + total, amount,
                                             (mach_vm_address_t)((U8 *)dst + total), &got);
    total += got;
    if(kr != KERN_SUCCESS || got != amount) { break; }
  }
  return total;
}

internal B32
mac_dmn_leaf_region(MAC_DMN_Entity *p, U64 address, mach_vm_address_t *base,
                    mach_vm_size_t *size, vm_region_submap_short_info_data_64_t *info)
{
  U32 depth = 0;
  for(;;)
  {
    // The shared cache is a submap. Its outer mapping is read-only even when
    // the leaf containing dyld code is executable; restoring the outer
    // protection after a breakpoint would turn that code into non-executable data.
    *base = address;
    mach_msg_type_number_t count = VM_REGION_SUBMAP_SHORT_INFO_COUNT_64;
    kern_return_t kr = mach_vm_region_recurse(p->port, base, size, &depth,
                                             (vm_region_recurse_info_t)info, &count);
    if(kr != KERN_SUCCESS || *base > address || address-*base >= *size) { return 0; }
    if(!info->is_submap) { return 1; }
    depth += 1;
  }
}

internal B32
mac_dmn_write(MAC_DMN_Entity *p, U64 address, U64 size, void *src)
{
  if(!p || address + size < address) { return 0; }
  U64 done = 0;
  while(done < size)
  {
    mach_vm_address_t region = address + done;
    mach_vm_size_t region_size = 0;
    vm_region_submap_short_info_data_64_t info = {0};
    if(!mac_dmn_leaf_region(p, address+done, &region, &region_size, &info)) { return 0; }
    kern_return_t kr = KERN_SUCCESS;
    U64 amount = Min(size - done, region + region_size - (address + done));
    amount = Min(amount, (U64)max_U32);
    U64 lo = (address + done) & ~((U64)vm_page_size - 1);
    U64 hi = (address + done + amount + vm_page_size - 1) & ~((U64)vm_page_size - 1);
    B32 changed = !(info.protection & VM_PROT_WRITE);
    if(changed)
    {
      // VM_PROT_COPY creates a private COW mapping for signed executable text.
      // Do not request W+X: Apple Silicon enforces W^X even for debuggers.
      kr = mach_vm_protect(p->port, lo, hi - lo, 0,
                           (info.protection | VM_PROT_WRITE | VM_PROT_COPY) & ~VM_PROT_EXECUTE);
    }
    if(kr != KERN_SUCCESS) { return 0; }
    kr = mach_vm_write(p->port, address + done, (vm_offset_t)((U8 *)src + done), (mach_msg_type_number_t)amount);
    kern_return_t restore = KERN_SUCCESS;
    if(changed) { restore = mach_vm_protect(p->port, lo, hi - lo, 0, info.protection); }
    if(kr != KERN_SUCCESS || restore != KERN_SUCCESS) { return 0; }
    if(info.protection & VM_PROT_EXECUTE)
    {
      vm_machine_attribute_val_t value = MATTR_VAL_CACHE_FLUSH;
      mach_vm_machine_attribute(p->port, address + done, amount, MATTR_CACHE, &value);
    }
    done += amount;
  }
  return 1;
}

internal B32
mac_dmn_regs(MAC_DMN_Entity *t, ARM64_RegBlock *regs, B32 write)
{
  if(!t) { return 0; }
  arm_thread_state64_t state = {0};
  arm_neon_state64_t neon = {0};
  mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
  if(thread_get_state(t->port, ARM_THREAD_STATE64, (thread_state_t)&state, &count) != KERN_SUCCESS) { return 0; }
  count = ARM_NEON_STATE64_COUNT;
  if(thread_get_state(t->port, ARM_NEON_STATE64, (thread_state_t)&neon, &count) != KERN_SUCCESS) { return 0; }
  if(!write)
  {
    MemoryCopy(regs->x, state.__x, sizeof(state.__x));
    regs->x[29] = arm_thread_state64_get_fp(state);
    regs->x[30] = arm_thread_state64_get_lr(state);
    regs->sp = arm_thread_state64_get_sp(state);
    regs->pc = arm_thread_state64_get_pc(state);
    regs->cpsr = state.__cpsr;
    MemoryCopy(regs->v, neon.__v, sizeof(neon.__v));
    regs->fpsr = neon.__fpsr;
    regs->fpcr = neon.__fpcr;
    return 1;
  }
  MemoryCopy(state.__x, regs->x, sizeof(state.__x));
  // Keep opaque PAC metadata returned by thread_get_state. The SDK accessors
  // sign modified control pointers correctly on arm64e hosts.
  if(arm_thread_state64_get_fp(state) != regs->x[29]) { arm_thread_state64_set_fp(state, regs->x[29]); }
  if(arm_thread_state64_get_lr(state) != regs->x[30]) { arm_thread_state64_set_lr_fptr(state, (void (*)(void))(uintptr_t)regs->x[30]); }
  if(arm_thread_state64_get_sp(state) != regs->sp) { arm_thread_state64_set_sp(state, regs->sp); }
  if(arm_thread_state64_get_pc(state) != regs->pc) { arm_thread_state64_set_pc_fptr(state, (void (*)(void))(uintptr_t)regs->pc); }
  state.__cpsr = (U32)regs->cpsr;
  MemoryCopy(neon.__v, regs->v, sizeof(neon.__v));
  neon.__fpsr = regs->fpsr;
  neon.__fpcr = regs->fpcr;
  return thread_set_state(t->port, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT) == KERN_SUCCESS &&
         thread_set_state(t->port, ARM_NEON_STATE64, (thread_state_t)&neon, ARM_NEON_STATE64_COUNT) == KERN_SUCCESS;
}

internal B32
mac_dmn_write_pc(MAC_DMN_Entity *t, U64 pc)
{
  arm_thread_state64_t state = {0};
  mach_msg_type_number_t count = ARM_THREAD_STATE64_COUNT;
  if(thread_get_state(t->port, ARM_THREAD_STATE64, (thread_state_t)&state, &count) != KERN_SUCCESS) { return 0; }
  arm_thread_state64_set_pc_fptr(state, (void (*)(void))(uintptr_t)pc);
  return thread_set_state(t->port, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT) == KERN_SUCCESS;
}

internal DMN_Event *
mac_dmn_event(Arena *arena, DMN_EventList *events, DMN_EventKind kind, MAC_DMN_Entity *p, MAC_DMN_Entity *t)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = kind;
  e->process = mac_dmn_handle(p);
  e->thread = mac_dmn_handle(t);
  e->arch = Arch_arm64;
  if(t)
  {
    ARM64_RegBlock regs = {0};
    if(mac_dmn_regs(t, &regs, 0)) { e->instruction_pointer = regs.pc; e->stack_pointer = regs.sp; }
  }
  return e;
}

internal void
mac_dmn_error(Arena *arena, DMN_EventList *events, char *message)
{
  DMN_Event *e = dmn_event_list_push(arena, events);
  e->kind = DMN_EventKind_Error;
  e->error_kind = DMN_ErrorKind_UnexpectedFailure;
  e->string = str8_copy(arena, str8_cstring(message));
}

internal void
mac_dmn_stop_all(void)
{
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *p = mac_dmn->entities + i;
    if(p->kind == MAC_DMN_Kind_Process && !p->suspended && !p->detached)
    {
      if(task_suspend(p->port) == KERN_SUCCESS) { p->suspended = 1; }
    }
  }
}

internal MAC_DMN_Entity *
mac_dmn_thread(MAC_DMN_Entity *p, mach_port_t port)
{
  thread_identifier_info_data_t info = {0};
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  if(thread_info(port, THREAD_IDENTIFIER_INFO, (thread_info_t)&info, &count) != KERN_SUCCESS) { return 0; }
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn->entities + i;
    if(t->kind == MAC_DMN_Kind_Thread && t->parent == p && t->id == info.thread_id) { return t; }
  }
  MAC_DMN_Entity *t = mac_dmn_alloc(MAC_DMN_Kind_Thread, p);
  t->id = info.thread_id;
  t->port = port;
  mach_port_mod_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, 1);
  return t;
}

internal void
mac_dmn_sync_threads(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *p)
{
  thread_act_array_t ports = 0;
  mach_msg_type_number_t count = 0;
  if(task_threads(p->port, &ports, &count) != KERN_SUCCESS) { return; }
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn->entities + i;
    if(t->kind == MAC_DMN_Kind_Thread && t->parent == p) { t->seen = 0; }
  }
  for(U32 i = 0; i < count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn_thread(p, ports[i]);
    if(t)
    {
      t->seen = 1;
      if(!t->announced)
      {
        t->announced = 1;
        DMN_Event *e = mac_dmn_event(arena, events, DMN_EventKind_CreateThread, p, t);
        e->code = (U32)t->id;
        U64 sp = e->stack_pointer;
        e->stack_pointer = 0;
        mach_vm_address_t stack = sp;
        mach_vm_size_t stack_size = 0;
        vm_region_basic_info_data_64_t stack_info = {0};
        mach_msg_type_number_t stack_count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = 0;
        if(sp && mach_vm_region(p->port, &stack, &stack_size, VM_REGION_BASIC_INFO_64,
                                (vm_region_info_t)&stack_info, &stack_count, &object) == KERN_SUCCESS &&
           stack <= sp && sp-stack < stack_size)
        { e->stack_pointer = stack+stack_size; }
        if(object) { mach_port_deallocate(mach_task_self(), object); }
        thread_identifier_info_data_t identifier = {0};
        mach_msg_type_number_t identifier_count = THREAD_IDENTIFIER_INFO_COUNT;
        if(thread_info(t->port, THREAD_IDENTIFIER_INFO, (thread_info_t)&identifier, &identifier_count) == KERN_SUCCESS)
        { e->tls_root_vaddr = arm64_address_from_pointer(identifier.thread_handle); }
        thread_extended_info_data_t ext = {0};
        mach_msg_type_number_t ext_count = THREAD_EXTENDED_INFO_COUNT;
        if(thread_info(t->port, THREAD_EXTENDED_INFO, (thread_info_t)&ext, &ext_count) == KERN_SUCCESS && ext.pth_name[0])
        {
          DMN_Event *name = mac_dmn_event(arena, events, DMN_EventKind_SetThreadName, p, t);
          name->string = str8_copy(arena, str8_cstring(ext.pth_name));
        }
      }
    }
    mach_port_deallocate(mach_task_self(), ports[i]);
  }
  mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)ports, count * sizeof(*ports));
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn->entities + i;
    if(t->kind == MAC_DMN_Kind_Thread && t->parent == p && !t->seen)
    {
      mac_dmn_event(arena, events, DMN_EventKind_ExitThread, p, t);
      mac_dmn_release(t);
    }
  }
}

internal U64
mac_dmn_image(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *p, U64 address, char *path)
{
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *m = mac_dmn->entities + i;
    if(m->kind == MAC_DMN_Kind_Module && m->parent == p && m->base == address)
    { m->seen = 1; return m->entry; }
  }
  struct mach_header_64 header = {0};
  if(mac_dmn_read(p, address, sizeof(header), &header) != sizeof(header) || header.magic != MH_MAGIC_64 ||
     header.cputype != CPU_TYPE_ARM64 || header.sizeofcmds > MB(16)) { return 0; }
  Temp scratch = scratch_begin(&arena, 1);
  U8 *commands = push_array(scratch.arena, U8, header.sizeofcmds);
  if(mac_dmn_read(p, address + sizeof(header), header.sizeofcmds, commands) != header.sizeofcmds)
  { scratch_end(scratch); return 0; }
  U64 preferred = 0, end = 0, entryoff = 0, tls = 0, tls_size = 0;
  Guid uuid = {0};
  B32 shared_cache = (header.flags & MH_DYLIB_IN_CACHE) != 0;
  for(U64 off = 0, idx = 0; idx < header.ncmds && off + sizeof(struct load_command) <= header.sizeofcmds; idx += 1)
  {
    struct load_command *lc = (struct load_command *)(commands + off);
    if(lc->cmdsize < sizeof(*lc) || off + lc->cmdsize > header.sizeofcmds) { break; }
    if(lc->cmd == LC_SEGMENT_64 && lc->cmdsize >= sizeof(struct segment_command_64))
    {
      struct segment_command_64 *s = (struct segment_command_64 *)lc;
      if(!strncmp(s->segname, "__TEXT", 16))
      {
        preferred = s->vmaddr;
        if(shared_cache) { end = s->vmaddr + s->vmsize; }
      }
      // Cache images share distant DATA/LINKEDIT mappings. Including those in
      // an image's IP lookup span incorrectly claims other dylibs' code.
      if(!shared_cache && strncmp(s->segname, "__PAGEZERO", 16)) { end = Max(end, s->vmaddr + s->vmsize); }
      struct section_64 *sections = (struct section_64 *)(s + 1);
      if(sizeof(*s) + (U64)s->nsects * sizeof(*sections) <= lc->cmdsize)
      {
        for(U32 j = 0; j < s->nsects; j += 1)
        {
          if((sections[j].flags & SECTION_TYPE) == S_THREAD_LOCAL_VARIABLES)
          { tls = sections[j].addr; tls_size = sections[j].size; }
        }
      }
    }
    else if(lc->cmd == LC_MAIN && lc->cmdsize >= sizeof(struct entry_point_command))
    { entryoff = ((struct entry_point_command *)lc)->entryoff; }
    else if(lc->cmd == LC_UUID && lc->cmdsize >= sizeof(struct uuid_command))
    { MemoryCopy(&uuid, ((struct uuid_command *)lc)->uuid, 16); }
    off += lc->cmdsize;
  }
  MAC_DMN_Entity *m = mac_dmn_alloc(MAC_DMN_Kind_Module, p);
  m->base = address;
  m->entry = entryoff ? address + entryoff : 0;
  m->seen = 1;
  m->path = strdup(path ? path : "");
  m->tls_descriptors = tls ? address + tls - preferred : 0;
  m->tls_descriptors_size = tls_size;
  m->info.arch = Arch_arm64;
  m->info.vsize = end >= preferred ? end - preferred : 0;
  m->info.entry_point_voff = entryoff;
  m->info.module_path = str8_cstring(m->path);
  // The converter opens the executable and finds a UUID-matching dSYM itself.
  // Keeping the executable key also retains its mapped sections and source hash.
  m->info.debug_info_path = m->info.module_path;
  m->info.debug_info_guid = uuid;
  struct stat st = {0};
  if(stat(m->path, &st) == 0) { m->info.debug_info_timestamp = mac_dense_time(st.st_mtimespec); }
  DMN_Event *e = mac_dmn_event(arena, events, DMN_EventKind_LoadModule, p, 0);
  e->module = mac_dmn_handle(m);
  e->address = address;
  e->size = m->info.vsize;
  e->string = str8_copy(arena, m->info.module_path);
  e->module_info = push_array(arena, DMN_ModuleInfo, 1);
  *e->module_info = m->info;
  e->module_info->module_path = e->string;
  e->module_info->debug_info_path = e->string;
  scratch_end(scratch);
  return m->entry;
}

internal void
mac_dmn_sync_modules(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *p)
{
  task_dyld_info_data_t task_info_data = {0};
  mach_msg_type_number_t count = TASK_DYLD_INFO_COUNT;
  if(task_info(p->port, TASK_DYLD_INFO, (task_info_t)&task_info_data, &count) != KERN_SUCCESS) { return; }
  struct dyld_all_image_infos infos = {0};
  U64 info_size = Min((U64)sizeof(infos), task_info_data.all_image_info_size);
  B32 valid = info_size >= offsetof(struct dyld_all_image_infos, dyldImageLoadAddress) + sizeof(infos.dyldImageLoadAddress) &&
              mac_dmn_read(p, task_info_data.all_image_info_addr, info_size, &infos) == info_size;
  // Before dyld publishes its image array, notification can still contain an
  // encoded chained fixup rather than a callable address. The entry-point trap
  // covers this startup interval; only consume relocated notifier pointers.
  if(valid && infos.infoArray && infos.infoArrayCount && infos.notification)
  {
    U64 notifier = arm64_address_from_pointer((U64)infos.notification);
    U32 instruction = 0;
    if(mac_dmn_read(p, notifier, sizeof(instruction), &instruction) == sizeof(instruction))
    { p->notifier = notifier; }
  }
  if(valid && infos.infoArray && infos.infoArrayCount < 65536)
  {
    Temp scratch = scratch_begin(&arena, 1);
    struct dyld_image_info *images = push_array(scratch.arena, struct dyld_image_info, infos.infoArrayCount);
    U64 bytes = infos.infoArrayCount * sizeof(*images);
    if(mac_dmn_read(p, (U64)infos.infoArray, bytes, images) == bytes)
    {
      for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
      {
        MAC_DMN_Entity *m = mac_dmn->entities + i;
        if(m->kind == MAC_DMN_Kind_Module && m->parent == p) { m->seen = 0; }
      }
      for(U32 i = 0; i < infos.infoArrayCount; i += 1)
      {
        char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
        U64 n = mac_dmn_read(p, (U64)images[i].imageFilePath, sizeof(path) - 1, path);
        path[Min(n, sizeof(path) - 1)] = 0;
        mac_dmn_image(arena, events, p, (U64)images[i].imageLoadAddress, path);
      }
      if(infos.dyldImageLoadAddress)
      { mac_dmn_image(arena, events, p, (U64)infos.dyldImageLoadAddress, "/usr/lib/dyld"); }
      for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
      {
        MAC_DMN_Entity *m = mac_dmn->entities + i;
        if(m->kind == MAC_DMN_Kind_Module && m->parent == p && !m->seen)
        {
          DMN_Event *e = mac_dmn_event(arena, events, DMN_EventKind_UnloadModule, p, 0);
          e->module = mac_dmn_handle(m);
          mac_dmn_release(m);
        }
      }
    }
    scratch_end(scratch);
  }
  // At a spawn's initial suspended stop dyld has not published its image array.
  // Find the main Mach header from mapped regions so user breakpoints can already
  // be resolved before any initializer or entry-point instruction is executed.
  B32 have_main = 0;
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *m = mac_dmn->entities + i;
    if(m->kind == MAC_DMN_Kind_Module && m->parent == p && m->path && p->path && !strcmp(m->path, p->path))
    { have_main = 1; if(m->entry) { p->entry = m->entry; } break; }
  }
  if(!have_main)
  {
    mach_vm_address_t address = 0;
    while(1)
    {
      mach_vm_size_t size = 0;
      vm_region_basic_info_data_64_t region = {0};
      mach_msg_type_number_t n = VM_REGION_BASIC_INFO_COUNT_64;
      mach_port_t object = 0;
      kern_return_t kr = mach_vm_region(p->port, &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&region, &n, &object);
      if(object) { mach_port_deallocate(mach_task_self(), object); }
      if(kr != KERN_SUCCESS || !size) { break; }
      if(region.protection & VM_PROT_READ)
      {
        struct mach_header_64 header = {0};
        if(mac_dmn_read(p, address, sizeof(header), &header) == sizeof(header) && header.magic == MH_MAGIC_64 && header.filetype == MH_EXECUTE)
        { p->entry = mac_dmn_image(arena, events, p, address, p->path); break; }
      }
      if(address + size <= address) { break; }
      address += size;
    }
  }
}

// Forward an unhandled exception to the exact task handler displaced on attach.
// This is a small MIG client for both exc (32-bit) and mach_exc (64-bit) codes;
// the public SDK does not ship the mach_exc MIG header.
internal kern_return_t
mac_dmn_forward(MAC_DMN_Entity *p, MAC_DMN_Exception *exception)
{
  kern_return_t result = KERN_FAILURE;
  for(U32 i = 0; i < p->old_count; i += 1)
  {
    if(!(p->old_masks[i] & (1u << exception->exception)) || !p->old_ports[i]) { continue; }
    exception_behavior_t behavior = p->old_behaviors[i] & ~MACH_EXCEPTION_CODES;
    B32 wide = !!(p->old_behaviors[i] & MACH_EXCEPTION_CODES);
    if(behavior != EXCEPTION_DEFAULT && behavior != EXCEPTION_STATE && behavior != EXCEPTION_STATE_IDENTITY) { continue; }
    union { U64 align; U8 bytes[16384]; } buffer = {0};
    mach_msg_header_t *head = (mach_msg_header_t *)buffer.bytes;
    U8 *at = buffer.bytes + sizeof(*head);
    head->msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
    head->msgh_remote_port = p->old_ports[i];
    if(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &head->msgh_local_port) != KERN_SUCCESS) { continue; }
    head->msgh_id = (wide ? 2405 : 2401) + (behavior == EXCEPTION_STATE ? 1 : behavior == EXCEPTION_STATE_IDENTITY ? 2 : 0);
    if(behavior != EXCEPTION_STATE)
    {
      head->msgh_bits |= MACH_MSGH_BITS_COMPLEX;
      mach_msg_body_t body = {2};
      MemoryCopy(at, &body, sizeof(body)); at += sizeof(body);
      mach_msg_port_descriptor_t port = {0};
      port.type = MACH_MSG_PORT_DESCRIPTOR;
      port.disposition = MACH_MSG_TYPE_COPY_SEND;
      port.name = exception->thread.name;
      MemoryCopy(at, &port, sizeof(port)); at += sizeof(port);
      port.name = p->port;
      MemoryCopy(at, &port, sizeof(port)); at += sizeof(port);
    }
    MemoryCopy(at, &NDR_record, sizeof(NDR_record)); at += sizeof(NDR_record);
    MemoryCopy(at, &exception->exception, 4); at += 4;
    MemoryCopy(at, &exception->code_count, 4); at += 4;
    for(U32 c = 0; c < exception->code_count; c += 1)
    {
      if(wide) { MemoryCopy(at, &exception->code[c], 8); at += 8; }
      else { U32 value = (U32)exception->code[c]; MemoryCopy(at, &value, 4); at += 4; }
    }
    int flavor = p->old_flavors[i];
    if(behavior != EXCEPTION_DEFAULT)
    {
      thread_state_data_t state = {0};
      mach_msg_type_number_t count = THREAD_STATE_MAX;
      result = thread_get_state(exception->thread.name, flavor, state, &count);
      if(result != KERN_SUCCESS) { mach_port_destroy(mach_task_self(), head->msgh_local_port); continue; }
      MemoryCopy(at, &flavor, 4); at += 4;
      MemoryCopy(at, &count, 4); at += 4;
      MemoryCopy(at, state, count * sizeof(natural_t)); at += count * sizeof(natural_t);
    }
    mach_port_t reply_port = head->msgh_local_port;
    head->msgh_size = (mach_msg_size_t)(at - buffer.bytes);
    result = mach_msg(head, MACH_SEND_MSG | MACH_RCV_MSG, head->msgh_size, sizeof(buffer.bytes), reply_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    if(result == KERN_SUCCESS && head->msgh_size >= sizeof(*head) + sizeof(NDR_record_t) + 4)
    {
      at = buffer.bytes + sizeof(*head) + sizeof(NDR_record_t);
      MemoryCopy(&result, at, 4); at += 4;
      if(result == KERN_SUCCESS && behavior != EXCEPTION_DEFAULT && head->msgh_size >= (U64)(at - buffer.bytes) + 8)
      {
        mach_msg_type_number_t count = 0;
        MemoryCopy(&flavor, at, 4); at += 4;
        MemoryCopy(&count, at, 4); at += 4;
        if(count <= THREAD_STATE_MAX && head->msgh_size >= (U64)(at - buffer.bytes) + count * 4)
        { result = thread_set_state(exception->thread.name, flavor, (thread_state_t)at, count); }
        else { result = KERN_FAILURE; }
      }
    }
    mach_port_destroy(mach_task_self(), reply_port);
    if(result == KERN_SUCCESS) { break; }
  }
  return result;
}

internal B32
mac_dmn_valid_exception(MAC_DMN_Exception *request)
{
  return request->head.msgh_id == 2405 &&
         (request->head.msgh_bits & MACH_MSGH_BITS_COMPLEX) &&
         request->body.msgh_descriptor_count == 2 && request->code_count <= 2 &&
         request->head.msgh_size >= offsetof(MAC_DMN_Exception, code) + request->code_count * 8;
}

internal void
mac_dmn_reply(MAC_DMN_Entity *owner, B32 ignore)
{
  if(!owner->pending_exception) { return; }
  MAC_DMN_Entity *p = owner->kind == MAC_DMN_Kind_Thread ? owner->parent : owner;
  MAC_DMN_Exception *request = &owner->exception;
  kern_return_t result = KERN_SUCCESS;
  if(request->exception == EXC_SOFTWARE && request->code_count >= 2 && request->code[0] == EXC_SOFT_SIGNAL)
  {
    int signo = (!owner->exception_is_user || ignore) ? 0 : (int)request->code[1];
    if(!p->detached && ptrace(PT_THUPDATE, (pid_t)p->id, (caddr_t)(uintptr_t)request->thread.name, signo) < 0)
    { result = KERN_FAILURE; }
  }
  else if(owner->exception_is_user && !ignore) { result = mac_dmn_forward(p, request); }
  __Reply__exception_raise_t reply = {0};
  reply.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
  reply.Head.msgh_size = sizeof(reply);
  reply.Head.msgh_remote_port = request->head.msgh_remote_port;
  reply.Head.msgh_id = request->head.msgh_id + 100;
  reply.NDR = NDR_record;
  reply.RetCode = result;
  mach_msg(&reply.Head, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  mach_port_deallocate(mach_task_self(), request->thread.name);
  mach_port_deallocate(mach_task_self(), request->task.name);
  owner->pending_exception = 0;
}

internal void
dmn_init(void)
{
  Arena *state_arena = arena_alloc();
  mac_dmn = push_array(state_arena, MAC_DMN_State, 1);
  mac_dmn->arena = arena_alloc(.reserve_size = GB(4), .commit_size = KB(64), .flags = ArenaFlag_NoChain);
  mac_dmn->entities = push_array(mac_dmn->arena, MAC_DMN_Entity, 1);
  mac_dmn->entity_count = 1;
  mac_dmn->access_mutex = mutex_alloc();
  mac_dmn->halt_mutex = mutex_alloc();
  mac_dmn->kqueue = kqueue();
  mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &mac_dmn->exception_port);
  mach_port_insert_right(mach_task_self(), mac_dmn->exception_port, mac_dmn->exception_port, MACH_MSG_TYPE_MAKE_SEND);
  mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_PORT_SET, &mac_dmn->exception_port_set);
  mach_port_move_member(mach_task_self(), mac_dmn->exception_port, mac_dmn->exception_port_set);
}

internal DMN_CtrlCtx *
dmn_ctrl_begin(void)
{
  mac_dmn_control_thread = 1;
  return &mac_dmn->context;
}

internal void
dmn_ctrl_exclusive_access_begin(void)
{
  mutex_take(mac_dmn->access_mutex);
  mac_dmn->running = 1;
  mutex_drop(mac_dmn->access_mutex);
}

internal void
dmn_ctrl_exclusive_access_end(void)
{
  mutex_take(mac_dmn->access_mutex);
  mac_dmn->running = 0;
  mutex_drop(mac_dmn->access_mutex);
}

internal B32
dmn_access_open(void)
{
  if(mac_dmn_control_thread) { return 1; }
  if(mac_dmn_access_depth++ == 0)
  {
    mutex_take(mac_dmn->access_mutex);
    mac_dmn_access_allowed = !mac_dmn->running;
  }
  return mac_dmn_access_allowed;
}

internal void
dmn_access_close(void)
{
  if(!mac_dmn_control_thread && --mac_dmn_access_depth == 0) { mutex_drop(mac_dmn->access_mutex); }
}

internal B32
mac_dmn_attach(U32 pid, B32 launched)
{
  mach_port_t task = 0;
  kern_return_t kr = task_for_pid(mach_task_self(), (int)pid, &task);
  if(kr != KERN_SUCCESS)
  {
    log_user_errorf("task_for_pid(%u): %s. Sign the debugger with com.apple.security.cs.debugger, authorize Developer Tools, and enable get-task-allow on hardened targets.", pid, mach_error_string(kr));
    return 0;
  }
  if(task_suspend(task) != KERN_SUCCESS) { mach_port_deallocate(mach_task_self(), task); return 0; }
  MAC_DMN_Entity *p = mac_dmn_alloc(MAC_DMN_Kind_Process, 0);
  p->port = task;
  p->id = pid;
  p->suspended = 1;
  p->launched = launched;
  p->initial_sigstop = 1;
  p->suppress_spawn_sigcont = launched;
  p->old_count = EXC_TYPES_COUNT;
  exception_mask_t mask = EXC_MASK_ALL & ~(EXC_MASK_RESOURCE | EXC_MASK_GUARD);
  kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &p->exception_port);
  if(kr == KERN_SUCCESS)
  { kr = mach_port_insert_right(mach_task_self(), p->exception_port, p->exception_port, MACH_MSG_TYPE_MAKE_SEND); }
  if(kr == KERN_SUCCESS)
  { kr = mach_port_move_member(mach_task_self(), p->exception_port, mac_dmn->exception_port_set); }
  if(kr == KERN_SUCCESS)
  { kr = task_get_exception_ports(task, mask, p->old_masks, &p->old_count, p->old_ports, p->old_behaviors, p->old_flavors); }
  if(kr == KERN_SUCCESS)
  { kr = task_set_exception_ports(task, mask, p->exception_port, EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES, ARM_THREAD_STATE64); }
  if(kr != KERN_SUCCESS)
  {
    task_resume(task);
    for(U32 i = 0; i < p->old_count; i += 1) { if(p->old_ports[i]) { mach_port_deallocate(mach_task_self(), p->old_ports[i]); } }
    mac_dmn_release(p);
    return 0;
  }
  if(ptrace(PT_ATTACHEXC, (pid_t)pid, 0, 0) < 0)
  {
    for(U32 i = 0; i < p->old_count; i += 1)
    {
      task_set_exception_ports(task, p->old_masks[i], p->old_ports[i], p->old_behaviors[i], p->old_flavors[i]);
      if(p->old_ports[i]) { mach_port_deallocate(mach_task_self(), p->old_ports[i]); }
    }
    task_resume(task);
    mac_dmn_release(p);
    return 0;
  }
  // PT_ATTACHEXC consumes one task-suspend count. A launched process retains
  // POSIX_SPAWN_START_SUSPENDED's count; an attached running process needs ours
  // re-established before reporting its initial stopped state.
  if(!launched) { p->suspended = task_suspend(task) == KERN_SUCCESS; }
  // PT_ATTACHEXC reports its SIGSTOP through the Mach exception port. Do not
  // waitpid here: with a suspended task that signal cannot be delivered yet.
  char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
  proc_pidpath((int)pid, path, sizeof(path));
  p->path = strdup(path);
  struct kevent change;
  EV_SET(&change, pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR, NOTE_EXIT | NOTE_EXITSTATUS, 0, 0);
  kevent(mac_dmn->kqueue, &change, 1, 0, 0, 0);
  if(launched) { kill((pid_t)pid, SIGCONT); }
  return 1;
}

internal U32
dmn_ctrl_launch(DMN_CtrlCtx *ctx, ProcessLaunchParams *params)
{
  if(ctx != &mac_dmn->context || !mac_dmn_control_thread || !params->cmd_line.first) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  char **argv = push_array(scratch.arena, char *, params->cmd_line.node_count + 1);
  U64 argc = 0;
  for(String8Node *n = params->cmd_line.first; n; n = n->next)
  { argv[argc++] = (char *)str8_copy(scratch.arena, n->string).str; }
  String8List inherited = {0};
  if(params->inherit_env) { inherited = get_process_info()->environment; }
  char **envp = push_array(scratch.arena, char *, inherited.node_count + params->env.node_count + 1);
  U64 envc = 0;
  // Explicit values replace inherited variables instead of appending duplicate
  // keys (getenv normally returns the first, not the last, matching entry).
  for(String8Node *n = inherited.first; n; n = n->next)
  {
    U64 key_size = 0;
    while(key_size < n->string.size && n->string.str[key_size] != '=') { key_size += 1; }
    B32 overridden = 0;
    for(String8Node *v = params->env.first; v; v = v->next)
    {
      if(v->string.size > key_size && v->string.str[key_size] == '=' &&
         MemoryMatch(v->string.str, n->string.str, key_size)) { overridden = 1; break; }
    }
    if(!overridden) { envp[envc++] = (char *)str8_copy(scratch.arena, n->string).str; }
  }
  for(String8Node *n = params->env.first; n; n = n->next)
  { envp[envc++] = (char *)str8_copy(scratch.arena, n->string).str; }
  posix_spawnattr_t attr;
  posix_spawn_file_actions_t actions;
  posix_spawnattr_init(&attr);
  posix_spawn_file_actions_init(&actions);
  short flags = POSIX_SPAWN_START_SUSPENDED | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
  sigset_t empty, defaults;
  sigemptyset(&empty);
  sigfillset(&defaults);
  posix_spawnattr_setsigmask(&attr, &empty);
  posix_spawnattr_setsigdefault(&attr, &defaults);
  if(!MemoryIsZeroStruct(&params->process_group))
  {
    flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setpgroup(&attr, 0);
  }
  posix_spawnattr_setflags(&attr, flags);
  if(params->path.size)
  { posix_spawn_file_actions_addchdir_np(&actions, (char *)str8_copy(scratch.arena, params->path).str); }
  File files[3] = {params->stdin_file, params->stdout_file, params->stderr_file};
  for(int i = 0; i < 3; i += 1)
  {
    if(files[i].u64[0]) { posix_spawn_file_actions_adddup2(&actions, (int)files[i].u64[0], i); }
    else if(params->consoleless) { posix_spawn_file_actions_addopen(&actions, i, "/dev/null", i ? O_WRONLY : O_RDONLY, 0); }
  }
  pid_t pid = 0;
  int error = posix_spawnp(&pid, argv[0], &actions, &attr, argv, envp);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attr);
  if(!error && !mac_dmn_attach((U32)pid, 1))
  { kill(pid, SIGKILL); waitpid(pid, 0, 0); pid = 0; }
  if(error) { log_user_errorf("Could not launch %s: %s", argv[0], strerror(error)); pid = 0; }
  if(pid && params->process_group.u64[0])
  {
    MAC_ProcessGroup *group = (MAC_ProcessGroup *)params->process_group.u64[0];
    group->pid = pid;
  }
  scratch_end(scratch);
  return (U32)pid;
}

internal B32
dmn_ctrl_attach(DMN_CtrlCtx *ctx, U32 pid)
{
  if(ctx != &mac_dmn->context || !mac_dmn_control_thread) { return 0; }
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    if(mac_dmn->entities[i].kind == MAC_DMN_Kind_Process && mac_dmn->entities[i].id == pid) { return 0; }
  }
  return mac_dmn_attach(pid, 0);
}

internal B32
dmn_ctrl_kill(DMN_CtrlCtx *ctx, DMN_Handle process, U32 exit_code)
{
  MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
  if(ctx != &mac_dmn->context || !mac_dmn_control_thread || !p) { return 0; }
  p->exit_code = exit_code;
  B32 ok = kill((pid_t)p->id, SIGKILL) == 0;
  if(ok)
  {
    for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
    {
      MAC_DMN_Entity *t = mac_dmn->entities + i;
      if(t->kind == MAC_DMN_Kind_Thread && t->parent == p) { mac_dmn_reply(t, 1); }
    }
    if(p->suspended) { task_resume(p->port); p->suspended = 0; }
  }
  return ok;
}

internal B32
dmn_ctrl_detach(DMN_CtrlCtx *ctx, DMN_Handle process)
{
  MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
  if(ctx != &mac_dmn->context || !mac_dmn_control_thread || !p) { return 0; }
  B32 ptrace_stop = 0;
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn->entities + i;
    if(t->kind == MAC_DMN_Kind_Thread && t->parent == p && t->pending_exception &&
       t->exception.exception == EXC_SOFTWARE && t->exception.code_count >= 2 &&
       t->exception.code[0] == EXC_SOFT_SIGNAL) { ptrace_stop = 1; }
  }
  if(!ptrace_stop)
  {
    // Mach suspension alone is not the BSD SSTOP required by PT_DETACH.
    // Receive this process's SIGSTOP without consuming another task's events.
    if(mach_port_move_member(mach_task_self(), p->exception_port, MACH_PORT_NULL) != KERN_SUCCESS) { return 0; }
    if(kill((pid_t)p->id, SIGSTOP) != 0)
    {
      mach_port_move_member(mach_task_self(), p->exception_port, mac_dmn->exception_port_set);
      return 0;
    }
    for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
    {
      MAC_DMN_Entity *t = mac_dmn->entities + i;
      if(t->kind == MAC_DMN_Kind_Thread && t->parent == p)
      {
        mac_dmn_reply(t, 1);
        if(t->suspended) { thread_resume(t->port); t->suspended = 0; }
      }
    }
    if(p->suspended && task_resume(p->port) == KERN_SUCCESS) { p->suspended = 0; }
    U64 endt_us = now_time_us() + 2000000;
    while(!ptrace_stop && now_time_us() < endt_us)
    {
      union { MAC_DMN_Exception request; U8 bytes[4096]; } message = {0};
      kern_return_t kr = mach_msg(&message.request.head, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                  0, sizeof(message), p->exception_port, 20, MACH_PORT_NULL);
      if(kr == KERN_SUCCESS && mac_dmn_valid_exception(&message.request))
      {
        MAC_DMN_Entity *t = mac_dmn_thread(p, message.request.thread.name);
        if(!t) { mach_msg_destroy(&message.request.head); break; }
        t->exception = message.request;
        t->pending_exception = 1;
        t->exception_is_user = 0;
        ptrace_stop = message.request.exception == EXC_SOFTWARE &&
                      message.request.code_count >= 2 && message.request.code[0] == EXC_SOFT_SIGNAL;
        if(!ptrace_stop) { mac_dmn_reply(t, 1); }
      }
      else if(kr == KERN_SUCCESS) { mach_msg_destroy(&message.request.head); }
      else if(kr != MACH_RCV_TIMED_OUT && kr != MACH_RCV_INTERRUPTED) { break; }
    }
    if(!p->suspended && task_suspend(p->port) == KERN_SUCCESS) { p->suspended = 1; }
    mach_port_move_member(mach_task_self(), p->exception_port, mac_dmn->exception_port_set);
    if(!ptrace_stop) { return 0; }
  }
  // Do not PT_THUPDATE the signal first: it would clear BSD's stopped state.
  if(ptrace(PT_DETACH, (pid_t)p->id, (caddr_t)1, 0) != 0) { return 0; }
  p->detached = 1;
  p->suspended = 0; // PT_DETACH releases the task-suspend count itself.
  for(U32 i = 0; i < p->old_count; i += 1)
  { task_set_exception_ports(p->port, p->old_masks[i], p->old_ports[i], p->old_behaviors[i], p->old_flavors[i]); }
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *t = mac_dmn->entities + i;
    if(t->kind == MAC_DMN_Kind_Thread && t->parent == p)
    {
      mac_dmn_reply(t, 1);
      if(t->suspended) { thread_resume(t->port); t->suspended = 0; }
    }
  }
  // No SIGCONT: it can replace the cleared BSD signal before the Mach reply
  // above is consumed, causing the detached process to stop again.
  return 1;
}

internal void
dmn_halt(U64 code, U64 user_data)
{
  mutex_take(mac_dmn->halt_mutex);
  mac_dmn->halt_requested = 1;
  mac_dmn->halt_code = code;
  mac_dmn->halt_user_data = user_data;
  mutex_drop(mac_dmn->halt_mutex);
  mach_msg_header_t wake = {0};
  wake.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
  wake.msgh_remote_port = mac_dmn->exception_port;
  wake.msgh_size = sizeof(wake);
  wake.msgh_id = 1;
  mach_msg(&wake, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(wake), 0, MACH_PORT_NULL, 0, MACH_PORT_NULL);
}

typedef struct MAC_DMN_Patch MAC_DMN_Patch;
struct MAC_DMN_Patch
{
  MAC_DMN_Entity *process;
  U64 address;
  U64 id;
  U32 saved;
  B32 installed;
  B32 is_internal;
};

typedef struct MAC_DMN_DebugSave MAC_DMN_DebugSave;
struct MAC_DMN_DebugSave
{
  MAC_DMN_Entity *thread;
  arm_debug_state64_t state;
};

internal B32
mac_dmn_frozen(DMN_RunCtrls *ctrls, MAC_DMN_Entity *t)
{
  if(ctrls->single_step_thread.u64[0])
  { return !dmn_handle_match(ctrls->single_step_thread, mac_dmn_handle(t)); }
  DMN_Handle entity = mac_dmn_handle(ctrls->run_entities_are_processes ? t->parent : t);
  B32 listed = 0;
  for(U64 i = 0; i < ctrls->run_entity_count; i += 1)
  { if(dmn_handle_match(entity, ctrls->run_entities[i])) { listed = 1; break; } }
  return ctrls->run_entities_are_unfrozen ? !listed : listed;
}

internal void
mac_dmn_retire_process(Arena *arena, DMN_EventList *events, MAC_DMN_Entity *p, U32 exit_code)
{
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *e = mac_dmn->entities + i;
    if(e->kind != MAC_DMN_Kind_Null && e->parent == p)
    {
      if(e->kind == MAC_DMN_Kind_Thread)
      {
        mac_dmn_event(arena, events, DMN_EventKind_ExitThread, p, e);
        mac_dmn_reply(e, 1);
      }
      else if(e->kind == MAC_DMN_Kind_Module)
      {
        DMN_Event *event = mac_dmn_event(arena, events, DMN_EventKind_UnloadModule, p, 0);
        event->module = mac_dmn_handle(e);
      }
      mac_dmn_release(e);
    }
  }
  DMN_Event *e = mac_dmn_event(arena, events, DMN_EventKind_ExitProcess, p, 0);
  e->code = exit_code;
  for(U32 i = 0; i < p->old_count; i += 1)
  { if(p->old_ports[i]) { mach_port_deallocate(mach_task_self(), p->old_ports[i]); } }
  struct kevent change;
  EV_SET(&change, p->id, EVFILT_PROC, EV_DELETE, 0, 0, 0);
  kevent(mac_dmn->kqueue, &change, 1, 0, 0, 0);
  mac_dmn_release(p);
}

internal void
mac_dmn_collect(Arena *arena, DMN_EventList *events)
{
  struct timespec zero = {0};
  struct kevent notes[64];
  int count = kevent(mac_dmn->kqueue, 0, 0, notes, ArrayCount(notes), &zero);
  for(int n = 0; n < count; n += 1)
  {
    if(notes[n].filter != EVFILT_PROC) { continue; }
    if(notes[n].fflags & NOTE_EXIT)
    {
      for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
      {
        MAC_DMN_Entity *p = mac_dmn->entities + i;
        if(p->kind == MAC_DMN_Kind_Process && p->id == notes[n].ident)
        {
          int status = (int)notes[n].data;
          int waited = 0;
          pid_t pid = waitpid((pid_t)p->id, &waited, WNOHANG);
          if(pid > 0) { status = waited; }
          U32 code = WIFEXITED(status) ? (U32)WEXITSTATUS(status) :
                     WIFSIGNALED(status) ? (U32)(128 + WTERMSIG(status)) : p->exit_code;
          mac_dmn_retire_process(arena, events, p, code);
          break;
        }
      }
    }
  }
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  {
    MAC_DMN_Entity *p = mac_dmn->entities + i;
    if(p->kind != MAC_DMN_Kind_Process) { continue; }
    if(p->detached) { mac_dmn_retire_process(arena, events, p, 0); continue; }
    if(!p->announced)
    {
      p->announced = 1;
      DMN_Event *e = mac_dmn_event(arena, events, DMN_EventKind_CreateProcess, p, 0);
      e->code = (U32)p->id;
      e->string = str8_copy(arena, str8_cstring(p->path));
    }
    mac_dmn_sync_threads(arena, events, p);
    mac_dmn_sync_modules(arena, events, p);
    if(!p->handshake && (!p->launched || !p->entry))
    {
      p->handshake = 1;
      mac_dmn_event(arena, events, DMN_EventKind_HandshakeComplete, p, 0);
    }
  }
}

internal DMN_EventList
dmn_ctrl_run(Arena *arena, DMN_CtrlCtx *ctx, DMN_RunCtrls *ctrls)
{
  DMN_EventList events = {0};
  if(ctx != &mac_dmn->context || !mac_dmn_control_thread)
  { mac_dmn_error(arena, &events, "Invalid debugger control context."); return events; }
  Temp scratch = scratch_begin(&arena, 1);
  mac_dmn_stop_all();
  mac_dmn_collect(arena, &events);
  if(events.count) { scratch_end(scratch); return events; }
  U64 process_count = 0;
  for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
  { process_count += mac_dmn->entities[i].kind == MAC_DMN_Kind_Process; }
  if(!process_count)
  {
    DMN_Event *e = dmn_event_list_push(arena, &events);
    e->kind = DMN_EventKind_Error;
    e->error_kind = DMN_ErrorKind_NotAttached;
    scratch_end(scratch);
    return events;
  }

  // Dyld notifications and synchronous hardware traps are stepped over with other threads held.
  // Each iteration either exposes an event or makes actual target progress.
  B32 repeat = 1;
  while(repeat && !events.count)
  {
    repeat = 0;
    U64 mark = arena_pos(scratch.arena);
    MAC_DMN_Entity *internal_step = 0;
    for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
    {
      MAC_DMN_Entity *t = mac_dmn->entities + i;
      if(t->kind == MAC_DMN_Kind_Thread && t->step_over)
      {
        ARM64_RegBlock regs = {0};
        if(mac_dmn_regs(t, &regs, 0) && regs.pc == t->step_over) { internal_step = t; break; }
        t->step_over = 0;
      }
    }
    U64 patch_cap = ctrls->traps.trap_count + 2 * process_count;
    MAC_DMN_Patch *patches = push_array(scratch.arena, MAC_DMN_Patch, patch_cap);
    U64 patch_count = 0;
    if(!internal_step)
    {
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n; n = n->next)
      {
        for(U64 j = 0; j < n->count; j += 1)
        {
          DMN_Trap *trap = n->v + j;
          if(trap->flags) { continue; }
          MAC_DMN_Entity *p = mac_dmn_entity(trap->process, MAC_DMN_Kind_Process);
          if(!p || (trap->vaddr & 3)) { mac_dmn_error(arena, &events, "Invalid or unaligned ARM64 breakpoint."); break; }
          B32 duplicate = 0;
          for(U64 k = 0; k < patch_count; k += 1)
          { if(patches[k].process == p && patches[k].address == trap->vaddr) { duplicate = 1; break; } }
          if(!duplicate)
          {
            MAC_DMN_Patch *patch = patches + patch_count++;
            patch->process = p;
            patch->address = trap->vaddr;
            patch->id = trap->id;
          }
        }
      }
      for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
      {
        MAC_DMN_Entity *p = mac_dmn->entities + i;
        if(p->kind != MAC_DMN_Kind_Process) { continue; }
        U64 addresses[2] = {p->notifier, p->handshake ? 0 : p->entry};
        for(U64 j = 0; j < 2; j += 1)
        {
          if(!addresses[j]) { continue; }
          B32 duplicate = 0;
          for(U64 k = 0; k < patch_count; k += 1)
          { if(patches[k].process == p && patches[k].address == addresses[j]) { duplicate = 1; break; } }
          if(!duplicate)
          {
            MAC_DMN_Patch *patch = patches + patch_count++;
            patch->process = p;
            patch->address = addresses[j];
            patch->is_internal = 1;
          }
        }
      }
      U32 brk = 0xd4200000;
      for(U64 i = 0; i < patch_count && !events.count; i += 1)
      {
        MAC_DMN_Patch *patch = patches + i;
        if(mac_dmn_read(patch->process, patch->address, 4, &patch->saved) != 4 ||
           !mac_dmn_write(patch->process, patch->address, 4, &brk))
        { mac_dmn_error(arena, &events, "Could not install ARM64 breakpoint in target memory."); break; }
        patch->installed = 1;
      }
    }
    U64 debug_count = 0;
    MAC_DMN_DebugSave *debug = push_array(scratch.arena, MAC_DMN_DebugSave, mac_dmn->entity_count);
    U64 runnable = 0;
    for(U64 i = 1; i < mac_dmn->entity_count && !events.count; i += 1)
    {
      MAC_DMN_Entity *t = mac_dmn->entities + i;
      if(t->kind != MAC_DMN_Kind_Thread) { continue; }
      B32 frozen = internal_step ? t != internal_step : mac_dmn_frozen(ctrls, t);
      if(frozen && !t->suspended)
      {
        if(thread_suspend(t->port) != KERN_SUCCESS) { continue; }
        t->suspended = 1;
      }
      else if(!frozen && t->suspended)
      { thread_resume(t->port); t->suspended = 0; }
      if(frozen) { continue; }
      runnable += 1;
      MAC_DMN_DebugSave *save = debug + debug_count;
      mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
      if(thread_get_state(t->port, ARM_DEBUG_STATE64, (thread_state_t)&save->state, &count) != KERN_SUCCESS)
      { mac_dmn_error(arena, &events, "Could not read ARM64 hardware debug registers."); break; }
      save->thread = t;
      debug_count += 1;
      arm_debug_state64_t state = save->state;
      B32 stepping = internal_step == t || dmn_handle_match(ctrls->single_step_thread, mac_dmn_handle(t));
      state.__mdscr_el1 = (state.__mdscr_el1 & ~1ull) | stepping;
      U32 bp_count = 0, wp_count = 0;
      for(DMN_TrapChunkNode *n = ctrls->traps.first; n && !events.count && !internal_step; n = n->next)
      {
        for(U64 j = 0; j < n->count; j += 1)
        {
          DMN_Trap *trap = n->v + j;
          if(!trap->flags || !dmn_handle_match(trap->process, mac_dmn_handle(t->parent))) { continue; }
          if(trap->flags & DMN_TrapFlag_BreakOnExecute)
          {
            if(bp_count >= 6 || (trap->vaddr & 3))
            { mac_dmn_error(arena, &events, "ARM64 supports six aligned hardware execution breakpoints."); break; }
            state.__bvr[bp_count] = trap->vaddr;
            state.__bcr[bp_count++] = 1 | (2 << 1) | (15 << 5);
          }
          else
          {
            U32 size = trap->size;
            U32 offset = trap->vaddr & 7;
            if(wp_count >= 4 || !size || size > 8 || offset + size > 8)
            { mac_dmn_error(arena, &events, "ARM64 supports four watchpoints, each contained in one eight-byte granule."); break; }
            state.__wvr[wp_count] = trap->vaddr & ~7ull;
            state.__wcr[wp_count++] = 1 | (2 << 1) |
              ((trap->flags & DMN_TrapFlag_BreakOnRead) ? (1 << 3) : 0) |
              ((trap->flags & DMN_TrapFlag_BreakOnWrite) ? (1 << 4) : 0) |
              ((((1u << size) - 1) << offset) << 5);
          }
        }
      }
      if(thread_set_state(t->port, ARM_DEBUG_STATE64, (thread_state_t)&state, ARM_DEBUG_STATE64_COUNT) != KERN_SUCCESS)
      { mac_dmn_error(arena, &events, "Could not program ARM64 hardware stepping or watchpoints."); }
    }
    if(!runnable && !events.count) { mac_dmn_error(arena, &events, "Run controls freeze every target thread."); }
    if(!events.count)
    {
      for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
      {
        MAC_DMN_Entity *p = mac_dmn->entities + i;
        if(p->kind == MAC_DMN_Kind_Thread) { mac_dmn_reply(p, ctrls->ignore_previous_exception); }
        if(p->kind == MAC_DMN_Kind_Process)
        {
          if(p->suspended && task_resume(p->port) == KERN_SUCCESS) { p->suspended = 0; }
        }
      }
      B32 stopped = 0;
      while(!stopped && !events.count)
      {
        union { MAC_DMN_Exception request; U8 bytes[4096]; } message = {0};
        kern_return_t kr = mach_msg(&message.request.head, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
                                    0, sizeof(message), mac_dmn->exception_port_set, 20, MACH_PORT_NULL);
        if(kr == KERN_SUCCESS && mac_dmn_valid_exception(&message.request))
        {
          mac_dmn_stop_all();
          MAC_DMN_Entity *p = 0;
          for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
          {
            MAC_DMN_Entity *candidate = mac_dmn->entities + i;
            if(candidate->kind == MAC_DMN_Kind_Process && candidate->port == message.request.task.name) { p = candidate; break; }
          }
          if(!p) { mach_msg_destroy(&message.request.head); continue; }
          MAC_DMN_Entity *t = mac_dmn_thread(p, message.request.thread.name);
          if(!t) { mach_msg_destroy(&message.request.head); continue; }
          t->exception = message.request;
          t->pending_exception = 1;
          t->exception_is_user = 0;
          mac_dmn_sync_threads(arena, &events, p);
          ARM64_RegBlock regs = {0};
          mac_dmn_regs(t, &regs, 0);
          B32 signal_stop = message.request.exception == EXC_SOFTWARE &&
                            message.request.code_count >= 2 && message.request.code[0] == EXC_SOFT_SIGNAL;
          if(signal_stop && p->initial_sigstop && message.request.code[1] == SIGSTOP)
          {
            p->initial_sigstop = 0;
            mac_dmn_reply(t, 1);
            repeat = 1;
          }
          else if(signal_stop && p->suppress_spawn_sigcont && message.request.code[1] == SIGCONT)
          {
            p->suppress_spawn_sigcont = 0;
            mac_dmn_reply(t, 1);
            repeat = 1;
          }
          else if(message.request.exception == EXC_BREAKPOINT)
          {
            MAC_DMN_Patch *hit = 0;
            for(U64 i = 0; i < patch_count; i += 1)
            {
              // BRK leaves PC on the trapping instruction, not PC+1 as on x86.
              if(patches[i].process == p && patches[i].address == regs.pc) { hit = patches + i; break; }
            }
            U32 exception_class = 0;
            if(!hit)
            {
              arm_exception_state64_t state = {0};
              mach_msg_type_number_t count = ARM_EXCEPTION_STATE64_COUNT;
              if(thread_get_state(t->port, ARM_EXCEPTION_STATE64, (thread_state_t)&state, &count) != KERN_SUCCESS)
              { mac_dmn_error(arena, &events, "Could not read ARM64 debug exception state."); continue; }
              exception_class = state.__esr >> 26;
            }
            B32 single_step_exception = (exception_class == 0x32 || exception_class == 0x33);
            B32 software_trap = (exception_class == 0x3c);
            if(hit && hit->is_internal)
            {
              if(regs.pc == p->entry && !p->handshake)
              {
                p->handshake = 1;
                mac_dmn_sync_modules(arena, &events, p);
                mac_dmn_event(arena, &events, DMN_EventKind_HandshakeComplete, p, t);
              }
              else
              {
                mac_dmn_sync_modules(arena, &events, p);
                if(t) { t->step_over = regs.pc; }
              }
              repeat = 1;
            }
            else if(internal_step && t == internal_step && !hit && single_step_exception)
            {
              t->step_over = 0;
              if(dmn_handle_match(ctrls->single_step_thread, mac_dmn_handle(t)))
              { mac_dmn_event(arena, &events, DMN_EventKind_SingleStep, p, t); }
              else { repeat = 1; }
            }
            else
            {
              U64 exception_pc = regs.pc;
              if(!hit && software_trap)
              {
                // Consume an intrinsic BRK just as x86 consumes an intrinsic
                // INT3. Retrying this instruction would trap forever.
                if(!mac_dmn_write_pc(t, regs.pc + 4))
                { mac_dmn_error(arena, &events, "Could not advance past ARM64 programmatic breakpoint."); continue; }
                regs.pc += 4;
              }
              DMN_EventKind kind = hit ? DMN_EventKind_Breakpoint :
                single_step_exception && dmn_handle_match(ctrls->single_step_thread, mac_dmn_handle(t)) ? DMN_EventKind_SingleStep : DMN_EventKind_Trap;
              DMN_Event *e = mac_dmn_event(arena, &events, kind, p, t);
              if(!hit) { e->address = exception_pc; }
              if(hit) { e->user_data = hit->id; }
              if(!hit && !software_trap && kind != DMN_EventKind_SingleStep)
              {
                for(DMN_TrapChunkNode *n = ctrls->traps.first; n; n = n->next)
                {
                  for(U64 j = 0; j < n->count; j += 1)
                  {
                    DMN_Trap *trap = n->v + j;
                    U64 fault = message.request.code_count > 1 ? message.request.code[1] : regs.pc;
                    if(trap->flags && dmn_handle_match(trap->process, mac_dmn_handle(p)) &&
                       ((fault >= trap->vaddr && fault < trap->vaddr + Max(trap->size, 1)) ||
                        ((trap->flags & DMN_TrapFlag_BreakOnExecute) && trap->vaddr == regs.pc)))
                    {
                      e->kind = DMN_EventKind_Breakpoint;
                      e->address = trap->vaddr;
                      e->user_data = trap->id;
                      t->step_over = regs.pc;
                    }
                  }
                }
              }
            }
          }
          else
          {
            t->exception_is_user = 1;
            DMN_Event *e = mac_dmn_event(arena, &events, DMN_EventKind_Exception, p, t);
            e->code = signal_stop ? (U32)message.request.code[1] : (U32)message.request.exception;
            e->address = message.request.code_count > 1 ? message.request.code[1] : regs.pc;
            if(message.request.exception == EXC_BAD_ACCESS)
            {
              arm_exception_state64_t state = {0};
              mach_msg_type_number_t count = ARM_EXCEPTION_STATE64_COUNT;
              if(thread_get_state(t->port, ARM_EXCEPTION_STATE64, (thread_state_t)&state, &count) == KERN_SUCCESS)
              {
                U32 ec = state.__esr >> 26;
                e->address = state.__far;
                e->exception_kind = (ec == 0x20 || ec == 0x21) ? DMN_ExceptionKind_MemoryExecute :
                                    (state.__esr & (1 << 6)) ? DMN_ExceptionKind_MemoryWrite : DMN_ExceptionKind_MemoryRead;
              }
            }
          }
          stopped = 1;
        }
        else if(kr == KERN_SUCCESS && message.request.head.msgh_id != 1)
        { mach_msg_destroy(&message.request.head); }
        else if(kr != KERN_SUCCESS && kr != MACH_RCV_TIMED_OUT && kr != MACH_RCV_INTERRUPTED)
        { mac_dmn_error(arena, &events, "Mach exception receive failed."); }
        mutex_take(mac_dmn->halt_mutex);
        if(mac_dmn->halt_requested)
        {
          mac_dmn_stop_all();
          DMN_Event *e = mac_dmn_event(arena, &events, DMN_EventKind_Halt, 0, 0);
          e->code = (U32)mac_dmn->halt_code;
          e->user_data = mac_dmn->halt_user_data;
          mac_dmn->halt_requested = 0;
          stopped = 1;
        }
        mutex_drop(mac_dmn->halt_mutex);
        if(!stopped)
        {
          // Mach has no ptrace-style thread-birth event. Sample the native task
          // thread list while all tasks are suspended, retaining new send rights.
          mac_dmn_stop_all();
          mac_dmn_collect(arena, &events);
          if(events.count) { stopped = 1; }
          else
          {
            for(U64 i = 1; i < mac_dmn->entity_count; i += 1)
            {
              MAC_DMN_Entity *p = mac_dmn->entities + i;
              if(p->kind == MAC_DMN_Kind_Process && p->suspended && task_resume(p->port) == KERN_SUCCESS) { p->suspended = 0; }
            }
          }
        }
      }
    }
    mac_dmn_stop_all();
    for(U64 i = 0; i < debug_count; i += 1)
    {
      MAC_DMN_DebugSave *save = debug + i;
      if(save->thread->kind == MAC_DMN_Kind_Thread)
      { thread_set_state(save->thread->port, ARM_DEBUG_STATE64, (thread_state_t)&save->state, ARM_DEBUG_STATE64_COUNT); }
    }
    for(U64 i = 0; i < patch_count; i += 1)
    {
      MAC_DMN_Patch *patch = patches + i;
      if(patch->installed && patch->process->kind == MAC_DMN_Kind_Process &&
         !mac_dmn_write(patch->process, patch->address, 4, &patch->saved))
      { mac_dmn_error(arena, &events, "Could not restore target instruction after breakpoint."); repeat = 0; }
    }
    arena_pop_to(scratch.arena, mark);
  }
  mac_dmn_collect(arena, &events);
  scratch_end(scratch);
  return events;
}

internal U64
dmn_process_memory_reserve(DMN_Handle process, U64 vaddr, U64 size)
{
  U64 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
    mach_vm_address_t address = vaddr;
    if(p && size && mach_vm_allocate(p->port, &address, size, vaddr ? VM_FLAGS_FIXED : VM_FLAGS_ANYWHERE) == KERN_SUCCESS)
    {
      if(mach_vm_protect(p->port, address, size, 0, VM_PROT_NONE) == KERN_SUCCESS) { result = address; }
      else { mach_vm_deallocate(p->port, address, size); }
    }
  }
  return result;
}

internal void
dmn_process_memory_commit(DMN_Handle process, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
    if(p) { mach_vm_protect(p->port, vaddr, size, 0, VM_PROT_READ | VM_PROT_WRITE); }
  }
}

internal void
dmn_process_memory_decommit(DMN_Handle process, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
    if(p)
    {
      // Replace with anonymous demand-zero pages without releasing the address.
      mach_vm_address_t address = vaddr;
      if(mach_vm_allocate(p->port, &address, size, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE) == KERN_SUCCESS)
      { mach_vm_protect(p->port, address, size, 0, VM_PROT_NONE); }
    }
  }
}

internal void
dmn_process_memory_release(DMN_Handle process, U64 vaddr, U64 size)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
    if(p) { mach_vm_deallocate(p->port, vaddr, size); }
  }
}

internal void
dmn_process_memory_protect(DMN_Handle process, U64 vaddr, U64 size, AccessFlags flags)
{
  DMN_AccessScope
  {
    MAC_DMN_Entity *p = mac_dmn_entity(process, MAC_DMN_Kind_Process);
    vm_prot_t prot = VM_PROT_NONE;
    if(flags & AccessFlag_Read) { prot |= VM_PROT_READ; }
    if(flags & AccessFlag_Write) { prot |= VM_PROT_WRITE; }
    if(flags & AccessFlag_Execute) { prot |= VM_PROT_EXECUTE; }
    if(p) { mach_vm_protect(p->port, vaddr, size, 0, prot); }
  }
}

internal U64
dmn_process_read(DMN_Handle process, Rng1U64 range, void *dst)
{
  U64 result = 0;
  DMN_AccessScope
  {
    if(range.max >= range.min)
    { result = mac_dmn_read(mac_dmn_entity(process, MAC_DMN_Kind_Process), range.min, range.max - range.min, dst); }
  }
  return result;
}

internal B32
dmn_process_write(DMN_Handle process, Rng1U64 range, void *src)
{
  B32 result = 0;
  DMN_AccessScope
  {
    if(range.max >= range.min)
    { result = mac_dmn_write(mac_dmn_entity(process, MAC_DMN_Kind_Process), range.min, range.max - range.min, src); }
  }
  return result;
}

internal B32
dmn_thread_read_reg_block(DMN_Handle thread, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope { result = mac_dmn_regs(mac_dmn_entity(thread, MAC_DMN_Kind_Thread), reg_block, 0); }
  return result;
}

internal B32
dmn_thread_write_reg_block(DMN_Handle thread, void *reg_block)
{
  B32 result = 0;
  DMN_AccessScope { result = mac_dmn_regs(mac_dmn_entity(thread, MAC_DMN_Kind_Thread), reg_block, 1); }
  return result;
}

internal B32
dmn_thread_get_module_tls_vaddr(DMN_Handle thread, DMN_Handle module, U64 *vaddr_out)
{
  B32 result = 0;
  DMN_AccessScope
  {
    MAC_DMN_Entity *t = mac_dmn_entity(thread, MAC_DMN_Kind_Thread);
    MAC_DMN_Entity *m = mac_dmn_entity(module, MAC_DMN_Kind_Module);
    if(t && m && t->parent == m->parent && m->tls_descriptors && m->tls_descriptors_size >= 24)
    {
      U64 descriptor[3] = {0};
      thread_identifier_info_data_t info = {0};
      mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
      if(mac_dmn_read(t->parent, m->tls_descriptors, sizeof(descriptor), descriptor) == sizeof(descriptor) &&
         descriptor[1] < 1024 &&
         thread_info(t->port, THREAD_IDENTIFIER_INFO, (thread_info_t)&info, &count) == KERN_SUCCESS)
      {
        // Darwin's cthread handle is the user TSD base (TPIDRRO_EL0 on ARM64).
        // dyld allocates one pthread key for each image's TLV storage block.
        U64 base = 0;
        if(mac_dmn_read(t->parent, info.thread_handle + descriptor[1] * sizeof(U64), sizeof(base), &base) == sizeof(base) && base)
        { *vaddr_out = base; result = 1; }
      }
    }
  }
  return result;
}

typedef struct MAC_DMN_ProcessList MAC_DMN_ProcessList;
struct MAC_DMN_ProcessList
{
  U64 count;
  U64 index;
  pid_t pids[];
};

internal void
dmn_process_iter_begin(DMN_ProcessIter *iter)
{
  MemoryZeroStruct(iter);
  int bytes = proc_listallpids(0, 0);
  if(bytes <= 0) { return; }
  // proc_listallpids(NULL) returns a count, not a byte size.
  U64 capacity = (U64)bytes + 64;
  MAC_DMN_ProcessList *list = calloc(1, sizeof(*list) + capacity * sizeof(pid_t));
  if(!list) { return; }
  int count = proc_listallpids(list->pids, (int)(capacity * sizeof(pid_t)));
  list->count = count > 0 ? Min((U64)count, capacity) : 0;
  iter->v[0] = (U64)list;
}

internal B32
dmn_process_iter_next(Arena *arena, DMN_ProcessIter *iter, DMN_ProcessInfo *info_out)
{
  MAC_DMN_ProcessList *list = (MAC_DMN_ProcessList *)iter->v[0];
  while(list && list->index < list->count)
  {
    pid_t pid = list->pids[list->index++];
    if(pid <= 0) { continue; }
    char path[PROC_PIDPATHINFO_MAXSIZE] = {0};
    if(proc_pidpath(pid, path, sizeof(path)) <= 0)
    { if(proc_name(pid, path, sizeof(path)) <= 0) { continue; } }
    info_out->pid = (U32)pid;
    info_out->name = str8_copy(arena, str8_cstring(path));
    return 1;
  }
  return 0;
}

internal void
dmn_process_iter_end(DMN_ProcessIter *iter)
{
  free((void *)iter->v[0]);
  MemoryZeroStruct(iter);
}
