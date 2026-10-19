// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MAC_SHELL_H
#define MAC_SHELL_H

#undef internal
#import <Cocoa/Cocoa.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
#define internal static

typedef enum Mac_SH_Operation
{
  Mac_SH_Operation_Message,
  Mac_SH_Operation_PickFile,
  Mac_SH_Operation_Reveal,
  Mac_SH_Operation_Browse,
} Mac_SH_Operation;

typedef struct Mac_SH_Call Mac_SH_Call;
struct Mac_SH_Call
{
  Mac_SH_Operation operation;
  Arena *arena;
  B32 error;
  String8 title;
  String8 string;
  String8 result;
};

internal void sh_mac_call_main(void *context);

internal NSString *sh_mac_string(String8 string);
internal B32 sh_mac_managed_install(NSString *receipt_path, NSString *bundle_path);
internal NSString *sh_mac_bundle_executable(NSString *bundle_path);

#endif // MAC_SHELL_H
