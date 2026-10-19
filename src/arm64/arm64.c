// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = {OffsetOf(ARM64_RegBlock, member), OffsetOf(ARM64_RegBlock, member) + size},
global Rng1U16 arm64_reg_code_rng_table[ARM64_RegCode_COUNT] = {ARM64_Reg_XList};
#undef X
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = str8_lit_comp(#name),
global String8 arm64_reg_code_name_table[ARM64_RegCode_COUNT] = {ARM64_Reg_XList};
#undef X
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = (ARM64_RegCode_##name == ARM64_RegCode_##base ? 0 : ARM64_RegCode_##base),
global U8 arm64_reg_code_base_table[ARM64_RegCode_COUNT] = {ARM64_Reg_XList};
#undef X
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = vector,
global B8 arm64_reg_code_is_vector_table[ARM64_RegCode_COUNT] = {ARM64_Reg_XList};
#undef X
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = dw,
global U8 dw_reg_code_from_arm64_table[ARM64_RegCode_COUNT] = {[0] = 255, ARM64_Reg_XList};
#undef X
#define X(name, member, size, base, dw, rdi, vector) [ARM64_RegCode_##name] = rdi,
global U8 rdi_reg_code_from_arm64_table[ARM64_RegCode_COUNT] = {ARM64_Reg_XList};
#undef X

internal U64
arm64_address_from_pointer(U64 pointer)
{
#if OS_MAC
  return pointer & 0x0000ffffffffffffull;
#else
  return pointer;
#endif
}
