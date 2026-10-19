// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RDI_FROM_MACHO_H
#define RDI_FROM_MACHO_H

internal RDIM_BinarySectionList m2r_rdi_binary_sections_from_macho(Arena *arena, MACHO_Bin *bin);
internal void m2r_apply_link_names(Arena *arena, MACHO_Bin *bin, RDIM_BakeParams *params);

#endif // RDI_FROM_MACHO_H
