// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal VoidProc *
r_ogl_os_load_procedure(char *name)
{
  // macOS exposes OpenGL 4.1, but not the 4.3/KHR_debug callback interface.
  if(strcmp(name, "glDebugMessageCallback") == 0)
  {
    return 0;
  }
  VoidProc *result = (VoidProc *)dlsym(RTLD_DEFAULT, name);
  if(result == 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 message = push_str8f(scratch.arena, "Could not load required OpenGL procedure: %s", name);
    sh_message(1, str8_lit("Fatal Error"), message);
    abort_self(1);
    scratch_end(scratch);
  }
  return result;
}

internal void
r_ogl_os_init(CmdLine *cmdln)
{
  // GLSL 330 and instanced attributes require the 4.1 profile on macOS;
  // Apple's other core profile is only OpenGL 3.2 / GLSL 150.
  NSOpenGLPixelFormatAttribute attributes[] =
  {
    NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion4_1Core,
    NSOpenGLPFAAccelerated,
    NSOpenGLPFADoubleBuffer,
    NSOpenGLPFAColorSize, 24,
    NSOpenGLPFAAlphaSize, 8,
    NSOpenGLPFADepthSize, 24,
    NSOpenGLPFAStencilSize, 8,
    0,
  };
  NSOpenGLPixelFormat *format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
  if(format != nil)
  {
    r_ogl_mac_ctx = [[NSOpenGLContext alloc] initWithFormat:format shareContext:nil];
    [format release];
  }
  if(r_ogl_mac_ctx == nil)
  {
    sh_message(1, str8_lit("Fatal Error"), str8_lit("Could not create an accelerated OpenGL 4.1 core context."));
    abort_self(1);
  }
  // A drawable is not required while creating the shared renderer resources.
  [r_ogl_mac_ctx makeCurrentContext];
  GLint swap_interval = 1;
  [r_ogl_mac_ctx setValues:&swap_interval forParameter:NSOpenGLContextParameterSwapInterval];
}

internal R_Handle
r_ogl_os_window_equip(WM_Window window)
{
  Mac_WM_Window *w = (Mac_WM_Window *)window.u64[0];
  [w->view setWantsBestResolutionOpenGLSurface:YES];
  R_Handle result = {0};
  return result;
}

internal void
r_ogl_os_window_unequip(WM_Window os, R_Handle r)
{
  Mac_WM_Window *w = (Mac_WM_Window *)os.u64[0];
  if([r_ogl_mac_ctx view] == w->view)
  {
    [r_ogl_mac_ctx clearDrawable];
  }
}

internal void
r_ogl_os_select_window(WM_Window os, R_Handle r)
{
  Mac_WM_Window *w = (Mac_WM_Window *)os.u64[0];
  // One context preserves textures, buffers, VAOs and FBOs across all windows.
  // All context and view operations run on the Cocoa main thread.
  if([r_ogl_mac_ctx view] != w->view)
  {
    [r_ogl_mac_ctx setView:w->view];
  }
  [r_ogl_mac_ctx update];
  [r_ogl_mac_ctx makeCurrentContext];
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDrawBuffer(GL_BACK);
  glReadBuffer(GL_BACK);
}

internal void
r_ogl_os_window_swap(WM_Window os, R_Handle r)
{
  // The drawable selected by r_window_begin_frame belongs to this window.
  [r_ogl_mac_ctx flushBuffer];
}
