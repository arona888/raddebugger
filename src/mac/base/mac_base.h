// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_BASE_H
#define MAC_BASE_H

#pragma push_macro("internal")
#undef internal
#include <copyfile.h>
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/semaphore.h>
#include <mach/task_info.h>
#include <mach/vm_statistics.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <setjmp.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <sys/ucontext.h>
#pragma pop_macro("internal")

#define MAC_RETRY_ON_EINTR(expr) (__extension__({ __typeof__(expr) _r; do { _r = (expr); } while(_r == -1 && errno == EINTR); _r; }))

typedef struct MAC_FileIter MAC_FileIter;
struct MAC_FileIter
{
  DIR *dir;
  String8 path;
};
StaticAssert(sizeof(Member(FileIter, memory)) >= sizeof(MAC_FileIter), mac_file_iter_size_check);

typedef struct MAC_Entity MAC_Entity;
struct MAC_Entity
{
  MAC_Entity *next;
  union
  {
    struct
    {
      pthread_t handle;
      ThreadEntryPointFunctionType *func;
      void *ptr;
      semaphore_t completed;
      U32 references;
    } thread;
    pthread_mutex_t mutex;
    pthread_rwlock_t rw_mutex;
    struct
    {
      pthread_cond_t handle;
      pthread_mutex_t rw_wait_mutex;
    } cv;
    struct
    {
      pthread_mutex_t mutex;
      pthread_cond_t cv;
      U64 count;
      U64 remaining;
      U64 generation;
    } barrier;
  };
};

typedef struct MAC_Semaphore MAC_Semaphore;
struct MAC_Semaphore
{
  MAC_Semaphore *next;
  MAC_Semaphore *prev;
  sem_t *named;
  semaphore_t local;
  B32 owner;
  char name[32];
};

typedef struct MAC_SharedMemory MAC_SharedMemory;
struct MAC_SharedMemory
{
  MAC_SharedMemory *next;
  MAC_SharedMemory *prev;
  int fd;
  B32 owner;
  char name[32];
};

typedef struct MAC_SafeCall MAC_SafeCall;
struct MAC_SafeCall
{
  MAC_SafeCall *next;
  ThreadEntryPointFunctionType *fail;
  void *ptr;
  sigjmp_buf jump;
};

typedef struct MAC_FaultRequest MAC_FaultRequest;
struct MAC_FaultRequest
{
  void *address;
  B32 handled;
  semaphore_t completed;
};

typedef struct MAC_State MAC_State;
struct MAC_State
{
  SystemInfo system_info;
  ProcessInfo process_info;
  Arena *arena;
  Arena *entity_arena;
  MAC_Entity *free_entity;
  pthread_mutex_t entity_mutex;
  MAC_Semaphore *first_owned_semaphore;
  MAC_Semaphore *last_owned_semaphore;
  MAC_SharedMemory *first_owned_shared_memory;
  MAC_SharedMemory *last_owned_shared_memory;
  char **default_env;
  U64 default_env_count;
  struct
  {
    MemoryReadFaultFunction *func;
    void *user_data;
    int requests[2];
    Thread workers[8];
    U32 worker_count;
    U32 active;
    pid_t owner_pid;
  } pager;
};

global MAC_State mac_state;
thread_static MAC_SafeCall *mac_safe_call;
thread_static semaphore_t mac_fault_completed;
thread_static B32 mac_in_fault_callback;

internal void mac_signal_handler(int sig, siginfo_t *info, void *context);
internal void mac_fault_worker(void *ptr);
internal void mac_cleanup_ipc(void);
internal MAC_Entity *mac_entity_alloc(void);
internal void mac_entity_release(MAC_Entity *entity);

#endif // MAC_BASE_H
