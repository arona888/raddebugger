// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal B32
macho_range_valid(String8 data, U64 off, U64 size)
{
  return off <= data.size && size <= data.size - off;
}

internal U32
macho_u32(String8 data, U64 off, B32 big_endian)
{
  U32 result = 0;
  if(macho_range_valid(data, off, 4))
  {
    for(U64 i = 0; i < 4; i += 1) { result |= (U32)data.str[off+i] << (8*(big_endian ? 3-i : i)); }
  }
  return result;
}

internal U64
macho_u64(String8 data, U64 off, B32 big_endian)
{
  U64 lo = macho_u32(data, off + (big_endian ? 4 : 0), big_endian);
  U64 hi = macho_u32(data, off + (big_endian ? 0 : 4), big_endian);
  return lo | (hi << 32);
}

internal String8
macho_fixed_name(String8 data, U64 off)
{
  U64 size = 0;
  if(macho_range_valid(data, off, 16))
  {
    for(; size < 16 && data.str[off+size] != 0; size += 1) {}
  }
  return str8_substr(data, r1u64(off, off+size));
}

internal B32
macho_has_magic(String8 data)
{
  U32 magic = macho_u32(data, 0, 0);
  return data.size >= 4 && (magic == 0xfeedfacf || magic == 0xbebafeca || magic == 0xbfbafeca || magic == 0xcafebabe || magic == 0xcafebabf);
}

internal MACHO_Bin
macho_bin_from_data_selected(Arena *arena, String8 input, MACHO_Bin *module)
{
  MACHO_Bin result = {0};
  if(module != 0 && (!module->valid || !module->has_uuid)) { return result; }
  String8 data = input;
  U64 slice_offset = 0;
  U32 magic = macho_u32(data, 0, 0);
  if(magic == 0xbebafeca || magic == 0xbfbafeca || magic == 0xcafebabe || magic == 0xcafebabf)
  {
    B32 big_endian = (magic == 0xbebafeca || magic == 0xbfbafeca);
    B32 fat64 = (magic == 0xbfbafeca || magic == 0xcafebabf);
    U64 stride = fat64 ? 32 : 20;
    U64 count = macho_u32(data, 4, big_endian);
    if(!macho_range_valid(data, 0, 8) || !macho_range_valid(data, 8, count*stride)) { return result; }
    B32 found = 0;
    for(U64 i = 0; i < count; i += 1)
    {
      U64 off = 8+i*stride;
      U32 cpu = macho_u32(data, off, big_endian);
      U64 file_off = fat64 ? macho_u64(data, off+8, big_endian) : macho_u32(data, off+8, big_endian);
      U64 file_size = fat64 ? macho_u64(data, off+16, big_endian) : macho_u32(data, off+12, big_endian);
      U32 align = macho_u32(data, off+(fat64 ? 24 : 16), big_endian);
      if(!macho_range_valid(data, file_off, file_size) || file_off < 8+count*stride || align > 63 || (file_off & (((U64)1 << align)-1)) != 0) { return result; }
      if(cpu == 0x0100000c && !found)
      {
        slice_offset = file_off;
        result.data = str8_substr(input, r1u64(file_off, file_off+file_size));
        found = 1;
      }
    }
    if(module != 0)
    {
      for(U64 i = 0; i < count; i += 1)
      {
        U64 off = 8+i*stride;
        if(macho_u32(data, off, big_endian) != 0x0100000c) { continue; }
        U64 file_off = fat64 ? macho_u64(data, off+8, big_endian) : macho_u32(data, off+8, big_endian);
        U64 file_size = fat64 ? macho_u64(data, off+16, big_endian) : macho_u32(data, off+12, big_endian);
        String8 slice = str8_substr(input, r1u64(file_off, file_off+file_size));
        if(macho_u32(slice, 0, 0) != 0xfeedfacf) { continue; }
        Temp candidate_temp = temp_begin(arena);
        MACHO_Bin candidate = macho_bin_from_data_selected(arena, slice, module);
        if(candidate.valid)
        {
          candidate.slice_offset = file_off;
          return candidate;
        }
        temp_end(candidate_temp);
      }
      MemoryZeroStruct(&result);
      return result;
    }
    if(!found) { MemoryZeroStruct(&result); return result; }
    data = result.data;
  }
  if(!macho_range_valid(data, 0, 32) || macho_u32(data, 0, 0) != 0xfeedfacf || macho_u32(data, 4, 0) != 0x0100000c) { MemoryZeroStruct(&result); return result; }
  U32 file_type = macho_u32(data, 12, 0);
  if(file_type != 2 && file_type != 6 && file_type != 8 && file_type != 0xa) { MemoryZeroStruct(&result); return result; }
  U32 command_count = macho_u32(data, 16, 0);
  U32 command_size = macho_u32(data, 20, 0);
  if(!macho_range_valid(data, 32, command_size) || command_count > command_size/8) { MemoryZeroStruct(&result); return result; }
  U64 command_end = 32+(U64)command_size;
  U64 section_count = 0;
  // Validate the complete load-command region before allocating or exposing records.
  for(U64 pass = 0; pass < 2; pass += 1)
  {
    U64 cursor = 32;
    U64 section_index = 0;
    U64 image_end = 0;
    U64 entry_fileoff = 0;
    B32 has_entry = 0;
    B32 has_text = 0;
    for(U64 i = 0; i < command_count; i += 1)
    {
      if(cursor > command_end || command_end-cursor < 8) { goto malformed; }
      U32 cmd = macho_u32(data, cursor, 0);
      U32 size = macho_u32(data, cursor+4, 0);
      if(size < 8 || (size & 7) || size > command_end-cursor) { goto malformed; }
      if(cmd == 0x19) // LC_SEGMENT_64
      {
        if(size < 72) { goto malformed; }
        U64 vmaddr = macho_u64(data, cursor+24, 0);
        U64 vmsize = macho_u64(data, cursor+32, 0);
        U64 fileoff = macho_u64(data, cursor+40, 0);
        U64 filesize = macho_u64(data, cursor+48, 0);
        U32 nsects = macho_u32(data, cursor+64, 0);
        if(nsects > (size-72)/80 || !macho_range_valid(data, fileoff, filesize) || filesize > vmsize || vmsize > max_U64-vmaddr) { goto malformed; }
        String8 segment_name = macho_fixed_name(data, cursor+8);
        if(str8_match(segment_name, str8_lit("__TEXT"), 0))
        {
          result.preferred_base = vmaddr;
          has_text = 1;
        }
        if(!str8_match(segment_name, str8_lit("__PAGEZERO"), 0) && !str8_match(segment_name, str8_lit("__DWARF"), 0)) { image_end = Max(image_end, vmaddr+vmsize); }
        for(U64 j = 0; j < nsects; j += 1)
        {
          U64 off = cursor+72+j*80;
          U64 addr = macho_u64(data, off+32, 0);
          U64 len = macho_u64(data, off+40, 0);
          U64 foff = macho_u32(data, off+48, 0);
          U32 flags = macho_u32(data, off+64, 0);
          U32 kind = flags & 0xff;
          B32 zero_fill = kind == 1 || kind == 0xc || kind == 0x12;
          U64 data_size = zero_fill || filesize == 0 ? 0 : len;
          if(addr < vmaddr || addr-vmaddr > vmsize || len > vmsize-(addr-vmaddr) || !macho_range_valid(data, foff, data_size)) { goto malformed; }
          if(data_size && (foff < fileoff || foff-fileoff > filesize || data_size > filesize-(foff-fileoff))) { goto malformed; }
          if(pass == 1)
          {
            MACHO_Section *s = &result.sections[section_index++];
            s->name = macho_fixed_name(data, off);
            s->segment_name = macho_fixed_name(data, off+16);
            s->addr = addr;
            s->size = len;
            s->offset = foff;
            s->file_size = data_size;
            s->flags = flags;
            s->protections = macho_u32(data, cursor+60, 0);
          }
        }
        if(pass == 0) { section_count += nsects; }
      }
      else if(cmd == 0x1b) // LC_UUID
      {
        if(size < 24) { goto malformed; }
        MemoryCopy(result.uuid, data.str+cursor+8, 16);
        result.has_uuid = 1;
      }
      else if(cmd == 0x80000028) // LC_MAIN: entryoff is relative to __TEXT's file offset.
      {
        if(size < 24) { goto malformed; }
        entry_fileoff = macho_u64(data, cursor+8, 0);
        has_entry = 1;
      }
      else if(cmd == 2) // LC_SYMTAB
      {
        if(size < 24) { goto malformed; }
        result.symbol_offset = macho_u32(data, cursor+8, 0);
        result.symbol_count = macho_u32(data, cursor+12, 0);
        result.string_offset = macho_u32(data, cursor+16, 0);
        result.string_size = macho_u32(data, cursor+20, 0);
        if(!macho_range_valid(data, result.symbol_offset, (U64)result.symbol_count*16) || !macho_range_valid(data, result.string_offset, result.string_size)) { goto malformed; }
      }
      cursor += size;
    }
    if(cursor != command_end) { goto malformed; }
    if(pass == 0)
    {
      result.sections = push_array(arena, MACHO_Section, section_count);
      result.section_count = section_count;
    }
    if(has_text)
    {
      result.image_size = image_end >= result.preferred_base ? image_end-result.preferred_base : 0;
      if(has_entry)
      {
        // Translate a file offset using the segment mapping, not section layout.
        U64 off = 32;
        for(U64 i = 0; i < command_count; i += 1)
        {
          U32 size = macho_u32(data, off+4, 0);
          if(macho_u32(data, off, 0) == 0x19)
          {
            U64 foff = macho_u64(data, off+40, 0);
            U64 fsize = macho_u64(data, off+48, 0);
            U64 addr = macho_u64(data, off+24, 0);
            if(entry_fileoff >= foff && entry_fileoff-foff < fsize && addr >= result.preferred_base)
            { result.entry_voff = addr-result.preferred_base+entry_fileoff-foff; break; }
          }
          off += size;
        }
      }
    }
  }
  result.valid = 1;
  result.arch = Arch_arm64;
  result.file_type = file_type;
  result.data = data;
  result.slice_offset = slice_offset;
  if(module != 0 && (file_type != 0xa || !macho_uuid_match(module, &result))) { goto malformed; }
  return result;
malformed:
  MemoryZeroStruct(&result);
  return result;
}

internal MACHO_Bin
macho_bin_from_data(Arena *arena, String8 data)
{
  return macho_bin_from_data_selected(arena, data, 0);
}

internal MACHO_Bin
macho_dsym_bin_from_data(Arena *arena, String8 data, MACHO_Bin *module)
{
  return macho_bin_from_data_selected(arena, data, module);
}

internal String8
macho_data_from_section(MACHO_Bin *bin, MACHO_Section *section)
{
  String8 result = {0};
  U32 kind = section->flags & 0xff;
  if(bin->valid && kind != 1 && kind != 0xc && kind != 0x12 && macho_range_valid(bin->data, section->offset, section->file_size))
  { result = str8_substr(bin->data, r1u64(section->offset, section->offset+section->file_size)); }
  return result;
}

internal MACHO_Symbol
macho_symbol_from_index(MACHO_Bin *bin, U64 index)
{
  MACHO_Symbol result = {0};
  if(bin->valid && index < bin->symbol_count)
  {
    U64 off = bin->symbol_offset+index*16;
    U32 strx = macho_u32(bin->data, off, 0);
    U8 type = bin->data.str[off+4];
    U8 sect = bin->data.str[off+5];
    if(!(type & 0xe0) && (type & 0x0e) == 0x0e && sect != 0 && sect <= bin->section_count && strx < bin->string_size)
    {
      String8 strings = str8_substr(bin->data, r1u64(bin->string_offset, (U64)bin->string_offset+bin->string_size));
      U64 end = strx;
      for(; end < strings.size && strings.str[end] != 0; end += 1) {}
      if(end < strings.size)
      {
        result.name = str8_substr(strings, r1u64(strx, end));
        result.address = macho_u64(bin->data, off+8, 0);
        result.section = sect;
        result.external = !!(type & 1);
      }
    }
  }
  return result;
}

internal B32
macho_uuid_match(MACHO_Bin *a, MACHO_Bin *b)
{
  return a->valid && b->valid && a->has_uuid && b->has_uuid && MemoryMatch(a->uuid, b->uuid, 16);
}

internal String8
macho_dsym_path_from_module(Arena *arena, String8 module_path, MACHO_Bin *module)
{
  String8 result = {0};
  if(!module->valid || !module->has_uuid || module->file_type == 0xa) { return result; }
  Temp scratch = scratch_begin(&arena, 1);
  String8 basename = str8_skip_last_slash(module_path);
  String8 directory = str8_chop_last_slash(module_path);
  String8List candidates = {0};
  str8_list_pushf(scratch.arena, &candidates, "%S.dSYM/Contents/Resources/DWARF/%S", module_path, basename);
  // A relocated bundle can keep its original name. Check sibling dSYMs by UUID.
  FileIter *iter = file_iter_begin(scratch.arena, directory, FileIterFlag_SkipFiles);
  for(FileInfo info = {0}; file_iter_next(scratch.arena, iter, &info);)
  {
    if(str8_match(str8_postfix(info.name, 5), str8_lit(".dSYM"), 0))
    {
      String8 dwarf_dir = str8f(scratch.arena, "%S/%S/Contents/Resources/DWARF", directory, info.name);
      FileIter *dwarf_iter = file_iter_begin(scratch.arena, dwarf_dir, FileIterFlag_SkipFolders);
      for(FileInfo dwarf_info = {0}; file_iter_next(scratch.arena, dwarf_iter, &dwarf_info);)
      { str8_list_pushf(scratch.arena, &candidates, "%S/%S", dwarf_dir, dwarf_info.name); }
      file_iter_end(dwarf_iter);
    }
  }
  file_iter_end(iter);
  for(String8Node *n = candidates.first; n != 0; n = n->next)
  {
    File file = file_open(AccessFlag_Read|AccessFlag_ShareRead, n->string);
    FileProperties props = properties_from_file(file);
    FileMap map = file_map_open(AccessFlag_Read, file);
    void *ptr = props.size ? file_map_view_open(map, AccessFlag_Read, r1u64(0, props.size)) : 0;
    if(ptr)
    {
      Temp candidate_temp = temp_begin(scratch.arena);
      MACHO_Bin dsym = macho_dsym_bin_from_data(scratch.arena, str8(ptr, props.size), module);
      B32 matches = dsym.valid;
      temp_end(candidate_temp);
      file_map_view_close(map, ptr, r1u64(0, props.size));
      if(matches) { result = push_str8_copy(arena, n->string); }
    }
    file_map_close(map);
    file_close(file);
    if(result.size) { break; }
  }
  scratch_end(scratch);
  return result;
}
