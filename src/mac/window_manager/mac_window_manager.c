// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal NSString *
mac_wm_nsstring(String8 string)
{
  return [[[NSString alloc] initWithBytes:string.str length:string.size encoding:NSUTF8StringEncoding] autorelease];
}

internal WM_Modifiers
mac_wm_modifiers(NSEventModifierFlags flags)
{
  WM_Modifiers result = 0;
  if(flags & (NSEventModifierFlagControl|NSEventModifierFlagCommand)) result |= WM_Modifier_Ctrl;
  if(flags & NSEventModifierFlagShift) result |= WM_Modifier_Shift;
  if(flags & NSEventModifierFlagOption) result |= WM_Modifier_Alt;
  return result;
}

internal WM_Key
mac_wm_key(U16 code)
{
  // Hardware positions define shortcuts; text itself is translated by Cocoa's input context.
  local_persist WM_Key table[128] =
  {
    [0]=WM_Key_A, [1]=WM_Key_S, [2]=WM_Key_D, [3]=WM_Key_F, [4]=WM_Key_H, [5]=WM_Key_G,
    [6]=WM_Key_Z, [7]=WM_Key_X, [8]=WM_Key_C, [9]=WM_Key_V, [11]=WM_Key_B,
    [12]=WM_Key_Q, [13]=WM_Key_W, [14]=WM_Key_E, [15]=WM_Key_R, [16]=WM_Key_Y, [17]=WM_Key_T,
    [18]=WM_Key_1, [19]=WM_Key_2, [20]=WM_Key_3, [21]=WM_Key_4, [22]=WM_Key_6, [23]=WM_Key_5,
    [24]=WM_Key_Equal, [25]=WM_Key_9, [26]=WM_Key_7, [27]=WM_Key_Minus, [28]=WM_Key_8, [29]=WM_Key_0,
    [30]=WM_Key_RightBracket, [31]=WM_Key_O, [32]=WM_Key_U, [33]=WM_Key_LeftBracket,
    [34]=WM_Key_I, [35]=WM_Key_P, [36]=WM_Key_Return, [37]=WM_Key_L, [38]=WM_Key_J,
    [39]=WM_Key_Quote, [40]=WM_Key_K, [41]=WM_Key_Semicolon, [42]=WM_Key_BackSlash,
    [43]=WM_Key_Comma, [44]=WM_Key_Slash, [45]=WM_Key_N, [46]=WM_Key_M, [47]=WM_Key_Period,
    [48]=WM_Key_Tab, [49]=WM_Key_Space, [50]=WM_Key_Tick, [51]=WM_Key_Backspace,
    [53]=WM_Key_Esc, [54]=WM_Key_Ctrl, [55]=WM_Key_Ctrl, [56]=WM_Key_Shift,
    [57]=WM_Key_CapsLock, [58]=WM_Key_Alt, [59]=WM_Key_Ctrl, [60]=WM_Key_Shift,
    [61]=WM_Key_Alt, [62]=WM_Key_Ctrl, [64]=WM_Key_F17, [65]=WM_Key_NumPeriod,
    [67]=WM_Key_NumStar, [69]=WM_Key_NumPlus, [71]=WM_Key_NumLock, [75]=WM_Key_NumSlash,
    [76]=WM_Key_Return, [78]=WM_Key_NumMinus, [79]=WM_Key_F18, [80]=WM_Key_F19,
    [81]=WM_Key_Equal, [82]=WM_Key_Num0, [83]=WM_Key_Num1, [84]=WM_Key_Num2,
    [85]=WM_Key_Num3, [86]=WM_Key_Num4, [87]=WM_Key_Num5, [88]=WM_Key_Num6,
    [89]=WM_Key_Num7, [91]=WM_Key_Num8, [92]=WM_Key_Num9, [96]=WM_Key_F5,
    [97]=WM_Key_F6, [98]=WM_Key_F7, [99]=WM_Key_F3, [100]=WM_Key_F8,
    [101]=WM_Key_F9, [103]=WM_Key_F11, [105]=WM_Key_F13, [106]=WM_Key_F16,
    [107]=WM_Key_F14, [109]=WM_Key_F10, [111]=WM_Key_F12, [113]=WM_Key_F15,
    [114]=WM_Key_Insert, [115]=WM_Key_Home, [116]=WM_Key_PageUp, [117]=WM_Key_Delete,
    [118]=WM_Key_F4, [119]=WM_Key_End, [120]=WM_Key_F2, [121]=WM_Key_PageDown,
    [122]=WM_Key_F1, [123]=WM_Key_Left, [124]=WM_Key_Right, [125]=WM_Key_Down, [126]=WM_Key_Up,
  };
  return code < ArrayCount(table) ? table[code] : WM_Key_Null;
}

internal WM_Event *
mac_wm_event(Mac_WM_Window *w, WM_EventKind kind)
{
  WM_Event *e = wm_event_list_push_new(mac_wm_state->event_arena, &mac_wm_state->events, kind);
  e->window.u64[0] = (U64)w;
  e->modifiers = mac_wm_modifiers([NSEvent modifierFlags]);
  e->repeat_count = 1;
  return e;
}

internal Vec2F32
mac_wm_pos(Mac_WM_Window *w, NSPoint point)
{
  NSPoint p = [w->view convertPoint:point fromView:nil];
  CGFloat scale = w->window.backingScaleFactor;
  // RAD uses top-down pixels; flipped-view backing coordinates have negative Y.
  return v2f32(p.x*scale, p.y*scale);
}

internal WM_Event *
mac_wm_keyboard(Mac_WM_Window *w, NSEvent *event, B32 down)
{
  U16 code = event.keyCode;
  WM_Key key = mac_wm_key(code);
  WM_Event *result = 0;
  B32 deliver = down || (code < 128 && mac_wm_state->delivered_keys[code]);
  if(code < 128) mac_wm_state->hardware_keys[code] = down;
  if(key != WM_Key_Null)
  {
    B32 any_down = 0;
    for(U32 i = 0; i < 128; i += 1)
      if(mac_wm_key(i) == key && mac_wm_state->hardware_keys[i]) any_down = 1;
    mac_wm_state->keys[key] = any_down;
    if(deliver)
    {
      WM_Event *e = result = mac_wm_event(w, down ? WM_EventKind_Press : WM_EventKind_Release);
      e->modifiers = mac_wm_modifiers(event.modifierFlags);
      if(key == WM_Key_Alt)   e->modifiers &= ~WM_Modifier_Alt;
      if(key == WM_Key_Ctrl)  e->modifiers &= ~WM_Modifier_Ctrl;
      if(key == WM_Key_Shift) e->modifiers &= ~WM_Modifier_Shift;
      e->key = key;
      e->is_repeat = event.type == NSEventTypeKeyDown && event.isARepeat;
      e->right_sided = (code == 54 || code == 60 || code == 61 || code == 62);
    }
  }
  if(code < 128) mac_wm_state->delivered_keys[code] = down && deliver;
  return result;
}

@interface MacWMWindow : NSWindow
@end
@implementation MacWMWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
  // App-menu equivalents retain their native actions; remaining Command
  // combinations are RAD's Control-equivalent shortcuts.
  if((event.modifierFlags & NSEventModifierFlagCommand) && [self.firstResponder isKindOfClass:[NSView class]])
  {
    if([[NSApp mainMenu] performKeyEquivalent:event]) return YES;
    [self.firstResponder keyDown:event];
    return YES;
  }
  return [super performKeyEquivalent:event];
}
@end

@interface MacWMView : NSView <NSTextInputClient, NSWindowDelegate>
{
@public
  Mac_WM_Window *owner;
  NSMutableAttributedString *marked;
  NSTrackingArea *tracking;
}
@end

@implementation MacWMView
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)dealloc
{
  [marked release];
  [tracking release];
  [super dealloc];
}
- (void)updateTrackingAreas
{
  if(tracking) { [self removeTrackingArea:tracking]; [tracking release]; }
  tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect options:NSTrackingMouseMoved|NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
  [self addTrackingArea:tracking];
  [super updateTrackingAreas];
}
- (void)keyDown:(NSEvent *)event
{
  U16 code = event.keyCode;
  B32 was_delivered = code < 128 && mac_wm_state->delivered_keys[code];
  WM_Event *press = mac_wm_keyboard(owner, event, 1);
  BOOL had_marked_text = self.hasMarkedText;
  BOOL handled = NO;
  if(had_marked_text || !(event.modifierFlags & (NSEventModifierFlagCommand|NSEventModifierFlagControl)))
    handled = [[self inputContext] handleEvent:event];
  // Decide after native interpretation: the committing key may have cleared
  // marked text, and the first composition key may have just created it.
  if(press && handled && (had_marked_text || self.hasMarkedText))
  {
    wm_eat_event(&mac_wm_state->events, press);
    if(code < 128) mac_wm_state->delivered_keys[code] = was_delivered;
  }
}
- (void)keyUp:(NSEvent *)event { mac_wm_keyboard(owner, event, 0); }
- (void)flagsChanged:(NSEvent *)event
{
  U16 code = event.keyCode;
  NSEventModifierFlags flag = 0;
  switch(code)
  {
    case 54: case 55: flag = NSEventModifierFlagCommand; break;
    case 56: case 60: flag = NSEventModifierFlagShift; break;
    case 58: case 61: flag = NSEventModifierFlagOption; break;
    case 59: case 62: flag = NSEventModifierFlagControl; break;
    case 57: flag = NSEventModifierFlagCapsLock; break;
  }
  if(flag)
  {
    // Device-specific low bits distinguish left/right modifier release while the
    // opposite side is still held (the aggregate flag alone cannot do that).
    U64 device_bit = 0;
    switch(code)
    {
      case 54: device_bit=0x10; break; case 55: device_bit=0x08; break;
      case 56: device_bit=0x02; break; case 60: device_bit=0x04; break;
      case 58: device_bit=0x20; break; case 61: device_bit=0x40; break;
      case 59: device_bit=0x01; break; case 62: device_bit=0x2000; break;
    }
    B32 down = device_bit ? !!(event.modifierFlags & device_bit) : !!(event.modifierFlags & flag);
    mac_wm_keyboard(owner, event, down);
  }
}
- (void)insertText:(id)text replacementRange:(NSRange)replacement
{
  NSString *string = [text isKindOfClass:[NSAttributedString class]] ? [text string] : text;
  [self unmarkText];
  for(NSUInteger i = 0; i < string.length; i += 1)
  {
    U32 cp = [string characterAtIndex:i];
    if(cp >= 0xd800 && cp <= 0xdbff && i+1 < string.length)
    {
      U32 low = [string characterAtIndex:i+1];
      if(low >= 0xdc00 && low <= 0xdfff) { cp = 0x10000 + ((cp-0xd800)<<10) + low-0xdc00; i += 1; }
    }
    if(cp >= 32 && cp != 127 && !(cp >= 0xf700 && cp <= 0xf8ff))
    {
      WM_Event *e = mac_wm_event(owner, WM_EventKind_Text);
      e->character = cp;
    }
  }
}
- (void)insertText:(id)text { [self insertText:text replacementRange:NSMakeRange(NSNotFound, 0)]; }
- (void)setMarkedText:(id)text selectedRange:(NSRange)selection replacementRange:(NSRange)replacement
{
  [marked release];
  marked = [text isKindOfClass:[NSAttributedString class]] ? [text mutableCopy] : [[NSMutableAttributedString alloc] initWithString:text];
}
- (void)unmarkText { [marked release]; marked = nil; }
- (BOOL)hasMarkedText { return marked.length != 0; }
- (NSRange)markedRange { return self.hasMarkedText ? NSMakeRange(0, marked.length) : NSMakeRange(NSNotFound, 0); }
- (NSRange)selectedRange { return NSMakeRange(0, 0); }
- (NSArray *)validAttributesForMarkedText { return @[]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
  if(actual) *actual = NSMakeRange(NSNotFound, 0);
  return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point { return NSNotFound; }
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
  if(actual) *actual = range;
  NSPoint p = [self convertPoint:[self.window mouseLocationOutsideOfEventStream] fromView:nil];
  NSRect rect = [self convertRect:NSMakeRect(p.x, p.y, 1, 20) toView:nil];
  return [self.window convertRectToScreen:rect];
}
- (void)doCommandBySelector:(SEL)selector {}
- (void)mouseDown:(NSEvent *)event
{
  Vec2F32 p = mac_wm_pos(owner, event.locationInWindow);
  B32 title = owner->custom_border && p.y < owner->title_thickness && p.y >= owner->edge_thickness;
  for(Mac_WM_ClientArea *area = owner->first_client_area; area; area = area->next)
    if(contains_2f32(area->rect, p)) title = 0;
  if(title && !(self.window.styleMask & NSWindowStyleMaskFullScreen))
  {
    if(event.clickCount == 2) [self.window zoom:nil];
    else [self.window performWindowDragWithEvent:event];
    mac_wm_event(owner, WM_EventKind_Wakeup);
  }
  else [self macMouse:event down:YES];
}
- (void)macMouse:(NSEvent *)event down:(BOOL)down
{
  WM_Key key = event.buttonNumber == 0 ? WM_Key_LeftMouseButton : event.buttonNumber == 1 ? WM_Key_RightMouseButton : WM_Key_MiddleMouseButton;
  mac_wm_state->keys[key] = down;
  WM_Event *e = mac_wm_event(owner, down ? WM_EventKind_Press : WM_EventKind_Release);
  e->key = key;
  e->pos = mac_wm_pos(owner, event.locationInWindow);
  e->modifiers = mac_wm_modifiers(event.modifierFlags);
}
- (void)mouseUp:(NSEvent *)event { [self macMouse:event down:NO]; }
- (void)rightMouseDown:(NSEvent *)event { [self macMouse:event down:YES]; }
- (void)rightMouseUp:(NSEvent *)event { [self macMouse:event down:NO]; }
- (void)otherMouseDown:(NSEvent *)event { [self macMouse:event down:YES]; }
- (void)otherMouseUp:(NSEvent *)event { [self macMouse:event down:NO]; }
- (void)mouseMoved:(NSEvent *)event
{
  WM_Event *e = mac_wm_event(owner, WM_EventKind_MouseMove);
  e->pos = mac_wm_pos(owner, event.locationInWindow);
  e->delta = v2f32(event.deltaX*self.window.backingScaleFactor, event.deltaY*self.window.backingScaleFactor);
  wm_set_cursor(mac_wm_state->cursor);
}
- (void)mouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)rightMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)otherMouseDragged:(NSEvent *)event { [self mouseMoved:event]; }
- (void)mouseEntered:(NSEvent *)event { [self mouseMoved:event]; }
- (void)mouseExited:(NSEvent *)event { [self mouseMoved:event]; [[NSCursor arrowCursor] set]; }
- (void)scrollWheel:(NSEvent *)event
{
  WM_Event *e = mac_wm_event(owner, WM_EventKind_Scroll);
  F32 factor = event.hasPreciseScrollingDeltas ? self.window.backingScaleFactor : 40.f;
  e->delta = v2f32(-event.scrollingDeltaX*factor, -event.scrollingDeltaY*factor);
  e->pos = mac_wm_pos(owner, event.locationInWindow);
  e->modifiers = mac_wm_modifiers(event.modifierFlags);
}
- (BOOL)windowShouldClose:(NSWindow *)sender { mac_wm_event(owner, WM_EventKind_WindowClose); return NO; }
- (void)windowDidResignKey:(NSNotification *)notification
{
  MemoryZeroArray(mac_wm_state->keys);
  MemoryZeroArray(mac_wm_state->hardware_keys);
  MemoryZeroArray(mac_wm_state->delivered_keys);
  [[self inputContext] discardMarkedText];
  [self unmarkText];
  mac_wm_event(owner, WM_EventKind_WindowLoseFocus);
}
- (void)windowDidBecomeKey:(NSNotification *)notification { mac_wm_event(owner, WM_EventKind_Wakeup); }
- (void)windowDidChangeBackingProperties:(NSNotification *)notification { mac_wm_event(owner, WM_EventKind_Wakeup); }
- (void)windowDidMove:(NSNotification *)notification { mac_wm_event(owner, WM_EventKind_Wakeup); }
- (void)windowDidResize:(NSNotification *)notification
{
  mac_wm_event(owner, WM_EventKind_Wakeup);
  // AppKit's modal live-resize tracking loop does not return to the outer frame loop.
  if(owner->shown && self.inLiveResize && !mac_wm_state->resizing)
  {
    mac_wm_state->resizing = 1;
    update();
    mac_wm_state->resizing = 0;
  }
}
- (void)drawRect:(NSRect)rect { mac_wm_event(owner, WM_EventKind_Wakeup); }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
  return ([sender draggingSourceOperationMask] & NSDragOperationCopy) ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender { return YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
  NSArray *urls = [[sender draggingPasteboard] readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
  if(urls.count == 0) return NO;
  WM_Event *e = mac_wm_event(owner, WM_EventKind_FileDrop);
  e->pos = mac_wm_pos(owner, [sender draggingLocation]);
  for(NSURL *url in urls)
  {
    const char *path = url.path.fileSystemRepresentation;
    str8_list_push(mac_wm_state->event_arena, &e->strings, push_str8_copy(mac_wm_state->event_arena, str8_cstring((char *)path)));
  }
  return YES;
}
@end

@interface MacWMDelegate : NSObject <NSApplicationDelegate>
@end
@implementation MacWMDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
  for(Mac_WM_Window *w = mac_wm_state->first_window; w; w = w->next)
    mac_wm_event(w, WM_EventKind_WindowClose);
  return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { return NO; }
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible
{
  if(!visible && mac_wm_state->first_window) [mac_wm_state->first_window->window makeKeyAndOrderFront:nil];
  return YES;
}
- (BOOL)application:(NSApplication *)sender openFile:(NSString *)path
{
  Mac_WM_Window *w = mac_wm_state->first_window;
  if(!w) return NO;
  NSWindow *target = sender.keyWindow ?: sender.mainWindow;
  for(Mac_WM_Window *candidate = mac_wm_state->first_window; candidate; candidate = candidate->next)
    if(candidate->window == target) { w = candidate; break; }
  WM_Event *e = mac_wm_event(w, WM_EventKind_FileDrop);
  NSRect content = w->view.bounds;
  CGFloat scale = w->window.backingScaleFactor;
  e->pos = v2f32(NSMidX(content)*scale, NSMidY(content)*scale);
  str8_list_push(mac_wm_state->event_arena, &e->strings, push_str8_copy(mac_wm_state->event_arena, str8_cstring((char *)path.fileSystemRepresentation)));
  // Apple Events do not themselves release an idle nextEventMatchingMask wait.
  wm_send_wakeup_event();
  return YES;
}
@end

internal void
wm_init(void)
{
  if(mac_wm_state) return;
  Assert([NSThread isMainThread]);
  // The application owns this root pool; event-loop pools bound transient objects.
  [[NSAutoreleasePool alloc] init];
  Arena *arena = arena_alloc();
  mac_wm_state = push_array(arena, Mac_WM_State, 1);
  mac_wm_state->event_arena = arena_alloc();
  mac_wm_state->info.double_click_time = [NSEvent doubleClickInterval];
  mac_wm_state->info.caret_blink_time = 0.5f;
  CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
  F64 hz = mode ? CGDisplayModeGetRefreshRate(mode) : 0;
  if(mode) CGDisplayModeRelease(mode);
  mac_wm_state->info.default_refresh_rate = hz > 0 ? hz : 60;
  [NSApplication sharedApplication];
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  mac_wm_state->delegate = [[MacWMDelegate alloc] init];
  [NSApp setDelegate:mac_wm_state->delegate];
  NSMenu *bar = [[[NSMenu alloc] init] autorelease];
  NSMenuItem *app_item = [[[NSMenuItem alloc] init] autorelease];
  [bar addItem:app_item];
  NSMenu *app_menu = [[[NSMenu alloc] initWithTitle:@"RAD Debugger"] autorelease];
  [app_menu addItemWithTitle:@"About RAD Debugger" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItemWithTitle:@"Hide RAD Debugger" action:@selector(hide:) keyEquivalent:@"h"];
  [app_menu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItemWithTitle:@"Quit RAD Debugger" action:@selector(terminate:) keyEquivalent:@"q"];
  [app_item setSubmenu:app_menu];
  [NSApp setMainMenu:bar];
  [NSApp finishLaunching];
  [NSApp activateIgnoringOtherApps:YES];
}

internal WM_SystemInfo *wm_get_system_info(void) { return &mac_wm_state->info; }
internal void
wm_set_clipboard_text(String8 string)
{
  @autoreleasepool
  {
    NSPasteboard *p = [NSPasteboard generalPasteboard];
    [p clearContents];
    [p setString:mac_wm_nsstring(string) forType:NSPasteboardTypeString];
  }
}
internal String8
wm_get_clipboard_text(Arena *arena)
{
  String8 result = str8_zero();
  @autoreleasepool
  {
    NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
    if(s) result = push_str8_copy(arena, str8_cstring((char *)s.UTF8String));
  }
  return result;
}

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  Mac_WM_Window *w = calloc(1, sizeof(*w));
  w->border_arena = arena_alloc();
  w->custom_border = !!(flags & WM_WindowFlag_CustomBorder);
  NSScreen *screen = NSScreen.screens.firstObject;
  F32 scale = screen.backingScaleFactor;
  // Persisted desktop coordinates use the primary screen's scale globally,
  // independent of the backing scale of the window's current screen.
  NSRect frame = NSMakeRect(rect.x0/scale, NSMaxY(screen.frame)-rect.y1/scale, (rect.x1-rect.x0)/scale, (rect.y1-rect.y0)/scale);
  NSUInteger style = NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable;
  if(w->custom_border) style |= NSWindowStyleMaskFullSizeContentView;
  NSRect content = [NSWindow contentRectForFrameRect:frame styleMask:style];
  w->window = [[MacWMWindow alloc] initWithContentRect:content styleMask:style backing:NSBackingStoreBuffered defer:NO];
  [w->window setReleasedWhenClosed:NO];
  [w->window setTitle:mac_wm_nsstring(title)];
  [w->window setAcceptsMouseMovedEvents:YES];
  [w->window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
  [w->window setMinSize:NSMakeSize(240, 160)];
  if(w->custom_border)
  {
    [w->window setTitleVisibility:NSWindowTitleHidden];
    [w->window setTitlebarAppearsTransparent:YES];
    for(NSUInteger button = NSWindowCloseButton; button <= NSWindowZoomButton; button += 1)
      [[w->window standardWindowButton:button] setHidden:YES];
  }
  MacWMView *view = [[MacWMView alloc] initWithFrame:NSMakeRect(0, 0, content.size.width, content.size.height)];
  view->owner = w;
  w->view = view;
  [view setAutoresizingMask:NSViewWidthSizable|NSViewHeightSizable];
  [view setWantsBestResolutionOpenGLSurface:YES];
  [view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
  [w->window setContentView:view];
  [w->window setDelegate:view];
  [w->window makeFirstResponder:view];
  if(flags & WM_WindowFlag_UseDefaultPosition) [w->window center];
  w->next = mac_wm_state->first_window;
  mac_wm_state->first_window = w;
  WM_Window result = {(U64)w};
  return result;
}
internal void
wm_window_close(WM_Window handle)
{
  Mac_WM_Window *w = (Mac_WM_Window *)handle.u64[0];
  if(!w) return;
  Mac_WM_Window **link = &mac_wm_state->first_window;
  while(*link && *link != w) link = &(*link)->next;
  if(*link) *link = w->next;
  [w->window setDelegate:nil];
  [w->window orderOut:nil];
  [w->window close];
  ((MacWMView *)w->view)->owner = 0;
  [w->view release];
  [w->window release];
  arena_release(w->border_arena);
  free(w);
}
internal void wm_window_set_title(WM_Window h, String8 title) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) [w->window setTitle:mac_wm_nsstring(title)]; }
internal void wm_window_first_paint(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; if(w && !w->shown) { w->shown=1; [w->window makeKeyAndOrderFront:nil]; } }
internal void wm_window_focus(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) { [NSApp activateIgnoringOtherApps:YES]; [w->window makeKeyAndOrderFront:nil]; } }
internal B32 wm_window_is_focused(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w && w->window.isKeyWindow; }
internal B32 wm_window_is_fullscreen(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w && !!(w->window.styleMask & NSWindowStyleMaskFullScreen); }
internal void wm_window_set_fullscreen(WM_Window h, B32 value) { Mac_WM_Window *w=(void *)h.u64[0]; if(w && wm_window_is_fullscreen(h) != !!value) [w->window toggleFullScreen:nil]; }
internal B32 wm_window_is_maximized(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w && w->window.isZoomed; }
internal void wm_window_set_maximized(WM_Window h, B32 value) { Mac_WM_Window *w=(void *)h.u64[0]; if(w && w->window.isZoomed != !!value) [w->window zoom:nil]; }
internal B32 wm_window_is_minimized(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w && w->window.isMiniaturized; }
internal void wm_window_set_minimized(WM_Window h, B32 value) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) { if(value) [w->window miniaturize:nil]; else [w->window deminiaturize:nil]; } }
internal void wm_window_bring_to_front(WM_Window h) { wm_window_focus(h); }
internal void
wm_window_set_monitor(WM_Window h, WM_Monitor monitor)
{
  Mac_WM_Window *w=(void *)h.u64[0];
  for(NSScreen *screen in NSScreen.screens)
    if([[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue] == monitor.u64[0] && w)
    {
      NSRect frame = w->window.frame;
      NSRect visible = screen.visibleFrame;
      frame.origin = NSMakePoint(NSMidX(visible)-frame.size.width/2, NSMidY(visible)-frame.size.height/2);
      [w->window setFrame:frame display:YES];
      break;
    }
}
internal void wm_window_clear_custom_border_data(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) { arena_clear(w->border_arena); w->first_client_area=0; w->title_thickness=0; w->edge_thickness=0; } }
internal void wm_window_push_custom_title_bar(WM_Window h, F32 t) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) w->title_thickness=t; }
internal void wm_window_push_custom_edges(WM_Window h, F32 t) { Mac_WM_Window *w=(void *)h.u64[0]; if(w) w->edge_thickness=t; }
internal void
wm_window_push_custom_title_bar_client_area(WM_Window h, Rng2F32 rect)
{
  Mac_WM_Window *w=(void *)h.u64[0];
  if(w) { Mac_WM_ClientArea *a=push_array(w->border_arena, Mac_WM_ClientArea, 1); a->rect=rect; a->next=w->first_client_area; w->first_client_area=a; }
}
internal Rng2F32
wm_rect_from_window(WM_Window h)
{
  Mac_WM_Window *w=(void *)h.u64[0];
  Rng2F32 result={0};
  if(w) { NSScreen *screen=NSScreen.screens.firstObject; NSRect r=w->window.frame; F32 s=screen.backingScaleFactor; F32 top=NSMaxY(screen.frame); result=r2f32p(r.origin.x*s,(top-NSMaxY(r))*s,NSMaxX(r)*s,(top-r.origin.y)*s); }
  return result;
}
internal Rng2F32
wm_client_rect_from_window(WM_Window h)
{
  Mac_WM_Window *w=(void *)h.u64[0];
  Rng2F32 result={0};
  if(w) { NSRect r=[w->view convertRectToBacking:w->view.bounds]; result=r2f32p(0,0,r.size.width,r.size.height); }
  return result;
}
internal F32 wm_dpi_from_window(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w ? 96.f*w->window.backingScaleFactor : 96.f; }
internal WM_ExtWindow
wm_focused_external_window(void)
{
  NSRunningApplication *app=NSWorkspace.sharedWorkspace.frontmostApplication;
  WM_ExtWindow result={(U64)app.processIdentifier};
  return result;
}
internal void wm_focus_external_window(WM_ExtWindow h) { NSRunningApplication *app=[NSRunningApplication runningApplicationWithProcessIdentifier:(pid_t)h.u64[0]]; [app activateWithOptions:NSApplicationActivateIgnoringOtherApps]; }
internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  WM_MonitorArray result={0}; result.count=NSScreen.screens.count; result.v=push_array(arena, WM_Monitor, result.count);
  U64 idx=0; for(NSScreen *screen in NSScreen.screens) result.v[idx++].u64[0]=[[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue];
  return result;
}
internal WM_Monitor wm_primary_monitor(void) { WM_Monitor result={(U64)CGMainDisplayID()}; return result; }
internal WM_Monitor wm_monitor_from_window(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; WM_Monitor result={w ? [[w->window.screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue] : CGMainDisplayID()}; return result; }
internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  for(NSScreen *screen in NSScreen.screens) if([[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue]==monitor.u64[0]) return push_str8_copy(arena, str8_cstring((char *)screen.localizedName.UTF8String));
  return str8_zero();
}
internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  Vec2F32 result={0};
  for(NSScreen *screen in NSScreen.screens) if([[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue]==monitor.u64[0]) result=v2f32(screen.frame.size.width*screen.backingScaleFactor, screen.frame.size.height*screen.backingScaleFactor);
  return result;
}
internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  F32 result=96;
  for(NSScreen *screen in NSScreen.screens) if([[screen.deviceDescription objectForKey:@"NSScreenNumber"] unsignedIntValue]==monitor.u64[0]) result=96*screen.backingScaleFactor;
  return result;
}
internal void
wm_send_wakeup_event(void)
{
  // postEvent is thread safe; unlike dispatching a main-queue block, it also
  // interrupts nextEventMatchingMask while the application is idle.
  @autoreleasepool
  {
    NSEvent *event=[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0x5244 data1:0 data2:0];
    [NSApp postEvent:event atStart:NO];
  }
}
internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  @autoreleasepool
  {
    for(;;)
    {
      NSDate *until = wait && mac_wm_state->events.count == 0 ? [NSDate distantFuture] : [NSDate distantPast];
      NSEvent *event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until inMode:NSDefaultRunLoopMode dequeue:YES];
      if(!event) break;
      if(event.type == NSEventTypeApplicationDefined && event.subtype == 0x5244) mac_wm_event(0, WM_EventKind_Wakeup);
      // AppKit can swallow Command-modified key-up events instead of forwarding
      // them to the responder. Keep shortcut key state balanced explicitly.
      else if(event.type == NSEventTypeKeyUp && [event.window.contentView isKindOfClass:[MacWMView class]])
        [(MacWMView *)event.window.contentView keyUp:event];
      else [NSApp sendEvent:event];
      // Any native event may have moved/resized a window without a WM event.
      wait=0;
    }
    [NSApp updateWindows];
    WM_EventList result=wm_event_list_copy(arena, &mac_wm_state->events);
    MemoryZeroStruct(&mac_wm_state->events);
    arena_clear(mac_wm_state->event_arena);
    return result;
  }
}
internal WM_Modifiers wm_get_modifiers(void) { return mac_wm_modifiers(NSEvent.modifierFlags); }
internal B32 wm_key_is_down(WM_Key key) { return key > WM_Key_Null && key < WM_Key_COUNT && mac_wm_state->keys[key]; }
internal Vec2F32 wm_mouse_from_window(WM_Window h) { Mac_WM_Window *w=(void *)h.u64[0]; return w ? mac_wm_pos(w, w->window.mouseLocationOutsideOfEventStream) : v2f32(0,0); }
internal NSCursor *
mac_wm_directional_cursor(WM_Cursor cursor)
{
  local_persist NSCursor *cursors[3];
  U32 index = cursor == WM_Cursor_DownRight ? 0 : cursor == WM_Cursor_UpRight ? 1 : 2;
  if(!cursors[index])
  {
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(24, 24)];
    [image lockFocus];
    NSBezierPath *path = [NSBezierPath bezierPath];
    U32 axis_count = index == 2 ? 2 : 1;
    for(U32 axis = 0; axis < axis_count; axis += 1)
    {
      F32 dx = index == 2 ? (axis == 0 ? 1 : 0) : 0.7071f;
      F32 dy = index == 2 ? (axis == 0 ? 0 : 1) : (index == 0 ? -0.7071f : 0.7071f);
      NSPoint a = NSMakePoint(12-9*dx, 12-9*dy);
      NSPoint b = NSMakePoint(12+9*dx, 12+9*dy);
      [path moveToPoint:a]; [path lineToPoint:b];
      [path moveToPoint:NSMakePoint(a.x+4*dx-4*dy, a.y+4*dy+4*dx)];
      [path lineToPoint:a];
      [path lineToPoint:NSMakePoint(a.x+4*dx+4*dy, a.y+4*dy-4*dx)];
      [path moveToPoint:NSMakePoint(b.x-4*dx-4*dy, b.y-4*dy+4*dx)];
      [path lineToPoint:b];
      [path lineToPoint:NSMakePoint(b.x-4*dx+4*dy, b.y-4*dy-4*dx)];
    }
    [path setLineJoinStyle:NSRoundLineJoinStyle];
    [path setLineCapStyle:NSRoundLineCapStyle];
    [NSColor.whiteColor setStroke]; [path setLineWidth:4]; [path stroke];
    [NSColor.blackColor setStroke]; [path setLineWidth:2]; [path stroke];
    [image unlockFocus];
    cursors[index] = [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(12, 12)];
    [image release];
  }
  return cursors[index];
}

internal void
wm_set_cursor(WM_Cursor cursor)
{
  mac_wm_state->cursor=cursor;
  NSCursor *native=NSCursor.arrowCursor;
  switch(cursor)
  {
    case WM_Cursor_IBar: native=NSCursor.IBeamCursor; break;
    case WM_Cursor_LeftRight: native=NSCursor.resizeLeftRightCursor; break;
    case WM_Cursor_UpDown: native=NSCursor.resizeUpDownCursor; break;
    case WM_Cursor_DownRight: case WM_Cursor_UpRight: case WM_Cursor_UpDownLeftRight: native=mac_wm_directional_cursor(cursor); break;
    case WM_Cursor_HandPoint: native=NSCursor.pointingHandCursor; break;
    case WM_Cursor_Disabled: native=NSCursor.operationNotAllowedCursor; break;
    default: break;
  }
  [native set];
}
