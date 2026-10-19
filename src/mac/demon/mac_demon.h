// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)
#ifndef MAC_DEMON_H
#define MAC_DEMON_H

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/exc.h>
#include <mach/arm/thread_status.h>
#include <mach-o/loader.h>
#include <mach-o/dyld_images.h>
#include <sys/ptrace.h>
#include <sys/event.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <spawn.h>
#include <fcntl.h>
#include <libproc.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// Mach MIG requests are packed to four-byte alignment, including the 64-bit codes.
#pragma pack(push, 4)
typedef struct MAC_DMN_Exception MAC_DMN_Exception;
struct MAC_DMN_Exception
{
  mach_msg_header_t head;
  mach_msg_body_t body;
  mach_msg_port_descriptor_t thread;
  mach_msg_port_descriptor_t task;
  NDR_record_t ndr;
  exception_type_t exception;
  mach_msg_type_number_t code_count;
  U64 code[2];
};
#pragma pack(pop)

typedef enum MAC_DMN_Kind
{
  MAC_DMN_Kind_Null,
  MAC_DMN_Kind_Process,
  MAC_DMN_Kind_Thread,
  MAC_DMN_Kind_Module,
} MAC_DMN_Kind;

typedef struct MAC_DMN_Entity MAC_DMN_Entity;
struct MAC_DMN_Entity
{
  MAC_DMN_Entity *next;
  MAC_DMN_Entity *parent;
  U32 generation;
  MAC_DMN_Kind kind;
  mach_port_t port;
  mach_port_t exception_port;
  U64 id;
  B32 seen;
  B32 suspended;
  B32 announced;
  B32 detached;
  B32 launched;
  B32 handshake;
  B32 pending_exception;
  B32 exception_is_user;
  B32 initial_sigstop;
  U32 exit_code;
  B32 suppress_spawn_sigcont;
  MAC_DMN_Exception exception;
  exception_mask_t old_masks[EXC_TYPES_COUNT];
  mach_port_t old_ports[EXC_TYPES_COUNT];
  exception_behavior_t old_behaviors[EXC_TYPES_COUNT];
  thread_state_flavor_t old_flavors[EXC_TYPES_COUNT];
  mach_msg_type_number_t old_count;
  U64 base;
  U64 entry;
  U64 notifier;
  U64 step_over;
  U64 tls_descriptors;
  U64 tls_descriptors_size;
  DMN_ModuleInfo info;
  char *path;
};

typedef struct MAC_DMN_State MAC_DMN_State;
struct MAC_DMN_State
{
  Arena *arena;
  MAC_DMN_Entity *entities;
  MAC_DMN_Entity *free_entities;
  U64 entity_count;
  Mutex access_mutex;
  Mutex halt_mutex;
  B32 running;
  B32 halt_requested;
  U64 halt_code;
  U64 halt_user_data;
  mach_port_t exception_port;
  mach_port_t exception_port_set;
  int kqueue;
  DMN_CtrlCtx context;
};

global MAC_DMN_State *mac_dmn;
thread_static B32 mac_dmn_control_thread;
thread_static U32 mac_dmn_access_depth;
thread_static B32 mac_dmn_access_allowed;

#endif // MAC_DEMON_H
