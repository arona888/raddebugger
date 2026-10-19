// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef FONT_PROVIDER_CORETEXT_H
#define FONT_PROVIDER_CORETEXT_H

#undef internal
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#define internal static

internal FP_Handle fp_ct_handle_from_provider(CGDataProviderRef provider);

#endif // FONT_PROVIDER_CORETEXT_H
