// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal B32
dw_is_dwarf_present_from_macho_bin(MACHO_Bin *bin)
{
  B32 result = 0;
  if(bin->valid)
  {
    for(U64 i = 0; i < bin->section_count; i += 1)
    {
      MACHO_Section *section = &bin->sections[i];
      if(str8_match(section->segment_name, str8_lit("__DWARF"), 0) && dw_section_kind_from_string(section->name) == DW_SectionKind_Info && macho_data_from_section(bin, section).size != 0)
      { result = 1; break; }
    }
  }
  return result;
}

internal DW_Raw
dw_raw_from_macho_bin(Arena *arena, MACHO_Bin *bin)
{
  DW_Raw result = {0};
  if(bin->valid)
  {
    for(U64 i = 0; i < bin->section_count; i += 1)
    {
      MACHO_Section *section = &bin->sections[i];
      if(str8_match(section->segment_name, str8_lit("__DWARF"), 0))
      {
        DW_SectionKind kind = dw_section_kind_from_string(section->name);
        if(kind != DW_SectionKind_Null)
        {
          result.sec[kind].name = push_str8_copy(arena, section->name);
          result.sec[kind].data = macho_data_from_section(bin, section);
        }
      }
    }
  }
  return result;
}
