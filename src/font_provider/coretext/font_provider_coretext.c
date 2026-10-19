// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ CoreText Font Ownership

internal FP_Handle
fp_ct_handle_from_provider(CGDataProviderRef provider)
{
  FP_Handle result = {0};
  if(provider != 0)
  {
    CGFontRef graphics_font = CGFontCreateWithDataProvider(provider);
    if(graphics_font != 0)
    {
      // Keep the base font in design units, so its metrics match the other providers.
      CTFontRef font = CTFontCreateWithGraphicsFont(graphics_font, CGFontGetUnitsPerEm(graphics_font), 0, 0);
      result.u64[0] = (U64)font;
      CGFontRelease(graphics_font);
    }
  }
  return result;
}

////////////////////////////////
//~ Backend Implementations

fp_hook void
fp_init(void)
{
  // CoreText initializes its services on demand; each handle owns its font.
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  FP_Handle result = {0};
  CFStringRef path_string = CFStringCreateWithBytes(kCFAllocatorDefault, path.str, path.size, kCFStringEncodingUTF8, false);
  if(path_string != 0)
  {
    CFURLRef url = CFURLCreateWithFileSystemPath(kCFAllocatorDefault, path_string, kCFURLPOSIXPathStyle, false);
    if(url != 0)
    {
      CGDataProviderRef provider = CGDataProviderCreateWithURL(url);
      result = fp_ct_handle_from_provider(provider);
      if(provider != 0) { CGDataProviderRelease(provider); }
      CFRelease(url);
    }
    CFRelease(path_string);
  }
  return result;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  FP_Handle result = {0};
  if(data_ptr != 0 && data_ptr->size != 0)
  {
    // The caller's bundled bytes have static lifetime. The font retains the provider.
    CGDataProviderRef provider = CGDataProviderCreateWithData(0, data_ptr->str, data_ptr->size, 0);
    result = fp_ct_handle_from_provider(provider);
    if(provider != 0) { CGDataProviderRelease(provider); }
  }
  return result;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  if(handle.u64[0] != 0)
  {
    CFRelease((CTFontRef)handle.u64[0]);
  }
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  FP_Metrics result = {0};
  CTFontRef font = (CTFontRef)handle.u64[0];
  if(font != 0)
  {
    result.design_units_per_em = CTFontGetUnitsPerEm(font);
    F32 scale = result.design_units_per_em / CTFontGetSize(font);
    result.ascent = scale * CTFontGetAscent(font);
    result.descent = scale * CTFontGetDescent(font);
    result.line_gap = scale * CTFontGetLeading(font);
    result.capital_height = scale * CTFontGetCapHeight(font);
  }
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  FP_RasterResult result = {0};
  CTFontRef base_font = (CTFontRef)handle.u64[0];
  if(base_font != 0 && size > 0 && string.size != 0)
  {
    // Font sizes, including sizes on Retina displays, use the provider's 96-DPI contract.
    CTFontRef font = CTFontCreateCopyWithAttributes(base_font, (96.f/72.f)*size, 0, 0);
    CFStringRef text = CFStringCreateWithBytes(kCFAllocatorDefault, string.str, string.size, kCFStringEncodingUTF8, false);
    if(font != 0 && text != 0)
    {
      const void *keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
      const void *values[] = {font, kCFBooleanTrue};
      CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, ArrayCount(keys), &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      CFAttributedStringRef attributed_text = CFAttributedStringCreate(kCFAllocatorDefault, text, attributes);
      CTLineRef line = CTLineCreateWithAttributedString(attributed_text);
      if(line != 0)
      {
        // CTLine handles supplementary-plane characters and shaping, not UTF-16 code units.
        CGFloat advance = CTLineGetTypographicBounds(line, 0, 0, 0);
        CGRect bounds = CTLineGetBoundsWithOptions(line, kCTLineBoundsUseGlyphPathBounds);
        CGFloat ascent = CTFontGetAscent(font);
        CGFloat descent = CTFontGetDescent(font);
        CGFloat leading = CTFontGetLeading(font);
        F32 width = ceil_f32(Max(advance, CGRectGetMaxX(bounds))) + 1;
        F32 height = round_f32(ascent + descent + leading) + 1;
        result.advance = round_f32(advance);
        if(width > 0 && width <= 32767 && height > 0 && height <= 32767)
        {
          S32 w = (S32)width;
          S32 h = (S32)height;
          U8 *pixels = push_array(arena, U8, (U64)w*h*4);
          CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
          CGContextRef context = CGBitmapContextCreate(pixels, w, h, 8, (U64)w*4, color_space, kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
          CGColorSpaceRelease(color_space);
          if(context != 0)
          {
            B32 smooth = !!(flags & FP_RasterFlag_Smooth);
            B32 hinted = !!(flags & FP_RasterFlag_Hinted);
            CGContextSetAllowsAntialiasing(context, true);
            CGContextSetShouldAntialias(context, smooth);
            // Atlas alpha is grayscale coverage, never LCD subpixel color.
            CGContextSetShouldSmoothFonts(context, false);
            CGContextSetShouldSubpixelPositionFonts(context, !hinted);
            CGContextSetShouldSubpixelQuantizeFonts(context, hinted);
            CGContextSetRGBFillColor(context, 1, 1, 1, 1);
            // Backing memory is top-down RGBA. Flip the context and then the text
            // matrix so glyphs stay upright, with the DWrite-compatible baseline.
            CGContextTranslateCTM(context, 0, h);
            CGContextScaleCTM(context, 1, -1);
            CGContextSetTextMatrix(context, CGAffineTransformMakeScale(1, -1));
            CGFloat baseline = h - round_f32(descent) - round_f32(leading);
            CGContextSetTextPosition(context, 0, baseline);
            CTLineDraw(line, context);
            CGContextRelease(context);

            U64 coverage = 0;
            for(U64 idx = 0; idx < (U64)w*h; idx += 1)
            {
              coverage += pixels[4*idx+3];
              // Renderer expects straight white RGB and glyph coverage in alpha.
              pixels[4*idx+0] = 255;
              pixels[4*idx+1] = 255;
              pixels[4*idx+2] = 255;
            }
            if(coverage != 0)
            {
              result.atlas_dim = v2s16(w, h);
              result.atlas = pixels;
            }
          }
        }
        CFRelease(line);
      }
      CFRelease(attributed_text);
      CFRelease(attributes);
    }
    if(text != 0) { CFRelease(text); }
    if(font != 0) { CFRelease(font); }
  }
  ProfEnd();
  return result;
}
