// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Cocoa Helpers

internal void
sh_mac_call_main(void *context)
{
  Mac_SH_Call *call = (Mac_SH_Call *)context;
  switch(call->operation)
  {
    case Mac_SH_Operation_Message: { sh_message(call->error, call->title, call->string); } break;
    case Mac_SH_Operation_PickFile: { call->result = sh_pick_file(call->arena, call->title, call->string); } break;
    case Mac_SH_Operation_Reveal: { sh_show_in_file_browser(call->string); } break;
    case Mac_SH_Operation_Browse: { sh_open_in_browser(call->string); } break;
  }
}

internal NSString *
sh_mac_string(String8 string)
{
  NSString *result = [[[NSString alloc] initWithBytes:string.str length:string.size encoding:NSUTF8StringEncoding] autorelease];
  return result != nil ? result : @"";
}

internal B32
sh_mac_managed_install(NSString *receipt_path, NSString *bundle_path)
{
  NSDictionary *receipt = [NSDictionary dictionaryWithContentsOfFile:receipt_path];
  return [[receipt objectForKey:@"BundlePath"] isEqualToString:bundle_path] &&
         [[receipt objectForKey:@"BundleIdentifier"] isEqualToString:@"com.radgametools.raddbg"];
}

internal NSString *
sh_mac_bundle_executable(NSString *bundle_path)
{
  NSDictionary *info = [NSDictionary dictionaryWithContentsOfFile:[bundle_path stringByAppendingPathComponent:@"Contents/Info.plist"]];
  NSString *name = [info objectForKey:@"CFBundleExecutable"];
  if(![name isKindOfClass:[NSString class]] || ![name isEqualToString:[name lastPathComponent]] || [name isEqualToString:@".."])
  {
    return nil;
  }
  return [[bundle_path stringByAppendingPathComponent:@"Contents/MacOS"] stringByAppendingPathComponent:name];
}

////////////////////////////////
//~ Shell Operations

internal void
sh_message(B32 error, String8 title, String8 message)
{
  if(!pthread_main_np())
  {
    Mac_SH_Call call = {Mac_SH_Operation_Message, 0, error, title, message};
    dispatch_sync_f(dispatch_get_main_queue(), &call, sh_mac_call_main);
    return;
  }
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setAlertStyle:error ? NSAlertStyleCritical : NSAlertStyleInformational];
    [alert setMessageText:sh_mac_string(title)];
    [alert setInformativeText:sh_mac_string(message)];
    [alert addButtonWithTitle:@"OK"];
    [NSApp activateIgnoringOtherApps:YES];
    [alert runModal];
    [alert release];
  }
}

internal String8
sh_pick_file(Arena *arena, String8 title, String8 initial_path)
{
  if(!pthread_main_np())
  {
    Mac_SH_Call call = {Mac_SH_Operation_PickFile, arena, 0, title, initial_path};
    dispatch_sync_f(dispatch_get_main_queue(), &call, sh_mac_call_main);
    return call.result;
  }
  String8 result = {0};
  @autoreleasepool
  {
    [NSApplication sharedApplication];
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    [panel setTitle:sh_mac_string(title)];
    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:NO];
    [panel setResolvesAliases:YES];
    if(initial_path.size != 0)
    {
      NSString *path = [[sh_mac_string(initial_path) stringByExpandingTildeInPath] stringByStandardizingPath];
      BOOL directory = NO;
      [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&directory];
      if(directory)
      {
        [panel setDirectoryURL:[NSURL fileURLWithPath:path isDirectory:YES]];
      }
      else
      {
        [panel setDirectoryURL:[NSURL fileURLWithPath:[path stringByDeletingLastPathComponent] isDirectory:YES]];
        [panel setNameFieldStringValue:[path lastPathComponent]];
      }
    }
    [NSApp activateIgnoringOtherApps:YES];
    if([panel runModal] == NSModalResponseOK)
    {
      result = str8_copy(arena, str8_cstring((char *)[[[panel URL] path] UTF8String]));
    }
  }
  return result;
}

internal void
sh_show_in_file_browser(String8 path)
{
  if(!pthread_main_np())
  {
    Mac_SH_Call call = {0};
    call.operation = Mac_SH_Operation_Reveal;
    call.string = path;
    dispatch_sync_f(dispatch_get_main_queue(), &call, sh_mac_call_main);
    return;
  }
  @autoreleasepool
  {
    NSString *native_path = [[sh_mac_string(path) stringByExpandingTildeInPath] stringByStandardizingPath];
    NSURL *url = [NSURL fileURLWithPath:native_path];
    [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:[NSArray arrayWithObject:url]];
  }
}

internal void
sh_open_in_browser(String8 url)
{
  if(!pthread_main_np())
  {
    Mac_SH_Call call = {0};
    call.operation = Mac_SH_Operation_Browse;
    call.string = url;
    dispatch_sync_f(dispatch_get_main_queue(), &call, sh_mac_call_main);
    return;
  }
  @autoreleasepool
  {
    NSURL *native_url = [NSURL URLWithString:sh_mac_string(url)];
    if(native_url == nil || ![[NSWorkspace sharedWorkspace] openURL:native_url])
    {
      sh_message(1, s("Open URL"), s("The URL could not be opened by the default browser."));
    }
  }
}

internal B32
sh_install_or_uninstall_self(B32 write, B32 install)
{
  B32 result = 0;
  @autoreleasepool
  {
    NSFileManager *files = [NSFileManager defaultManager];
    NSString *applications = [NSHomeDirectory() stringByAppendingPathComponent:@"Applications"];
    NSString *bundle_path = [applications stringByAppendingPathComponent:@"RAD Debugger.app"];
    NSString *receipt_directory = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/RAD Debugger"];
    NSString *receipt_path = [receipt_directory stringByAppendingPathComponent:@"install.plist"];
    NSString *bin_directory = [NSHomeDirectory() stringByAppendingPathComponent:@".local/bin"];
    NSString *link_path = [bin_directory stringByAppendingPathComponent:@"raddbg"];
    B32 managed = sh_mac_managed_install(receipt_path, bundle_path);
    NSString *executable = sh_mac_bundle_executable(bundle_path);
    NSString *link_target = [files destinationOfSymbolicLinkAtPath:link_path error:NULL];
    result = managed && executable != nil && [files isExecutableFileAtPath:executable] && [link_target isEqualToString:executable];
    if(write)
    {
      NSError *error = nil;
      B32 ok = 1;
      if(install)
      {
        B32 created_bundle = 0;
        B32 created_link = 0;
        NSString *staging = [applications stringByAppendingPathComponent:[NSString stringWithFormat:@".RAD-Debugger-%@.app", [[NSUUID UUID] UUIDString]]];
        if([files fileExistsAtPath:bundle_path] && !managed)
        {
          ok = 0;
          error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteFileExistsError userInfo:[NSDictionary dictionaryWithObject:@"An unmanaged RAD Debugger.app already exists in your Applications folder. It has not been replaced." forKey:NSLocalizedDescriptionKey]];
        }
        if(ok && ![files fileExistsAtPath:bundle_path])
        {
          ok = [files createDirectoryAtPath:applications withIntermediateDirectories:YES attributes:nil error:&error];
          NSString *source = sh_mac_string(get_process_info()->binary_file_path);
          NSString *source_bundle = [[NSBundle mainBundle] bundlePath];
          if(ok && [[source_bundle pathExtension] isEqualToString:@"app"] && [source hasPrefix:[source_bundle stringByAppendingString:@"/"]])
          {
            // Preserve an existing bundle and its signature without changing its Info.plist.
            ok = [files copyItemAtPath:source_bundle toPath:staging error:&error];
          }
          else if(ok)
          {
            NSString *macos = [staging stringByAppendingPathComponent:@"Contents/MacOS"];
            ok = [files createDirectoryAtPath:macos withIntermediateDirectories:YES attributes:nil error:&error];
            if(ok) { ok = [files copyItemAtPath:source toPath:[macos stringByAppendingPathComponent:@"raddbg"] error:&error]; }
            if(ok) { ok = [files setAttributes:[NSDictionary dictionaryWithObject:[NSNumber numberWithUnsignedLong:0755] forKey:NSFilePosixPermissions] ofItemAtPath:[macos stringByAppendingPathComponent:@"raddbg"] error:&error]; }
            if(ok)
            {
              NSDictionary *info = @{
                @"CFBundleName": @"RAD Debugger",
                @"CFBundleDisplayName": @"RAD Debugger",
                @"CFBundleIdentifier": @"com.radgametools.raddbg",
                @"CFBundlePackageType": @"APPL",
                @"CFBundleExecutable": @"raddbg",
                @"CFBundleVersion": @BUILD_VERSION_STRING_LITERAL,
                @"CFBundleShortVersionString": @BUILD_VERSION_STRING_LITERAL,
                @"NSHighResolutionCapable": @YES,
              };
              NSData *data = [NSPropertyListSerialization dataWithPropertyList:info format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
              ok = data != nil && [data writeToFile:[staging stringByAppendingPathComponent:@"Contents/Info.plist"] options:NSDataWritingAtomic error:&error];
            }
          }
          if(ok)
          {
            ok = [files moveItemAtPath:staging toPath:bundle_path error:&error];
            created_bundle = ok;
          }
          if(!ok) { [files removeItemAtPath:staging error:NULL]; }
        }
        executable = sh_mac_bundle_executable(bundle_path);
        if(ok && (executable == nil || ![files isExecutableFileAtPath:executable]))
        {
          ok = 0;
          error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileReadCorruptFileError userInfo:[NSDictionary dictionaryWithObject:@"The installed application does not contain an executable." forKey:NSLocalizedDescriptionKey]];
        }
        if(ok && ![link_target isEqualToString:executable])
        {
          // Never overwrite an unrelated file or command, including a dangling symlink.
          if([files attributesOfItemAtPath:link_path error:NULL] != nil)
          {
            ok = 0;
            error = [NSError errorWithDomain:NSCocoaErrorDomain code:NSFileWriteFileExistsError userInfo:[NSDictionary dictionaryWithObject:@"~/.local/bin/raddbg already exists and does not point to the installed application. It has not been replaced." forKey:NSLocalizedDescriptionKey]];
          }
          if(ok) { ok = [files createDirectoryAtPath:bin_directory withIntermediateDirectories:YES attributes:nil error:&error]; }
          if(ok)
          {
            ok = [files createSymbolicLinkAtPath:link_path withDestinationPath:executable error:&error];
            created_link = ok;
          }
        }
        if(ok) { ok = [files createDirectoryAtPath:receipt_directory withIntermediateDirectories:YES attributes:nil error:&error]; }
        if(ok)
        {
          NSDictionary *receipt = @{@"BundlePath": bundle_path, @"BundleIdentifier": @"com.radgametools.raddbg"};
          NSData *data = [NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
          ok = data != nil && [data writeToFile:receipt_path options:NSDataWritingAtomic error:&error];
        }
        if(ok)
        {
          OSStatus status = LSRegisterURL((CFURLRef)[NSURL fileURLWithPath:bundle_path isDirectory:YES], true);
          if(status != noErr)
          {
            ok = 0;
            error = [NSError errorWithDomain:NSOSStatusErrorDomain code:status userInfo:nil];
          }
        }
        if(!ok)
        {
          if(created_link) { [files removeItemAtPath:link_path error:NULL]; }
          if(created_bundle)
          {
            [files removeItemAtPath:bundle_path error:NULL];
            [files removeItemAtPath:receipt_path error:NULL];
          }
        }
      }
      else if(managed)
      {
        // The receipt is outside the bundle so copying a signed app never invalidates it.
        if([link_target isEqualToString:executable]) { ok = [files removeItemAtPath:link_path error:&error]; }
        if(ok && [files fileExistsAtPath:bundle_path]) { ok = [files removeItemAtPath:bundle_path error:&error]; }
        if(ok) { ok = [files removeItemAtPath:receipt_path error:&error]; }
      }
      if(!ok)
      {
        NSString *description = [error localizedDescription];
        sh_message(1, s("Application Installation"), str8_cstring((char *)[description UTF8String]));
      }
      executable = sh_mac_bundle_executable(bundle_path);
      result = sh_mac_managed_install(receipt_path, bundle_path) && executable != nil && [files isExecutableFileAtPath:executable] && [[files destinationOfSymbolicLinkAtPath:link_path error:NULL] isEqualToString:executable];
    }
  }
  return result;
}
