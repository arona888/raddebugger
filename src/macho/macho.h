// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef MACHO_H
#define MACHO_H

typedef struct MACHO_Section MACHO_Section;
struct MACHO_Section
{
  String8 name;
  String8 segment_name;
  U64 addr;
  U64 size;
  U64 offset;
  U64 file_size;
  U32 flags;
  U32 protections;
};

typedef struct MACHO_Bin MACHO_Bin;
struct MACHO_Bin
{
  B32 valid;
  Arch arch;
  U32 file_type;
  String8 data; // Selected thin slice; all file offsets below are slice-relative.
  U64 slice_offset;
  U64 preferred_base; // __TEXT, excluding __PAGEZERO.
  U64 image_size;
  U64 entry_voff;
  U8 uuid[16];
  B32 has_uuid;
  U64 section_count;
  MACHO_Section *sections;
  U32 symbol_offset;
  U32 symbol_count;
  U32 string_offset;
  U32 string_size;
};

typedef struct MACHO_Symbol MACHO_Symbol;
struct MACHO_Symbol
{
  String8 name;
  U64 address;
  U8 section;
  B32 external;
};

internal B32 macho_has_magic(String8 data);
internal MACHO_Bin macho_bin_from_data(Arena *arena, String8 data);
// Select an MH_DSYM ARM64 slice by module UUID, validating every universal slice bound.
internal MACHO_Bin macho_dsym_bin_from_data(Arena *arena, String8 data, MACHO_Bin *module);
internal String8 macho_data_from_section(MACHO_Bin *bin, MACHO_Section *section);
internal MACHO_Symbol macho_symbol_from_index(MACHO_Bin *bin, U64 index);
internal B32 macho_uuid_match(MACHO_Bin *a, MACHO_Bin *b);
internal String8 macho_dsym_path_from_module(Arena *arena, String8 module_path, MACHO_Bin *module);

#endif // MACHO_H
