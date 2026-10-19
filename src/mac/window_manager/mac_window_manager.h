// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)
#ifndef MAC_WINDOW_MANAGER_H
#define MAC_WINDOW_MANAGER_H
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

typedef struct Mac_WM_ClientArea Mac_WM_ClientArea;
struct Mac_WM_ClientArea
{
  Mac_WM_ClientArea *next;
  Rng2F32 rect;
};
typedef struct Mac_WM_Window Mac_WM_Window;
struct Mac_WM_Window
{
  Mac_WM_Window *next;
  NSWindow *window;
  NSView *view;
  Arena *border_arena;
  Mac_WM_ClientArea *first_client_area;
  F32 title_thickness;
  F32 edge_thickness;
  B32 custom_border;
  B32 shown;
};
typedef struct Mac_WM_State Mac_WM_State;
struct Mac_WM_State
{
  Arena *event_arena;
  WM_EventList events;
  WM_SystemInfo info;
  Mac_WM_Window *first_window;
  id delegate;
  B32 keys[WM_Key_COUNT];
  B32 hardware_keys[128];
  B32 delivered_keys[128];
  B32 resizing;
  WM_Cursor cursor;
};
global Mac_WM_State *mac_wm_state;
#endif
