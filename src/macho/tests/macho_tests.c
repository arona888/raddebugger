// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal void
macho_test_u32(U8 *data, U64 offset, U32 value)
{
  MemoryCopy(data+offset, &value, sizeof(value));
}

internal void
macho_test_u64(U8 *data, U64 offset, U64 value)
{
  MemoryCopy(data+offset, &value, sizeof(value));
}

internal String8
macho_test_image(Arena *arena)
{
  U8 *data = push_array(arena, U8, 512);
  macho_test_u32(data, 0, 0xfeedfacf);
  macho_test_u32(data, 4, 0x0100000c);
  macho_test_u32(data, 12, 2);
  macho_test_u32(data, 16, 4);
  macho_test_u32(data, 20, 72+72+24+24);
  macho_test_u32(data, 32, 0x19);
  macho_test_u32(data, 36, 72);
  MemoryCopy(data+40, "__PAGEZERO", 10);
  macho_test_u64(data, 64, 0x100000000ull);
  macho_test_u32(data, 104, 0x19);
  macho_test_u32(data, 108, 72);
  MemoryCopy(data+112, "__TEXT", 6);
  macho_test_u64(data, 128, 0x100000000ull);
  macho_test_u64(data, 136, 0x4000);
  macho_test_u64(data, 152, 512);
  macho_test_u32(data, 164, 5);
  macho_test_u32(data, 176, 0x1b);
  macho_test_u32(data, 180, 24);
  for(U64 i = 0; i < 16; i += 1) { data[184+i] = (U8)(i+1); }
  macho_test_u32(data, 200, 0x80000028);
  macho_test_u32(data, 204, 24);
  macho_test_u64(data, 208, 0x100);
  return str8(data, 512);
}

Test(macho_text_base_and_entry)
{
  String8 image = macho_test_image(arena);
  MACHO_Bin bin = macho_bin_from_data(arena, image);
  TestCheck(bin.valid && bin.arch == Arch_arm64);
  TestCheck(bin.preferred_base == 0x100000000ull);
  TestCheck(bin.image_size == 0x4000 && bin.entry_voff == 0x100);
  TestCheck(bin.has_uuid && bin.uuid[0] == 1 && bin.uuid[15] == 16);
}

Test(macho_rejects_truncated_load_commands)
{
  String8 image = macho_test_image(arena);
  MACHO_Bin bin = macho_bin_from_data(arena, str8_prefix(image, 223));
  TestCheck(!bin.valid);
  macho_test_u32(image.str, 108, 0xfffffff8);
  bin = macho_bin_from_data(arena, image);
  TestCheck(!bin.valid);
}

Test(macho_universal_slice_bounds)
{
  String8 thin = macho_test_image(arena);
  U8 *fat = push_array(arena, U8, 0x1200);
  // A little-endian universal container, with a page-aligned ARM64 image.
  macho_test_u32(fat, 0, 0xcafebabe);
  macho_test_u32(fat, 4, 1);
  macho_test_u32(fat, 8, 0x0100000c);
  macho_test_u32(fat, 16, 0x1000);
  macho_test_u32(fat, 20, thin.size);
  macho_test_u32(fat, 24, 12);
  MemoryCopy(fat+0x1000, thin.str, thin.size);
  MACHO_Bin bin = macho_bin_from_data(arena, str8(fat, 0x1200));
  TestCheck(bin.valid && bin.slice_offset == 0x1000 && bin.entry_voff == 0x100);
  bin = macho_bin_from_data(arena, str8(fat, 0x11ff));
  TestCheck(!bin.valid);
}

internal String8
macho_test_universal_dsym(Arena *arena)
{
  String8 thin = macho_test_image(arena);
  macho_test_u32(thin.str, 12, 0xa);
  U8 *fat = push_array(arena, U8, 0x2200);
  macho_test_u32(fat, 0, 0xcafebabe);
  macho_test_u32(fat, 4, 2);
  for(U64 i = 0; i < 2; i += 1)
  {
    U64 off = 8+i*20;
    U64 slice_off = 0x1000+i*0x1000;
    macho_test_u32(fat, off, 0x0100000c);
    macho_test_u32(fat, off+4, i == 0 ? 2 : 0); // arm64e precedes generic ARM64.
    macho_test_u32(fat, off+8, slice_off);
    macho_test_u32(fat, off+12, thin.size);
    macho_test_u32(fat, off+16, 12);
    MemoryCopy(fat+slice_off, thin.str, thin.size);
    macho_test_u32(fat, slice_off+8, i == 0 ? 2 : 0);
  }
  fat[0x1000+184] = 0xff;
  return str8(fat, 0x2200);
}

Test(macho_dsym_uuid_selects_matching_eligible_slice)
{
  MACHO_Bin module = macho_bin_from_data(arena, macho_test_image(arena));
  String8 data = macho_test_universal_dsym(arena);
  MACHO_Bin ordinary = macho_bin_from_data(arena, data);
  TestCheck(ordinary.valid && ordinary.slice_offset == 0x1000);
  TestCheck(!macho_uuid_match(&module, &ordinary));
  MACHO_Bin selected = macho_dsym_bin_from_data(arena, data, &module);
  TestCheck(selected.valid && selected.file_type == 0xa && selected.slice_offset == 0x2000);
  TestCheck(selected.data.str == data.str+0x2000 && selected.data.size == 512);
  TestCheck(macho_uuid_match(&module, &selected));
  // Reversing UUID order must choose the first slice instead.
  data.str[0x1000+184] = 1;
  data.str[0x2000+184] = 0xff;
  selected = macho_dsym_bin_from_data(arena, data, &module);
  TestCheck(selected.valid && selected.slice_offset == 0x1000);
  TestCheck(macho_uuid_match(&module, &selected));
}

Test(macho_dsym_uuid_rejects_no_eligible_match)
{
  MACHO_Bin module = macho_bin_from_data(arena, macho_test_image(arena));
  String8 data = macho_test_universal_dsym(arena);
  data.str[0x2000+184] = 0xfe;
  MACHO_Bin selected = macho_dsym_bin_from_data(arena, data, &module);
  TestCheck(!selected.valid);
  // A matching UUID in an ineligible CPU slice must not be accepted.
  data.str[0x2000+184] = 1;
  macho_test_u32(data.str, 28, 0x01000007);
  selected = macho_dsym_bin_from_data(arena, data, &module);
  TestCheck(!selected.valid);
}

Test(macho_dsym_uuid_rejects_malformed_slice_bounds)
{
  MACHO_Bin module = macho_bin_from_data(arena, macho_test_image(arena));
  String8 data = macho_test_universal_dsym(arena);
  MACHO_Bin selected = macho_dsym_bin_from_data(arena, str8_prefix(data, data.size-1), &module);
  TestCheck(!selected.valid);
  // Even an earlier matching slice cannot bypass later malformed bounds.
  data.str[0x1000+184] = 1;
  macho_test_u32(data.str, 40, 0x201);
  selected = macho_dsym_bin_from_data(arena, data, &module);
  TestCheck(!selected.valid);
}
