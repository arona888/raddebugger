// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
// Helpers

internal MAC_Entity *
mac_entity_alloc(void)
{
  pthread_mutex_lock(&mac_state.entity_mutex);
  MAC_Entity *e = mac_state.free_entity;
  if(e) { SLLStackPop(mac_state.free_entity); }
  else { e = push_array_no_zero(mac_state.entity_arena, MAC_Entity, 1); }
  pthread_mutex_unlock(&mac_state.entity_mutex);
  MemoryZeroStruct(e);
  return e;
}

internal void
mac_entity_release(MAC_Entity *e)
{
  pthread_mutex_lock(&mac_state.entity_mutex);
  SLLStackPush(mac_state.free_entity, e);
  pthread_mutex_unlock(&mac_state.entity_mutex);
}

internal DateTime
mac_date_time_from_tm(struct tm t, U32 msec)
{
  DateTime d = {0};
  d.year = t.tm_year+1900; d.mon = t.tm_mon; d.day = t.tm_mday-1;
  d.hour = t.tm_hour; d.min = t.tm_min; d.sec = t.tm_sec; d.msec = msec;
  return d;
}

internal struct tm
mac_tm_from_date_time(DateTime d)
{
  struct tm t = {0};
  t.tm_year = d.year-1900; t.tm_mon = d.mon; t.tm_mday = d.day+1;
  t.tm_hour = d.hour; t.tm_min = d.min; t.tm_sec = d.sec; t.tm_isdst = -1;
  return t;
}

internal DenseTime
mac_dense_time(struct timespec t)
{
  struct tm value = {0};
  gmtime_r(&t.tv_sec, &value);
  return dense_time_from_date_time(mac_date_time_from_tm(value, t.tv_nsec/1000000));
}

internal FileProperties
mac_properties(struct stat *s)
{
  FileProperties p = {0};
  p.size = s->st_size;
  p.created = mac_dense_time(s->st_birthtimespec);
  p.modified = mac_dense_time(s->st_mtimespec);
  if(S_ISDIR(s->st_mode)) { p.flags |= FilePropertyFlag_IsFolder; }
  return p;
}

// Darwin limits POSIX IPC names to 31 bytes. Hash the full logical name so
// parent and converter processes use the same name without truncation collisions.
internal void
mac_ipc_name(char dst[32], String8 name)
{
  U64 hash = 14695981039346656037ull;
  for EachIndex(i, name.size) { hash = (hash ^ name.str[i])*1099511628211ull; }
  snprintf(dst, 32, "/raddbg_%016llx", (unsigned long long)hash);
}

internal B32
mac_wait_semaphore(semaphore_t sem, U64 endt_us)
{
  kern_return_t kr;
  do
  {
    if(endt_us == max_U64) { kr = semaphore_wait(sem); }
    else
    {
      U64 now = now_time_us();
      U64 left = endt_us > now ? endt_us-now : 0;
      mach_timespec_t duration = {(unsigned int)(left/1000000), (clock_res_t)((left%1000000)*1000)};
      kr = semaphore_timedwait(sem, duration);
    }
  } while(kr == KERN_ABORTED);
  return kr == KERN_SUCCESS;
}

internal int
mac_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mutex, U64 endt_us)
{
  if(endt_us == max_U64) { return pthread_cond_wait(cv, mutex); }
  U64 now = now_time_us();
  U64 left = endt_us > now ? endt_us-now : 0;
  struct timespec duration = {(time_t)(left/1000000), (long)((left%1000000)*1000)};
  return pthread_cond_timedwait_relative_np(cv, mutex, &duration);
}

////////////////////////////////
// Time, system, and process information

internal B32
debugger_is_attached(void)
{
  struct kinfo_proc info = {0};
  size_t size = sizeof(info);
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
  return sysctl(mib, ArrayCount(mib), &info, &size, 0, 0) == 0 && (info.kp_proc.p_flag & P_TRACED) != 0;
}

internal U64
now_time_us(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (U64)t.tv_sec*1000000 + t.tv_nsec/1000;
}

internal U32 now_time_unix(void) { return (U32)time(0); }
internal DateTime
now_time_universal(void)
{
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  struct tm t;
  gmtime_r(&now.tv_sec, &t);
  return mac_date_time_from_tm(t, now.tv_nsec/1000000);
}

internal DateTime
universal_from_local_time(DateTime *dt)
{
  struct tm local = mac_tm_from_date_time(*dt), universal;
  time_t t = mktime(&local);
  gmtime_r(&t, &universal);
  return mac_date_time_from_tm(universal, dt->msec);
}

internal DateTime
local_from_universal_time(DateTime *dt)
{
  struct tm universal = mac_tm_from_date_time(*dt), local;
  time_t t = timegm(&universal);
  localtime_r(&t, &local);
  return mac_date_time_from_tm(local, dt->msec);
}

internal void
sleep_ms(U32 ms)
{
  struct timespec remaining = {ms/1000, (long)(ms%1000)*1000000};
  while(nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {}
}

internal Guid
make_guid(void)
{
  Guid g;
  arc4random_buf(g.v, sizeof(g.v));
  g.data3 = (g.data3 & 0x0fff) | 0x4000;
  g.data4[0] = (g.data4[0] & 0x3f) | 0x80;
  return g;
}

internal SystemInfo *get_system_info(void) { return &mac_state.system_info; }
internal ProcessInfo *get_process_info(void) { return &mac_state.process_info; }
internal U32 tid(void) { U64 id = 0; pthread_threadid_np(0, &id); return (U32)id; }
internal void
set_platform_thread_name(String8 name)
{
  char buffer[64];
  U64 n = Min(name.size, sizeof(buffer)-1);
  MemoryCopy(buffer, name.str, n); buffer[n] = 0;
  pthread_setname_np(buffer);
}

internal String8
get_current_path(Arena *arena)
{
  char path[PATH_MAX];
  return getcwd(path, sizeof(path)) ? str8_copy(arena, str8_cstring(path)) : str8_zero();
}

internal U32
get_process_start_time_unix(void)
{
  struct proc_bsdinfo info = {0};
  return proc_pidinfo(getpid(), PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info) ? (U32)info.pbi_start_tvsec : 0;
}

internal U64
get_available_commit_memory(void)
{
  vm_statistics64_data_t stats;
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  if(host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&stats, &count) != KERN_SUCCESS)
  {
    return get_system_info()->physical_memory_size;
  }
  return ((U64)stats.free_count + stats.inactive_count + stats.purgeable_count)*get_system_info()->page_size;
}

////////////////////////////////
// Virtual memory and demand paging

internal void *
reserve_memory(U64 size)
{
  void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
  return p == MAP_FAILED ? 0 : p;
}
internal B32 commit_memory(void *p, U64 size) { return mprotect(p, size, PROT_READ|PROT_WRITE) == 0; }
internal void decommit_memory(void *p, U64 size) { mmap(p, size, PROT_NONE, MAP_FIXED|MAP_PRIVATE|MAP_ANON, -1, 0); }
internal void release_memory(void *p, U64 size) { if(p && size) { munmap(p, size); } }
internal void *
reserve_memory_large(U64 size)
{
  void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANON, VM_FLAGS_SUPERPAGE_SIZE_2MB, 0);
  return p == MAP_FAILED ? 0 : p;
}
internal B32 commit_memory_large(void *p, U64 size) { return commit_memory(p, size); }
internal B32 memory_placeholders_supported(void) { return 1; }
internal void *
reserve_memory_placeholders(U64 size, U64 block_size)
{
  if(!size || !block_size || size%block_size || block_size%get_system_info()->page_size) { return 0; }
  return reserve_memory(size);
}
internal void release_memory_placeholders(void *p, U64 size, U64 block_size) { release_memory(p, size); }
internal B32
unmap_memory_preserve_placeholder(void *p, U64 size)
{
  return mmap(p, size, PROT_NONE, MAP_PRIVATE|MAP_ANON|MAP_FIXED, -1, 0) == p;
}
internal B32 split_memory_placeholder(void *p, U64 size, U64 first_size)
{
  return p && first_size > 0 && first_size < size && first_size%get_system_info()->page_size == 0;
}
internal B32 coalesce_memory_placeholders(void *p, U64 size) { return p && size > 0; }
internal void
prefetch_memory_ranges(U64 count, Rng1U64 *ranges)
{
  U64 page = get_system_info()->page_size;
  for EachIndex(i, count)
  {
    U64 lo = AlignDownPow2(ranges[i].min, page), hi = AlignPow2(ranges[i].max, page);
    if(hi > lo) { madvise(PtrFromInt(lo), hi-lo, MADV_WILLNEED); }
  }
}

internal void
mac_fault_worker(void *ptr)
{
  ThreadNameF("memory_pager_%u", (U32)IntFromPtr(ptr));
  for(;;)
  {
    MAC_FaultRequest *request = 0;
    ssize_t n = MAC_RETRY_ON_EINTR(read(mac_state.pager.requests[0], &request, sizeof(request)));
    if(n != sizeof(request)) { _exit(1); }
    if(request == 0) { break; }
    mac_in_fault_callback = 1;
    request->handled = mac_state.pager.func(request->address, mac_state.pager.user_data);
    mac_in_fault_callback = 0;
    semaphore_t completed = request->completed;
    // The faulting thread owns the request; never access it after signaling.
    semaphore_signal(completed);
  }
}

internal B32
memory_read_fault_handler_set(MemoryReadFaultFunction *func, void *user_data)
{
  if(func)
  {
    if(mac_state.pager.worker_count || ins_atomic_u32_eval(&mac_state.pager.active)) { return 0; }
    if(pipe(mac_state.pager.requests) != 0) { return 0; }
    fcntl(mac_state.pager.requests[0], F_SETFD, FD_CLOEXEC);
    fcntl(mac_state.pager.requests[1], F_SETFD, FD_CLOEXEC);
    mac_state.pager.func = func;
    mac_state.pager.user_data = user_data;
    mac_state.pager.owner_pid = getpid();
    U32 count = (U32)Clamp(2, get_system_info()->logical_processor_count, ArrayCount(mac_state.pager.workers));
    for EachIndex(i, count)
    {
      Thread worker = thread_launch(mac_fault_worker, PtrFromInt(i));
      if(worker.u64[0] == 0)
      {
        memory_read_fault_handler_set(0, 0);
        return 0;
      }
      mac_state.pager.workers[mac_state.pager.worker_count++] = worker;
    }
    ins_atomic_u32_eval_assign(&mac_state.pager.active, 1);
  }
  else
  {
    if(mac_in_fault_callback || (mac_state.pager.worker_count && mac_state.pager.owner_pid != getpid())) { return 0; }
    ins_atomic_u32_eval_assign(&mac_state.pager.active, 0);
    MAC_FaultRequest *stop = 0;
    for EachIndex(i, mac_state.pager.worker_count)
    {
      if(MAC_RETRY_ON_EINTR(write(mac_state.pager.requests[1], &stop, sizeof(stop))) != sizeof(stop)) { return 0; }
    }
    for EachIndex(i, mac_state.pager.worker_count) { thread_join(mac_state.pager.workers[i], max_U64); }
    if(mac_state.pager.worker_count)
    {
      close(mac_state.pager.requests[0]); close(mac_state.pager.requests[1]);
    }
    MemoryZeroStruct(&mac_state.pager);
  }
  return 1;
}

internal B32
mac_dispatch_fault(int sig, siginfo_t *info, void *context)
{
  if((sig != SIGSEGV && sig != SIGBUS) || !info || !context || mac_in_fault_callback ||
     !ins_atomic_u32_eval(&mac_state.pager.active) || mac_state.pager.owner_pid != getpid()) { return 0; }
#if ARCH_ARM64
  ucontext_t *uc = (ucontext_t *)context;
  U32 esr = uc->uc_mcontext->__es.__esr;
  U32 ec = esr >> 26;
  if((ec != 0x24 && ec != 0x25) || (esr & (1u << 6))) { return 0; }
#elif ARCH_X64
  ucontext_t *uc = (ucontext_t *)context;
  if(uc->uc_mcontext->__es.__err & ((1u << 1)|(1u << 4))) { return 0; }
#endif
  int saved_errno = errno;
  if(mac_fault_completed == MACH_PORT_NULL && semaphore_create(mach_task_self(), &mac_fault_completed, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) { return 0; }
  MAC_FaultRequest request = {.address = info->si_addr, .completed = mac_fault_completed};
  MAC_FaultRequest *p = &request;
  if(MAC_RETRY_ON_EINTR(write(mac_state.pager.requests[1], &p, sizeof(p))) != sizeof(p)) { errno = saved_errno; return 0; }
  kern_return_t kr;
  do { kr = semaphore_wait(mac_fault_completed); } while(kr == KERN_ABORTED);
  errno = saved_errno;
  return kr == KERN_SUCCESS && request.handled;
}

////////////////////////////////
// Threads and synchronization

internal void
mac_thread_release(MAC_Entity *e)
{
  if(ins_atomic_u32_dec_eval(&e->thread.references) == 0)
  {
    semaphore_destroy(mach_task_self(), e->thread.completed);
    mac_entity_release(e);
  }
}

internal void *
mac_thread_entry(void *p)
{
  MAC_Entity *e = p;
#if defined(__OBJC__)
  @autoreleasepool { supplement_thread_base_entry_point(e->thread.func, e->thread.ptr); }
#else
  supplement_thread_base_entry_point(e->thread.func, e->thread.ptr);
#endif
  if(mac_fault_completed) { semaphore_destroy(mach_task_self(), mac_fault_completed); mac_fault_completed = 0; }
  semaphore_signal(e->thread.completed);
  mac_thread_release(e);
  return 0;
}

internal Thread
thread_launch(ThreadEntryPointFunctionType *f, void *p)
{
  MAC_Entity *e = mac_entity_alloc();
  e->thread.func = f; e->thread.ptr = p; e->thread.references = 2;
  if(semaphore_create(mach_task_self(), &e->thread.completed, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS)
  {
    mac_entity_release(e); return (Thread){0};
  }
  if(pthread_create(&e->thread.handle, 0, mac_thread_entry, e) != 0)
  {
    semaphore_destroy(mach_task_self(), e->thread.completed);
    mac_entity_release(e); return (Thread){0};
  }
  return (Thread){(U64)e};
}

internal B32
thread_join(Thread thread, U64 endt_us)
{
  MAC_Entity *e = (MAC_Entity *)thread.u64[0];
  if(!e || !mac_wait_semaphore(e->thread.completed, endt_us)) { return 0; }
  B32 result = pthread_join(e->thread.handle, 0) == 0;
  mac_thread_release(e);
  return result;
}
internal void
thread_detach(Thread thread)
{
  MAC_Entity *e = (MAC_Entity *)thread.u64[0];
  if(e) { pthread_detach(e->thread.handle); mac_thread_release(e); }
}

internal Mutex
mutex_alloc(void)
{
  MAC_Entity *e = mac_entity_alloc();
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  int r = pthread_mutex_init(&e->mutex, &attr);
  pthread_mutexattr_destroy(&attr);
  if(r) { mac_entity_release(e); e = 0; }
  return (Mutex){(U64)e};
}
internal void mutex_release(Mutex h) { if(h.u64[0]) { MAC_Entity *e = (void *)h.u64[0]; pthread_mutex_destroy(&e->mutex); mac_entity_release(e); } }
internal void mutex_take(Mutex h) { if(h.u64[0]) { pthread_mutex_lock(&((MAC_Entity *)h.u64[0])->mutex); } }
internal void mutex_drop(Mutex h) { if(h.u64[0]) { pthread_mutex_unlock(&((MAC_Entity *)h.u64[0])->mutex); } }
internal RWMutex
rw_mutex_alloc(void)
{
  MAC_Entity *e = mac_entity_alloc();
  if(pthread_rwlock_init(&e->rw_mutex, 0) != 0) { mac_entity_release(e); e = 0; }
  return (RWMutex){(U64)e};
}
internal void rw_mutex_release(RWMutex h) { if(h.u64[0]) { MAC_Entity *e = (void *)h.u64[0]; pthread_rwlock_destroy(&e->rw_mutex); mac_entity_release(e); } }
internal void
rw_mutex_take(RWMutex h, B32 write_mode)
{
  if(h.u64[0])
  {
    pthread_rwlock_t *m = &((MAC_Entity *)h.u64[0])->rw_mutex;
    if(write_mode) { pthread_rwlock_wrlock(m); } else { pthread_rwlock_rdlock(m); }
  }
}
internal void rw_mutex_drop(RWMutex h, B32 write_mode) { if(h.u64[0]) { pthread_rwlock_unlock(&((MAC_Entity *)h.u64[0])->rw_mutex); } }
internal CondVar
cond_var_alloc(void)
{
  MAC_Entity *e = mac_entity_alloc();
  int r = pthread_cond_init(&e->cv.handle, 0);
  if(r == 0)
  {
    r = pthread_mutex_init(&e->cv.rw_wait_mutex, 0);
    if(r) { pthread_cond_destroy(&e->cv.handle); }
  }
  if(r) { mac_entity_release(e); e = 0; }
  return (CondVar){(U64)e};
}
internal void
cond_var_release(CondVar h)
{
  if(h.u64[0])
  {
    MAC_Entity *e = (void *)h.u64[0];
    pthread_cond_destroy(&e->cv.handle); pthread_mutex_destroy(&e->cv.rw_wait_mutex);
    mac_entity_release(e);
  }
}
internal B32
cond_var_wait(CondVar cv, Mutex mutex, U64 endt_us)
{
  if(!cv.u64[0] || !mutex.u64[0]) { return 0; }
  return mac_cond_wait(&((MAC_Entity *)cv.u64[0])->cv.handle, &((MAC_Entity *)mutex.u64[0])->mutex, endt_us) == 0;
}
internal B32
cond_var_wait_rw(CondVar cv, RWMutex mutex, B32 write_mode, U64 endt_us)
{
  if(!cv.u64[0] || !mutex.u64[0]) { return 0; }
  MAC_Entity *e = (void *)cv.u64[0];
  pthread_mutex_lock(&e->cv.rw_wait_mutex);
  rw_mutex_drop(mutex, write_mode);
  int result = mac_cond_wait(&e->cv.handle, &e->cv.rw_wait_mutex, endt_us);
  // Drop the wait gate before reacquiring the caller's lock: broadcasters may
  // hold that lock and need the gate to complete the wakeup.
  pthread_mutex_unlock(&e->cv.rw_wait_mutex);
  rw_mutex_take(mutex, write_mode);
  return result == 0;
}
internal void
cond_var_signal(CondVar cv)
{
  if(cv.u64[0])
  {
    MAC_Entity *e = (void *)cv.u64[0];
    pthread_mutex_lock(&e->cv.rw_wait_mutex);
    pthread_cond_signal(&e->cv.handle);
    pthread_mutex_unlock(&e->cv.rw_wait_mutex);
  }
}
internal void
cond_var_broadcast(CondVar cv)
{
  if(cv.u64[0])
  {
    MAC_Entity *e = (void *)cv.u64[0];
    pthread_mutex_lock(&e->cv.rw_wait_mutex);
    pthread_cond_broadcast(&e->cv.handle);
    pthread_mutex_unlock(&e->cv.rw_wait_mutex);
  }
}

internal Semaphore
semaphore_alloc(U32 initial_count, U32 max_count, String8 name)
{
  MAC_Semaphore *s = calloc(1, sizeof(*s));
  if(!s) { return (Semaphore){0}; }
  if(name.size)
  {
    mac_ipc_name(s->name, name);
    s->named = sem_open(s->name, O_CREAT|O_EXCL, 0600, initial_count);
    s->owner = s->named != SEM_FAILED;
    if(s->named == SEM_FAILED && errno == EEXIST) { s->named = sem_open(s->name, 0); }
    if(s->named == SEM_FAILED) { free(s); return (Semaphore){0}; }
  }
  else if(semaphore_create(mach_task_self(), &s->local, SYNC_POLICY_FIFO, initial_count) != KERN_SUCCESS)
  {
    free(s); return (Semaphore){0};
  }
  if(s->owner)
  {
    pthread_mutex_lock(&mac_state.entity_mutex);
    DLLPushBack(mac_state.first_owned_semaphore, mac_state.last_owned_semaphore, s);
    pthread_mutex_unlock(&mac_state.entity_mutex);
  }
  return (Semaphore){(U64)s};
}
internal Semaphore
semaphore_open(String8 name)
{
  MAC_Semaphore *s = calloc(1, sizeof(*s));
  if(!s) { return (Semaphore){0}; }
  mac_ipc_name(s->name, name);
  s->named = sem_open(s->name, 0);
  if(s->named == SEM_FAILED) { free(s); s = 0; }
  return (Semaphore){(U64)s};
}
internal void
semaphore_close(Semaphore h)
{
  MAC_Semaphore *s = (void *)h.u64[0];
  if(s)
  {
    if(s->named)
    {
      sem_close(s->named);
      if(s->owner)
      {
        pthread_mutex_lock(&mac_state.entity_mutex);
        DLLRemove(mac_state.first_owned_semaphore, mac_state.last_owned_semaphore, s);
        pthread_mutex_unlock(&mac_state.entity_mutex);
        sem_unlink(s->name);
      }
    }
    else { semaphore_destroy(mach_task_self(), s->local); }
    free(s);
  }
}
internal void semaphore_release(Semaphore h) { semaphore_close(h); }
internal B32
semaphore_take(Semaphore h, U64 endt_us)
{
  MAC_Semaphore *s = (void *)h.u64[0];
  if(!s) { return 0; }
  if(!s->named) { return mac_wait_semaphore(s->local, endt_us); }
  if(endt_us == max_U64) { return MAC_RETRY_ON_EINTR(sem_wait(s->named)) == 0; }
  // Darwin's named semaphores have no timed-wait API.
  for(;;)
  {
    if(sem_trywait(s->named) == 0) { return 1; }
    if(errno != EAGAIN && errno != EINTR) { return 0; }
    U64 now = now_time_us();
    if(now >= endt_us) { return 0; }
    usleep((useconds_t)Min(endt_us-now, 1000));
  }
}
internal void
semaphore_drop_count(Semaphore h, U64 count)
{
  MAC_Semaphore *s = (void *)h.u64[0];
  if(s) for EachIndex(i, count)
  {
    if(s->named) { sem_post(s->named); } else { semaphore_signal(s->local); }
  }
}

internal Barrier
barrier_alloc(U64 count)
{
  if(!count) { return (Barrier){0}; }
  MAC_Entity *e = mac_entity_alloc();
  pthread_mutex_init(&e->barrier.mutex, 0);
  pthread_cond_init(&e->barrier.cv, 0);
  e->barrier.count = e->barrier.remaining = count;
  return (Barrier){(U64)e};
}
internal void
barrier_release(Barrier h)
{
  if(h.u64[0])
  {
    MAC_Entity *e = (void *)h.u64[0];
    pthread_cond_destroy(&e->barrier.cv); pthread_mutex_destroy(&e->barrier.mutex);
    mac_entity_release(e);
  }
}
internal void
barrier_wait(Barrier h)
{
  MAC_Entity *e = (void *)h.u64[0];
  if(!e) { return; }
  pthread_mutex_lock(&e->barrier.mutex);
  U64 generation = e->barrier.generation;
  if(--e->barrier.remaining == 0)
  {
    e->barrier.remaining = e->barrier.count;
    e->barrier.generation += 1;
    pthread_cond_broadcast(&e->barrier.cv);
  }
  else while(generation == e->barrier.generation) { pthread_cond_wait(&e->barrier.cv, &e->barrier.mutex); }
  pthread_mutex_unlock(&e->barrier.mutex);
}

internal void
safe_call(ThreadEntryPointFunctionType *func, ThreadEntryPointFunctionType *fail_handler, void *ptr)
{
  MAC_SafeCall call = {.next = mac_safe_call, .fail = fail_handler, .ptr = ptr};
  mac_safe_call = &call;
  if(sigsetjmp(call.jump, 1) == 0) { func(ptr); }
  mac_safe_call = call.next;
}

////////////////////////////////
// Files, maps, and directories (File handles retain POSIX fd values)

internal File
file_open(AccessFlags flags, String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  int f = (flags & AccessFlag_Write) ? ((flags & AccessFlag_Read) ? O_RDWR : O_WRONLY) : O_RDONLY;
  if(flags & (AccessFlag_Write|AccessFlag_Append)) { f |= O_CREAT; }
  if(flags & AccessFlag_Append) { f |= O_APPEND; }
  if(flags & AccessFlag_CreateNew) { f |= O_CREAT|O_EXCL; }
  int fd = MAC_RETRY_ON_EINTR(open((char *)push_cstr(scratch.arena, path).str, f|O_CLOEXEC, 0644));
  if(fd >= 0 && fd < 3) { int old = fd; fd = fcntl(fd, F_DUPFD_CLOEXEC, 3); close(old); }
  scratch_end(scratch);
  return (File){fd >= 0 ? (U64)fd : 0};
}
internal void file_close(File h) { if(h.u64[0]) { close((int)h.u64[0]); } }
internal FilePair
file_pipe_make(B32 read_inherited, B32 write_inherited)
{
  FilePair result = {0};
  int fds[2];
  if(pipe(fds) == 0)
  {
    for(U32 i = 0; i < 2; i += 1)
    {
      if(fds[i] < 3) { int old = fds[i]; fds[i] = fcntl(old, F_DUPFD_CLOEXEC, 3); close(old); }
    }
    if(fds[0] >= 0 && fds[1] >= 0)
    {
      fcntl(fds[0], F_SETFD, read_inherited ? 0 : FD_CLOEXEC);
      fcntl(fds[1], F_SETFD, write_inherited ? 0 : FD_CLOEXEC);
      result.read.u64[0] = fds[0]; result.write.u64[0] = fds[1];
    }
    else { if(fds[0] >= 0) { close(fds[0]); } if(fds[1] >= 0) { close(fds[1]); } }
  }
  return result;
}
internal U64 file_pipe_read(File h, void *dst, U64 size) { ssize_t n = MAC_RETRY_ON_EINTR(read((int)h.u64[0], dst, size)); return n > 0 ? (U64)n : 0; }
internal U64 file_pipe_write(File h, void *src, U64 size) { ssize_t n = MAC_RETRY_ON_EINTR(write((int)h.u64[0], src, size)); return n > 0 ? (U64)n : 0; }
internal U64 file_pipe_bytes_available(File h) { int n = 0; return h.u64[0] && ioctl((int)h.u64[0], FIONREAD, &n) == 0 && n > 0 ? (U64)n : 0; }
internal B32 file_pipe_is_end(File h) { struct pollfd p = {.fd = (int)h.u64[0], .events = POLLIN|POLLHUP}; poll(&p, 1, 0); return !!(p.revents & (POLLHUP|POLLERR|POLLNVAL)); }
internal U64
file_read(File h, Rng1U64 range, void *dst)
{
  if(!h.u64[0] || range.max < range.min || range.max > max_S64) { return 0; }
  U64 off = 0, size = dim_1u64(range);
  while(off < size)
  {
    ssize_t n = MAC_RETRY_ON_EINTR(pread((int)h.u64[0], (U8 *)dst+off, Min(size-off, MB(256)), range.min+off));
    if(n <= 0) { break; } off += n;
  }
  return off;
}
internal U64
file_write(File h, Rng1U64 range, void *src)
{
  if(!h.u64[0] || range.max < range.min || range.max > max_S64) { return 0; }
  U64 off = 0, size = dim_1u64(range);
  while(off < size)
  {
    ssize_t n = MAC_RETRY_ON_EINTR(pwrite((int)h.u64[0], (U8 *)src+off, Min(size-off, MB(256)), range.min+off));
    if(n <= 0) { break; } off += n;
  }
  return off;
}
internal B32
file_set_times(File h, DateTime dt)
{
  struct tm t = mac_tm_from_date_time(dt);
  struct timespec times[2] = {{timegm(&t), (long)dt.msec*1000000}, {0}};
  times[1] = times[0];
  return h.u64[0] && futimens((int)h.u64[0], times) == 0;
}
internal FileProperties properties_from_file(File h) { struct stat s; return h.u64[0] && fstat((int)h.u64[0], &s) == 0 ? mac_properties(&s) : (FileProperties){0}; }
internal FileID
id_from_file(File h)
{
  FileID id = {0}; struct stat s;
  if(h.u64[0] && fstat((int)h.u64[0], &s) == 0) { id.v[0] = s.st_dev; id.v[1] = s.st_ino; }
  return id;
}
internal B32 file_set_size(File h, U64 size) { return h.u64[0] && size <= max_S64 && ftruncate((int)h.u64[0], size) == 0; }
internal B32 file_flush(File h) { return h.u64[0] && fsync((int)h.u64[0]) == 0; }
internal B32
file_reserve_size(File h, U64 size)
{
  if(!h.u64[0] || size > max_S64) { return 0; }
  fstore_t store = {.fst_flags = F_ALLOCATEALL, .fst_posmode = F_PEOFPOSMODE, .fst_length = (off_t)size};
  return fcntl((int)h.u64[0], F_PREALLOCATE, &store) == 0;
}
internal B32
delete_file_at_path(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  B32 r = remove((char *)push_cstr(scratch.arena, path).str) == 0;
  scratch_end(scratch); return r;
}
internal B32
copy_file_path(String8 dst, String8 src)
{
  Temp scratch = scratch_begin(0, 0);
  B32 r = copyfile((char *)push_cstr(scratch.arena, src).str, (char *)push_cstr(scratch.arena, dst).str, 0, COPYFILE_ALL) == 0;
  scratch_end(scratch); return r;
}
internal B32
move_file_path(String8 dst, String8 src)
{
  Temp scratch = scratch_begin(0, 0);
  B32 r = rename((char *)push_cstr(scratch.arena, src).str, (char *)push_cstr(scratch.arena, dst).str) == 0;
  scratch_end(scratch); return r;
}
internal B32 replace_file_path(String8 dst, String8 src) { return move_file_path(dst, src); }
internal String8
full_path_from_path(Arena *arena, String8 path)
{
  Temp scratch = scratch_begin(&arena, 1);
  char resolved[PATH_MAX];
  String8 result = str8_zero();
  if(realpath((char *)push_cstr(scratch.arena, path).str, resolved)) { result = str8_copy(arena, str8_cstring(resolved)); }
  scratch_end(scratch); return result;
}
internal FileProperties
properties_from_file_path(String8 path)
{
  Temp scratch = scratch_begin(0, 0); struct stat s;
  FileProperties p = {0};
  if(stat((char *)push_cstr(scratch.arena, path).str, &s) == 0) { p = mac_properties(&s); }
  scratch_end(scratch); return p;
}
internal B32
file_path_exists(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  B32 r = access((char *)push_cstr(scratch.arena, path).str, F_OK) == 0;
  scratch_end(scratch); return r;
}
internal B32 folder_path_exists(String8 path) { return !!(properties_from_file_path(path).flags & FilePropertyFlag_IsFolder); }
internal FileMap file_map_open(AccessFlags flags, File file) { return (FileMap){file.u64[0]}; }
internal void file_map_close(FileMap map) { /* POSIX maps do not own a second descriptor. */ }
internal void *
file_map_view_open(FileMap map, AccessFlags flags, Rng1U64 range)
{
  if(!map.u64[0] || range.max <= range.min) { return 0; }
  int prot = ((flags & AccessFlag_Read) ? PROT_READ : 0) | ((flags & AccessFlag_Write) ? PROT_WRITE : 0);
  void *p = mmap(0, dim_1u64(range), prot, MAP_PRIVATE, (int)map.u64[0], range.min);
  return p == MAP_FAILED ? 0 : p;
}
internal void *
file_map_view_replace_placeholder(FileMap map, void *ptr, Rng1U64 range)
{
  if(!map.u64[0] || range.max <= range.min) { return 0; }
  void *p = mmap(ptr, dim_1u64(range), PROT_READ, MAP_FIXED|MAP_PRIVATE, (int)map.u64[0], range.min);
  return p == MAP_FAILED ? 0 : p;
}
internal void file_map_view_close(FileMap map, void *ptr, Rng1U64 range) { if(ptr) { munmap(ptr, dim_1u64(range)); } }
internal FileIter *
file_iter_begin(Arena *arena, String8 path, FileIterFlags flags)
{
  FileIter *it = push_array(arena, FileIter, 1);
  it->flags = flags;
  MAC_FileIter *m = (void *)it->memory;
  m->path = push_cstr(arena, path); m->dir = opendir((char *)m->path.str);
  return it;
}
internal B32
file_iter_next(Arena *arena, FileIter *it, FileInfo *out)
{
  MAC_FileIter *m = (void *)it->memory;
  if(!m->dir || (it->flags & FileIterFlag_Done)) { return 0; }
  struct dirent *entry;
  while((entry = readdir(m->dir)) != 0)
  {
    if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) { continue; }
    if((it->flags & FileIterFlag_SkipHiddenFiles) && entry->d_name[0] == '.') { continue; }
    struct stat s;
    if(fstatat(dirfd(m->dir), entry->d_name, &s, 0) != 0) { continue; }
    if(S_ISDIR(s.st_mode) ? !!(it->flags & FileIterFlag_SkipFolders) : !!(it->flags & FileIterFlag_SkipFiles)) { continue; }
    out->name = str8_copy(arena, str8_cstring(entry->d_name)); out->props = mac_properties(&s);
    return 1;
  }
  it->flags |= FileIterFlag_Done; return 0;
}
internal void file_iter_end(FileIter *it) { MAC_FileIter *m = (void *)it->memory; if(m->dir) { closedir(m->dir); m->dir = 0; } }
internal B32
make_directory(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  B32 r = mkdir((char *)push_cstr(scratch.arena, path).str, 0755) == 0;
  scratch_end(scratch); return r;
}

////////////////////////////////
// Shared memory

internal SharedMemory
shared_memory_alloc(U64 size, String8 name)
{
  if(!size || size > max_S64) { return (SharedMemory){0}; }
  MAC_SharedMemory *m = calloc(1, sizeof(*m));
  if(!m) { return (SharedMemory){0}; }
  if(name.size) { mac_ipc_name(m->name, name); }
  else { snprintf(m->name, sizeof(m->name), "/rad_%x_%08x", getpid(), arc4random()); }
  m->fd = shm_open(m->name, O_RDWR|O_CREAT|O_EXCL, 0600);
  m->owner = m->fd >= 0;
  if(m->fd < 0 && errno == EEXIST && name.size) { m->fd = shm_open(m->name, O_RDWR, 0600); }
  if(m->fd < 0 || ftruncate(m->fd, size) != 0)
  {
    if(m->fd >= 0) { close(m->fd); } if(m->owner) { shm_unlink(m->name); }
    free(m); return (SharedMemory){0};
  }
  fcntl(m->fd, F_SETFD, FD_CLOEXEC);
  if(!name.size) { shm_unlink(m->name); m->owner = 0; }
  if(m->owner)
  {
    pthread_mutex_lock(&mac_state.entity_mutex);
    DLLPushBack(mac_state.first_owned_shared_memory, mac_state.last_owned_shared_memory, m);
    pthread_mutex_unlock(&mac_state.entity_mutex);
  }
  return (SharedMemory){(U64)m};
}
internal SharedMemory
shared_memory_open(String8 name)
{
  MAC_SharedMemory *m = calloc(1, sizeof(*m));
  if(!m) { return (SharedMemory){0}; }
  mac_ipc_name(m->name, name); m->fd = shm_open(m->name, O_RDWR, 0600);
  if(m->fd < 0) { free(m); m = 0; } else { fcntl(m->fd, F_SETFD, FD_CLOEXEC); }
  return (SharedMemory){(U64)m};
}
internal void
shared_memory_close(SharedMemory h)
{
  MAC_SharedMemory *m = (void *)h.u64[0];
  if(m)
  {
    if(m->owner)
    {
      pthread_mutex_lock(&mac_state.entity_mutex);
      DLLRemove(mac_state.first_owned_shared_memory, mac_state.last_owned_shared_memory, m);
      pthread_mutex_unlock(&mac_state.entity_mutex);
      shm_unlink(m->name);
    }
    close(m->fd);
    free(m);
  }
}
internal void *
shared_memory_view_open(SharedMemory h, Rng1U64 range)
{
  MAC_SharedMemory *m = (void *)h.u64[0];
  if(!m || range.max <= range.min) { return 0; }
  void *p = mmap(0, dim_1u64(range), PROT_READ|PROT_WRITE, MAP_SHARED, m->fd, range.min);
  return p == MAP_FAILED ? 0 : p;
}
internal void shared_memory_view_close(SharedMemory h, void *p, Rng1U64 range) { if(p) { munmap(p, dim_1u64(range)); } }
internal void *
shared_memory_view_replace_placeholder(SharedMemory h, void *ptr, Rng1U64 range, AccessFlags flags)
{
  MAC_SharedMemory *m = (void *)h.u64[0];
  if(!m || range.max <= range.min) { return 0; }
  int prot = PROT_READ | ((flags & AccessFlag_Write) ? PROT_WRITE : 0);
  void *p = mmap(ptr, dim_1u64(range), prot, MAP_SHARED|MAP_FIXED, m->fd, range.min);
  return p == MAP_FAILED ? 0 : p;
}

////////////////////////////////
// Child processes and dynamic libraries

typedef struct MAC_ProcessGroup MAC_ProcessGroup;
struct MAC_ProcessGroup { pid_t pid; B32 kill_on_close; };

internal Process
process_launch(ProcessLaunchParams *params)
{
  Process result = {0};
  if(!params->cmd_line.first || !params->cmd_line.first->string.size) { return result; }
  Temp scratch = scratch_begin(0, 0);
  posix_spawn_file_actions_t actions;
  posix_spawnattr_t attr;
  if(posix_spawn_file_actions_init(&actions) != 0) { scratch_end(scratch); return result; }
  if(posix_spawnattr_init(&attr) != 0) { posix_spawn_file_actions_destroy(&actions); scratch_end(scratch); return result; }
  int error = 0;
  if(params->path.size) { error = posix_spawn_file_actions_addchdir_np(&actions, (char *)push_cstr(scratch.arena, params->path).str); }
  File files[] = {params->stdin_file, params->stdout_file, params->stderr_file};
  for EachIndex(i, ArrayCount(files)) if(files[i].u64[0] && !error)
  {
    error = posix_spawn_file_actions_adddup2(&actions, (int)files[i].u64[0], (int)i);
  }
  for EachIndex(i, ArrayCount(files))
  {
    B32 unique = files[i].u64[0] > 2;
    for EachIndex(j, i) { unique &= files[i].u64[0] != files[j].u64[0]; }
    if(unique && !error) { error = posix_spawn_file_actions_addclose(&actions, (int)files[i].u64[0]); }
  }
  if(params->process_group.u64[0] || params->new_console)
  {
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
  }
  char **argv = push_array(scratch.arena, char *, params->cmd_line.node_count+1);
  U64 i = 0;
  for EachNode(n, String8Node, params->cmd_line.first) { argv[i++] = (char *)push_cstr(scratch.arena, n->string).str; }
  // Explicit values override inherited environment entries, including PATH.
  char **env = push_array(scratch.arena, char *, (params->inherit_env ? mac_state.default_env_count : 0)+params->env.node_count+1);
  U64 env_count = 0;
  if(params->inherit_env) for EachIndex(j, mac_state.default_env_count) { env[env_count++] = mac_state.default_env[j]; }
  for EachNode(n, String8Node, params->env.first)
  {
    String8 value = push_cstr(scratch.arena, n->string);
    U64 equal = str8_find_needle(value, 0, str8_lit("="), 0);
    B32 replaced = 0;
    if(equal < value.size) for EachIndex(j, env_count)
    {
      if(!strncmp(env[j], (char *)value.str, equal) && env[j][equal] == '=') { env[j] = (char *)value.str; replaced = 1; break; }
    }
    if(!replaced) { env[env_count++] = (char *)value.str; }
  }
  pid_t pid = 0;
  if(!error && posix_spawnp(&pid, argv[0], &actions, &attr, argv, env) == 0)
  {
    result.u64[0] = pid;
    if(params->process_group.u64[0] && !process_group_add(params->process_group, result))
    {
      kill(pid, SIGKILL); MAC_RETRY_ON_EINTR(waitpid(pid, 0, 0)); result.u64[0] = 0;
    }
  }
  posix_spawnattr_destroy(&attr); posix_spawn_file_actions_destroy(&actions);
  scratch_end(scratch); return result;
}
internal U64 pid_from_process(Process p) { return p.u64[0]; }
internal B32
process_poll(Process p, U64 *exit_code)
{
  if(!p.u64[0]) { return 0; }
  siginfo_t info = {0};
  B32 done = waitid(P_PID, (id_t)p.u64[0], &info, WEXITED|WNOHANG|WNOWAIT) == 0 && info.si_pid != 0;
  if(done && exit_code) { *exit_code = info.si_code == CLD_EXITED ? info.si_status : 128+info.si_status; }
  return done;
}
internal B32 process_is_active(Process p) { return p.u64[0] && !process_poll(p, 0) && kill((pid_t)p.u64[0], 0) == 0; }
internal B32
process_join(Process p, U64 endt_us, U64 *exit_code)
{
  if(!p.u64[0]) { return 0; }
  for(;;)
  {
    int status = 0;
    pid_t pid = MAC_RETRY_ON_EINTR(waitpid((pid_t)p.u64[0], &status, endt_us == max_U64 ? 0 : WNOHANG));
    if(pid == (pid_t)p.u64[0] && (WIFEXITED(status) || WIFSIGNALED(status)))
    {
      if(exit_code) { *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128+WTERMSIG(status); }
      return 1;
    }
    if(pid == -1 || now_time_us() >= endt_us) { return 0; }
    usleep(1000);
  }
}
internal void mac_reap_process(void *p) { MAC_RETRY_ON_EINTR(waitpid((pid_t)IntFromPtr(p), 0, 0)); }
internal void process_detach(Process p) { if(p.u64[0]) { thread_detach(thread_launch(mac_reap_process, PtrFromInt(p.u64[0]))); } }
internal B32 process_kill(Process p) { return p.u64[0] && kill((pid_t)p.u64[0], SIGKILL) == 0; }
internal B32 process_send_ctrl_c(Process p) { return p.u64[0] && kill((pid_t)p.u64[0], SIGINT) == 0; }
internal ProcessGroup
process_group_make(B32 kill_on_close)
{
  MAC_ProcessGroup *g = calloc(1, sizeof(*g));
  if(g) { g->kill_on_close = kill_on_close; }
  return (ProcessGroup){(U64)g};
}
internal B32
process_group_add(ProcessGroup h, Process p)
{
  MAC_ProcessGroup *g = (void *)h.u64[0];
  if(!g || !p.u64[0] || g->pid) { return 0; }
  g->pid = (pid_t)p.u64[0]; return 1;
}
internal void
process_group_close(ProcessGroup h)
{
  MAC_ProcessGroup *g = (void *)h.u64[0];
  if(g) { if(g->kill_on_close && g->pid) { kill(-g->pid, SIGKILL); } free(g); }
}
internal void abort_self(U64 code) { _exit((int)code); }
internal Library
library_open(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  void *p = dlopen((char *)push_cstr(scratch.arena, path).str, RTLD_LAZY|RTLD_LOCAL);
  scratch_end(scratch); return (Library){(U64)p};
}
internal void library_close(Library h) { if(h.u64[0]) { dlclose((void *)h.u64[0]); } }
internal VoidProc *
library_load_proc(Library h, String8 name)
{
  if(!h.u64[0]) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  VoidProc *p = (VoidProc *)dlsym((void *)h.u64[0], (char *)push_cstr(scratch.arena, name).str);
  scratch_end(scratch); return p;
}

////////////////////////////////
// Initialization and fault dispatch

internal void
mac_signal_handler(int sig, siginfo_t *info, void *context)
{
  if(mac_dispatch_fault(sig, info, context)) { return; }
  if(mac_safe_call)
  {
    MAC_SafeCall *call = mac_safe_call;
    mac_safe_call = call->next;
    if(call->fail) { call->fail(call->ptr); }
    siglongjmp(call->jump, 1);
  }
  // Restore the default disposition so macOS generates its normal crash report.
  struct sigaction action = {.sa_handler = SIG_DFL};
  sigemptyset(&action.sa_mask); sigaction(sig, &action, 0);
  kill(getpid(), sig);
}

int
main(int argc, char **argv)
{
  struct sigaction action = {.sa_sigaction = mac_signal_handler, .sa_flags = SA_SIGINFO};
  sigemptyset(&action.sa_mask);
  int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
  for EachIndex(i, ArrayCount(signals)) { sigaction(signals[i], &action, 0); }
  signal(SIGPIPE, SIG_IGN);
  SystemInfo *system = &mac_state.system_info;
  system->page_size = getpagesize(); system->allocation_granularity = system->page_size;
  system->large_page_size = MB(2);
  system->logical_processor_count = (U32)Max(1, sysconf(_SC_NPROCESSORS_ONLN));
  size_t size = sizeof(system->physical_memory_size);
  sysctlbyname("hw.memsize", &system->physical_memory_size, &size, 0, 0);
  TCTX *tctx = tctx_alloc(); tctx_select(tctx);
  mac_state.arena = arena_alloc(); mac_state.entity_arena = arena_alloc();
  pthread_mutex_init(&mac_state.entity_mutex, 0);
  char host[256];
  if(gethostname(host, sizeof(host)) == 0) { host[sizeof(host)-1] = 0; system->machine_name = str8_copy(mac_state.arena, str8_cstring(host)); }
  extern char **environ;
  while(environ[mac_state.default_env_count]) { mac_state.default_env_count += 1; }
  mac_state.default_env = push_array(mac_state.arena, char *, mac_state.default_env_count+1);
  ProcessInfo *process = &mac_state.process_info;
  process->pid = getpid();
  atexit(mac_cleanup_ipc);
  for EachIndex(i, mac_state.default_env_count)
  {
    String8 value = str8_copy(mac_state.arena, str8_cstring(environ[i]));
    mac_state.default_env[i] = (char *)value.str;
    str8_list_push(mac_state.arena, &process->environment, value);
  }
  uint32_t path_size = 0;
  _NSGetExecutablePath(0, &path_size);
  char *binary = push_array_no_zero(mac_state.arena, char, path_size);
  if(_NSGetExecutablePath(binary, &path_size) == 0)
  {
    process->binary_file_path = full_path_from_path(mac_state.arena, str8_cstring(binary));
    process->binary_path = str8_chop_last_slash(process->binary_file_path);
  }
  process->initial_path = get_current_path(mac_state.arena);
  const char *home = getenv("HOME");
  if(!home) { home = "/tmp"; }
  process->user_program_config_data_path = str8f(mac_state.arena, "%s/Library/Application Support", home);
  process->user_program_cache_data_path = str8f(mac_state.arena, "%s/Library/Caches", home);
  process->user_program_logs_data_path = str8f(mac_state.arena, "%s/Library/Logs", home);
#if defined(__OBJC__)
  @autoreleasepool { main_thread_base_entry_point(argc, argv); }
#else
  main_thread_base_entry_point(argc, argv);
#endif
  memory_read_fault_handler_set(0, 0);
  if(mac_fault_completed) { semaphore_destroy(mach_task_self(), mac_fault_completed); }
  return 0;
}

internal void
mac_cleanup_ipc(void)
{
  if(mac_state.process_info.pid != (U32)getpid()) { return; }
  pthread_mutex_lock(&mac_state.entity_mutex);
  for EachNode(s, MAC_Semaphore, mac_state.first_owned_semaphore) { sem_unlink(s->name); }
  for EachNode(m, MAC_SharedMemory, mac_state.first_owned_shared_memory) { shm_unlink(m->name); }
  pthread_mutex_unlock(&mac_state.entity_mutex);
}
