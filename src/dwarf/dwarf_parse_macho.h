// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DWARF_PARSE_MACHO_H
#define DWARF_PARSE_MACHO_H

internal B32 dw_is_dwarf_present_from_macho_bin(MACHO_Bin *bin);
internal DW_Raw dw_raw_from_macho_bin(Arena *arena, MACHO_Bin *bin);

#endif // DWARF_PARSE_MACHO_H
