// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal RDIM_BinarySectionList
m2r_rdi_binary_sections_from_macho(Arena *arena, MACHO_Bin *bin)
{
  RDIM_BinarySectionList result = {0};
  if(bin->valid)
  {
    for(U64 i = 0; i < bin->section_count; i += 1)
    {
      MACHO_Section *src = &bin->sections[i];
      if(src->addr < bin->preferred_base || str8_match(src->segment_name, str8_lit("__DWARF"), 0)) { continue; }
      RDIM_BinarySection *dst = rdim_binary_section_list_push(arena, &result);
      dst->name = push_str8f(arena, "%S,%S", src->segment_name, src->name);
      dst->flags = 0;
      if(src->protections & 1) { dst->flags |= RDI_BinarySectionFlag_Read; }
      if(src->protections & 2) { dst->flags |= RDI_BinarySectionFlag_Write; }
      if(src->protections & 4) { dst->flags |= RDI_BinarySectionFlag_Execute; }
      dst->voff_first = src->addr-bin->preferred_base;
      dst->voff_opl = dst->voff_first+src->size;
      dst->foff_first = bin->slice_offset+src->offset;
      dst->foff_opl = dst->foff_first+macho_data_from_section(bin, src).size;
    }
  }
  return result;
}

internal int
m2r_symbol_address_compare(MACHO_Symbol *a, MACHO_Symbol *b)
{
  return a->address < b->address ? -1 : a->address > b->address ? 1 : (int)b->external-(int)a->external;
}

internal void
m2r_apply_link_names(Arena *arena, MACHO_Bin *bin, RDIM_BakeParams *params)
{
  Temp scratch = scratch_begin(&arena, 1);
  MACHO_Symbol *symbols = push_array_no_zero(scratch.arena, MACHO_Symbol, bin->symbol_count);
  U64 count = 0;
  for(U64 i = 0; i < bin->symbol_count; i += 1)
  {
    MACHO_Symbol symbol = macho_symbol_from_index(bin, i);
    if(symbol.name.size && symbol.address >= bin->preferred_base) { symbols[count++] = symbol; }
  }
  quick_sort(symbols, count, sizeof(*symbols), m2r_symbol_address_compare);
  for(RDIM_UnitChunkNode *unit_chunk = params->units.first; unit_chunk != 0; unit_chunk = unit_chunk->next)
  {
    for(U64 ui = 0; ui < unit_chunk->count; ui += 1)
    {
      for(RDIM_SymbolChunkNode *chunk = unit_chunk->v[ui].procedures.first; chunk != 0; chunk = chunk->next)
      {
        for(U64 pi = 0; pi < chunk->count; pi += 1)
        {
          RDIM_Symbol *procedure = &chunk->v[pi];
          if(procedure->link_name.size || procedure->root_scope == 0 || procedure->root_scope->voff_ranges.first == 0) { continue; }
          U64 address = bin->preferred_base+procedure->root_scope->voff_ranges.min;
          U64 min = 0, max = count;
          for(; min < max;)
          {
            U64 mid = min+(max-min)/2;
            if(symbols[mid].address < address) { min = mid+1; } else { max = mid; }
          }
          if(min < count && symbols[min].address == address)
          { procedure->link_name = push_str8_copy(arena, symbols[min].name); }
        }
      }
    }
  }
  scratch_end(scratch);
}
