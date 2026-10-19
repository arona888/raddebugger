// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

Test(arm64_unwind_frame_chain)
{
  U64 frame_a[] = {0x1140, 0x8040};
  U64 frame_b[] = {0, 0x9000};
  MemoryMap memory = {0};
  memory_map_push(arena, &memory, r1u64(0x1100, 0x1110), frame_a);
  memory_map_push(arena, &memory, r1u64(0x1140, 0x1150), frame_b);
  ARM64_RegBlock regs = {.sp = 0x1000, .pc = 0x7000};
  regs.x[29] = 0x1100;
  UWND_StepResult step = uwnd_step_moduleless(Arch_arm64, &memory, &regs);
  TestCheck(step.status == UWND_StepStatus_Good);
  TestCheck(regs.pc == 0x8040 && regs.sp == 0x1110 && regs.x[29] == 0x1140);
  step = uwnd_step_moduleless(Arch_arm64, &memory, &regs);
  TestCheck(step.status == UWND_StepStatus_Good);
  TestCheck(regs.pc == 0x9000 && regs.sp == 0x1150 && regs.x[29] == 0);
  // A root frame is normal termination, not a corrupt stack. Returning Error
  // here used to discard every otherwise valid frame in the callstack cache.
  step = uwnd_step_moduleless(Arch_arm64, &memory, &regs);
  TestCheck(step.status == UWND_StepStatus_Good && regs.pc == 0);
}

Test(arm64_unwind_rejects_backwards_frame)
{
  U64 frame[] = {0x1000, 0x8040};
  MemoryMap memory = {0};
  memory_map_push(arena, &memory, r1u64(0x1100, 0x1110), frame);
  ARM64_RegBlock regs = {.sp = 0x1000, .pc = 0x7000};
  regs.x[29] = 0x1100;
  UWND_StepResult step = uwnd_step_moduleless(Arch_arm64, &memory, &regs);
  TestCheck(step.status == UWND_StepStatus_Error);
  TestCheck(regs.pc == 0x7000 && regs.sp == 0x1000 && regs.x[29] == 0x1100);
}

Test(arm64_unwind_requests_missing_frame)
{
  MemoryMap memory = {0};
  ARM64_RegBlock regs = {.sp = 0x1000, .pc = 0x7000};
  regs.x[29] = 0x1100;
  UWND_StepResult step = uwnd_step_moduleless(Arch_arm64, &memory, &regs);
  TestCheck(step.status == UWND_StepStatus_FailedMemoryRead);
  TestCheck(step.missed_read_vaddr_range.min == 0x1100 && step.missed_read_vaddr_range.max == 0x1110);
}

Test(arm64_unwind_completed_nonleaf_epilogue)
{
  struct {U32 code[8]; U64 return_idx;} cases[] =
  {
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd65f03c0}, 4},
    // STP FP,LR,[SP,#-16]!; MOV FP,SP; BL; LDP FP,LR,[SP],#16; RET
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0x14000002, 0xd503201f, 0xa8c17bfd, 0xd65f03c0}, 6},
    // Control flow in the body does not invalidate an executed local epilogue.
    {{0xd10083ff, 0xa9017bfd, 0x910043fd, 0x94000010, 0xa9417bfd, 0x910083ff, 0xd65f03c0}, 6},
    // SUB SP,SP,#32; STP FP,LR,[SP,#16]; ADD FP,SP,#16; BL;
    // LDP FP,LR,[SP,#16]; ADD SP,SP,#32; RET
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd50323bf, 0xd65f03c0}, 5},
    // An authenticated return can either follow AUTIASP or use RETAA/RETAB.
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd65f0bff}, 4},
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd65f0fff}, 4},
  };
  for EachElement(i, cases)
  {
    U64 caller_frame[] = {0, 0x9000};
    MemoryMap memory = {0};
    memory_map_push(arena, &memory, r1u64(0x7000, 0x7000 + sizeof(cases[i].code)), cases[i].code);
    memory_map_push(arena, &memory, r1u64(0x1140, 0x1150), caller_frame);
    ARM64_RegBlock regs = {.sp = 0x1100, .pc = 0x7000 + 4*cases[i].return_idx};
    regs.x[29] = 0x1140;
    regs.x[30] = 0x8040;
    UWND_ModuleInfo module = {.function_vaddr = 0x7000};
    U64 cfa = 0;
    UWND_StepResult step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, &cfa);
    TestCheck(step.status == UWND_StepStatus_Good);
    TestCheck(regs.pc == 0x8040 && regs.sp == 0x1100 && regs.x[29] == 0x1140 && regs.x[30] == 0);
    TestCheck(cfa == 0x1100);
    // Reconstructed frames have no symbol-derived top-frame start and must
    // follow the frame chain, not reuse the previous frame's live LR.
    module.function_vaddr = 0;
    step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, &cfa);
    TestCheck(step.status == UWND_StepStatus_Good);
    TestCheck(regs.pc == 0x9000 && regs.sp == 0x1150);
  }
}

Test(arm64_unwind_nonleaf_body_and_restore_boundary)
{
  U32 code[] = {0xa9bf7bfd, 0x910003fd, 0x94000010, 0xd503201f, 0xd503201f, 0xa8c17bfd, 0xd65f03c0};
  U64 frame[] = {0x1140, 0x8040};
  MemoryMap memory = {0};
  memory_map_push(arena, &memory, r1u64(0x7000, 0x7000 + sizeof(code)), code);
  memory_map_push(arena, &memory, r1u64(0x1000, 0x1010), frame);
  UWND_ModuleInfo module = {.function_vaddr = 0x7000};
  // The body and the unexecuted LDP both still have the callee's frame and
  // the BL's return address in LR. Neither may fabricate a return from LR.
  for(U64 pc = 0x7010; pc <= 0x7014; pc += 4)
  {
    ARM64_RegBlock regs = {.sp = 0x1000, .pc = pc};
    regs.x[29] = 0x1000;
    regs.x[30] = 0x700c;
    UWND_StepResult step = arm64_uwnd_step_leaf(&memory, module.function_vaddr, &regs);
    TestCheck(step.status == UWND_StepStatus_Error);
    TestCheck(regs.pc == pc && regs.sp == 0x1000 && regs.x[29] == 0x1000 && regs.x[30] == 0x700c);
    step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, 0);
    TestCheck(step.status == UWND_StepStatus_Good);
    TestCheck(regs.pc == 0x8040 && regs.sp == 0x1010 && regs.x[29] == 0x1140);
  }
}

Test(arm64_unwind_nonleaf_missing_memory)
{
  U32 code[] = {0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd65f03c0};
  UWND_ModuleInfo module = {.function_vaddr = 0x7000};
  MemoryMap memory = {0};
  ARM64_RegBlock regs = {.sp = 0x1000, .pc = 0x700c};
  regs.x[29] = 0x1000;
  regs.x[30] = 0x700c;
  UWND_StepResult step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, 0);
  TestCheck(step.status == UWND_StepStatus_FailedMemoryRead);
  TestCheck(regs.pc == 0x700c && regs.sp == 0x1000 && regs.x[30] == 0x700c);
  memory_map_push(arena, &memory, r1u64(0x7000, 0x7000 + sizeof(code)), code);
  step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, 0);
  TestCheck(step.status == UWND_StepStatus_FailedMemoryRead);
  TestCheck(step.missed_read_vaddr_range.min == 0x1000 && step.missed_read_vaddr_range.max == 0x1010);
  TestCheck(regs.pc == 0x700c && regs.sp == 0x1000 && regs.x[30] == 0x700c);

  MemoryMap partial_memory = {0};
  memory_map_push(arena, &partial_memory, r1u64(0x7000, 0x7010), code);
  regs.pc = 0x7010;
  regs.sp = 0x1100;
  regs.x[29] = 0x1140;
  regs.x[30] = 0x8040;
  step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &partial_memory, &module, 0, &regs, 0);
  TestCheck(step.status == UWND_StepStatus_FailedMemoryRead);
  TestCheck(step.missed_read_vaddr_range.min == 0x7010 && step.missed_read_vaddr_range.max == 0x7014);
  TestCheck(regs.pc == 0x7010 && regs.sp == 0x1100 && regs.x[29] == 0x1140 && regs.x[30] == 0x8040);
}

#if OS_MAC
Test(arm64_disasm_control_flow)
{
  struct {U32 word; DASM_InstFlags flags; U64 destination;} cases[] =
  {
    {0x17ffffff, DASM_InstFlag_UnconditionalJump, 0xffc}, // B -4
    {0x94000002, DASM_InstFlag_Call, 0x1008},            // BL +8
    {0x54ffffe0, DASM_InstFlag_Branch, 0xffc},          // B.EQ -4
    {0xb5ffffe0, DASM_InstFlag_Branch, 0xffc},          // CBNZ X0, -4
    {0x3607ffe0, DASM_InstFlag_Branch, 0xffc},          // TBZ W0, #0, -4
    {0xd65f0fff, DASM_InstFlag_Return, 0},              // RETAB
    {0xd10083ff, DASM_InstFlag_NonFlow|DASM_InstFlag_ChangesStackPointer, 0},
    {0x927be93f, DASM_InstFlag_NonFlow|DASM_InstFlag_ChangesStackPointer, 0}, // AND SP,X9,#-32
    {0xb278013f, DASM_InstFlag_NonFlow|DASM_InstFlag_ChangesStackPointer, 0}, // ORR SP,X9,#0x100
    {0x52001d3f, DASM_InstFlag_NonFlow|DASM_InstFlag_ChangesStackPointer, 0}, // EOR WSP,W9,#0xff
    {0xf27be93f, DASM_InstFlag_NonFlow, 0}, // ANDS writes XZR, not SP
    {0x927be920, DASM_InstFlag_NonFlow, 0}, // AND X0,X9,#-32
  };
  for EachElement(i, cases)
  {
    DASM_Inst instruction = dasm_inst_from_code(arena, Arch_arm64, 0x1000, str8_struct(&cases[i].word), DASM_Syntax_Intel);
    TestCheck(instruction.size == 4);
    TestCheck(instruction.flags == cases[i].flags);
    TestCheck(instruction.dst_vaddr == cases[i].destination);
  }
}
#endif

Test(arm64_unwind_in_progress_epilogue)
{
  struct {U32 code[8]; U64 pc_idx; U64 sp;} cases[] =
  {
    {{0xd10083ff, 0xa9017bfd, 0x910043fd, 0x94000010, 0xa9417bfd, 0x910083ff, 0xd65f03c0}, 5, 0x10e0},
    {{0xa9bf7bfd, 0x910003fd, 0x94000010, 0xa8c17bfd, 0xd50323bf, 0xd65f03c0}, 4, 0x1100},
    {{0xd10083ff, 0xa9017bfd, 0x910043fd, 0x94000010, 0xa9417bfd, 0x910083ff, 0xd50323ff, 0xd65f03c0}, 5, 0x10e0},
  };
  for EachElement(i, cases)
  {
    U64 caller_frame[] = {0, 0x9000};
    MemoryMap memory = {0};
    memory_map_push(arena, &memory, r1u64(0x7000, 0x7000 + sizeof(cases[i].code)), cases[i].code);
    memory_map_push(arena, &memory, r1u64(0x1140, 0x1150), caller_frame);
    ARM64_RegBlock regs = {.sp = cases[i].sp, .pc = 0x7000 + 4*cases[i].pc_idx};
    regs.x[29] = 0x1140;
    regs.x[30] = 0x8040;
    UWND_ModuleInfo module = {.function_vaddr = 0x7000};
    U64 cfa = 0;
    UWND_StepResult step = uwnd_step(UWND_Unwinder_Null, Arch_arm64, &memory, &module, 0, &regs, &cfa);
    TestCheck(step.status == UWND_StepStatus_Good);
    TestCheck(regs.pc == 0x8040 && regs.sp == 0x1100 && regs.x[29] == 0x1140);
    TestCheck(cfa == 0x1100);
  }
}
