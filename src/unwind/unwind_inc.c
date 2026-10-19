// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#if defined(ARM64_H)
# include "arm64/arm64_unwind.c"
#endif
#include "unwind.c"
#if defined(PE_H) && defined(X64_H)
# include "pe/x64/unwind/pe_x64_unwind.c"
#endif
#if defined(EH_FRAME_H)
# include "eh_frame/unwind/eh_frame_unwind.c"
#endif
