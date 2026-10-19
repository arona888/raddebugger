// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef BASE_INC_H
#define BASE_INC_H

////////////////////////////////
//~ rjf: Base Includes

#include "base_context_cracking.h"

// Import the Apple SDK before the codebase's keyword macros. Carbon's FileInfo
// is unrelated to our file iterator record and is not used by the backends.
#if OS_MAC && defined(__OBJC__)
# define FileInfo MacSDKFileInfo
# import <Cocoa/Cocoa.h>
# import <CoreText/CoreText.h>
# import <CoreServices/CoreServices.h>
# undef FileInfo
# undef nil
// Keep nil usable in Objective-C without macro-expanding C identifiers and
// token-pasted rule names in the platform-independent unity source.
static id const nil = (id)0;
#endif

#include "base_core.h"
#include "base_profile.h"
#include "base_memory.h"
#include "base_arena.h"
#include "base_math.h"
#include "base_strings.h"
#include "base_memory_map.h"
#include "base_hash.h"
#include "base_system.h"
#include "base_threads.h"
#include "base_ring.h"
#include "base_thread_context.h"
#include "base_files.h"
#include "base_shared_memory.h"
#include "base_processes.h"
#include "base_dynamic_libraries.h"
#include "base_command_line.h"
#include "base_markup.h"
#include "base_meta.h"
#include "base_log.h"
#include "base_test.h"
#include "base_entry_point.h"

#if OS_WINDOWS
# include "win32/base/win32_base.h"
#elif OS_LINUX
# include "linux/base/linux_base.h"
#elif OS_MAC
# include "mac/base/mac_base.h"
#else
# error Operating system backend not found for base layer.
#endif

#endif // BASE_INC_H
