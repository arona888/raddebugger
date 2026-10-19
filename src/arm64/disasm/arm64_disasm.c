// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// LLVM's stable C disassembler ABI is exported by Apple's native CLT libLTO.
// Keep the library and decoder thread-local: LLVM contexts are not shared across workers.
typedef void *ARM64_LLVMCreateDisasm(char *, char *, char *, void *, int, void *, void *);
typedef size_t ARM64_LLVMDisasmInstruction(void *, U8 *, U64, U64, char *, size_t);
typedef void ARM64_LLVMInitialize(void);
typedef struct ARM64_Disassembler ARM64_Disassembler;
struct ARM64_Disassembler
{
  B32 initialized;
  Library library;
  void *context;
  ARM64_LLVMDisasmInstruction *instruction;
};
thread_static ARM64_Disassembler arm64_disassembler;

internal DASM_Inst
arm64_dasm_inst_from_code(Arena *arena, U64 vaddr, String8 code, DASM_Syntax syntax)
{
  DASM_Inst inst = {0};
  if(code.size < 4 || (vaddr & 3))
  {
    return inst;
  }
  ARM64_Disassembler *decoder = &arm64_disassembler;
  if(!decoder->initialized)
  {
    decoder->initialized = 1;
    String8 paths[] =
    {
#if OS_MAC
      str8_lit("/Library/Developer/CommandLineTools/usr/lib/libLTO.dylib"),
      str8_lit("/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libLTO.dylib"),
      str8_lit("libLTO.dylib"),
#elif OS_WINDOWS
      str8_lit("LLVM-C.dll"),
#else
      str8_lit("libLLVM.so"),
#endif
    };
    for EachElement(idx, paths)
    {
      decoder->library = library_open(paths[idx]);
      if(decoder->library.u64[0] != 0) {break;}
    }
    if(decoder->library.u64[0] != 0)
    {
      ARM64_LLVMInitialize *initialize = (ARM64_LLVMInitialize *)library_load_proc(decoder->library, str8_lit("lto_initialize_disassembler"));
      if(initialize != 0) {initialize();}
      else
      {
        char *names[] = {"LLVMInitializeAArch64TargetInfo", "LLVMInitializeAArch64Target", "LLVMInitializeAArch64TargetMC", "LLVMInitializeAArch64Disassembler"};
        for EachElement(idx, names)
        {
          ARM64_LLVMInitialize *init = (ARM64_LLVMInitialize *)library_load_proc(decoder->library, str8_cstring(names[idx]));
          if(init != 0) {init();}
        }
      }
      ARM64_LLVMCreateDisasm *create = (ARM64_LLVMCreateDisasm *)library_load_proc(decoder->library, str8_lit("LLVMCreateDisasmCPUFeatures"));
      decoder->instruction = (ARM64_LLVMDisasmInstruction *)library_load_proc(decoder->library, str8_lit("LLVMDisasmInstruction"));
      if(create != 0 && decoder->instruction != 0)
      {
        decoder->context = create("aarch64-apple-darwin", "apple-m1", "+v8.3a,+pauth,+fp-armv8,+neon", 0, 0, 0, 0);
      }
    }
    if(decoder->context == 0)
    {
      log_user_errorf("ARM64 disassembler unavailable: install Apple's Command Line Tools or provide libLLVM/libLTO.\n");
    }
  }
  if(decoder->context == 0) {return inst;}
  char text[512] = {0};
  size_t size = decoder->instruction(decoder->context, code.str, 4, vaddr, text, sizeof(text));
  // An undefined encoding is still a four-byte A64 instruction slot.
  inst.size = 4;
  inst.string = size == 4 ? push_str8_copy(arena, str8_skip_chop_whitespace(str8_cstring(text))) : push_str8f(arena, ".inst 0x%08x", (U32)code.str[0] | (U32)code.str[1]<<8 | (U32)code.str[2]<<16 | (U32)code.str[3]<<24);
  U32 op = (U32)code.str[0] | (U32)code.str[1]<<8 | (U32)code.str[2]<<16 | (U32)code.str[3]<<24;
  inst.flags = DASM_InstFlag_NonFlow;
  if((op & 0x7c000000) == 0x14000000) // B/BL imm26
  {
    inst.flags = (op & 0x80000000) ? DASM_InstFlag_Call : DASM_InstFlag_UnconditionalJump;
    inst.dst_vaddr = vaddr + ((S64)((S32)(op << 6) >> 6) * 4);
  }
  else if((op & 0xff000010) == 0x54000000 || (op & 0x7e000000) == 0x34000000) // B.cond/CBZ/CBNZ
  {
    inst.flags = DASM_InstFlag_Branch;
    inst.dst_vaddr = vaddr + ((S64)((S32)(op << 8) >> 13) * 4);
  }
  else if((op & 0x7e000000) == 0x36000000) // TBZ/TBNZ
  {
    inst.flags = DASM_InstFlag_Branch;
    inst.dst_vaddr = vaddr + ((S64)((S32)(op << 13) >> 18) * 4);
  }
  else if((op & 0xfffffc1f) == 0xd65f0000 || op == 0xd65f0bff || op == 0xd65f0fff || (op & 0xfffffbff) == 0xd69f03e0) // RET/RETAA/RETAB/ERET
  {
    inst.flags = DASM_InstFlag_Return;
  }
  else if((op & 0xfe000000) == 0xd6000000) // BR/BLR and authenticated variants
  {
    U32 kind = (op >> 21) & 15;
    if(kind == 1 || kind == 9) {inst.flags = DASM_InstFlag_Call;}
    else if(kind == 0 || kind == 8) {inst.flags = DASM_InstFlag_UnconditionalJump;}
  }
  // SP arithmetic/logical immediates (not flag-setting aliases), and writeback.
  if(((op & 0x1f000000) == 0x11000000 && !(op & 0x20000000) && (op & 31) == 31) ||
     ((op & 0x1f200000) == 0x0b200000 && !(op & 0x20000000) && (op & 31) == 31) ||
     ((op & 0x1f800000) == 0x12000000 && ((op >> 29) & 3) != 3 && (op & 31) == 31))
  {
    inst.flags |= DASM_InstFlag_ChangesStackPointer;
  }
  if(((op >> 5) & 31) == 31 &&
     (((op & 0x3a000000) == 0x28000000 && ((op >> 23) & 3) != 0 && ((op >> 23) & 3) != 2) ||
      ((op & 0x3b200000) == 0x38000000 && ((op >> 10) & 1))))
  {
    inst.flags |= DASM_InstFlag_ChangesStackPointer;
  }
  return inst;
}
