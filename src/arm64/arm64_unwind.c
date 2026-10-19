// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Recover the live top frame before FP establishment, through its return
// epilogue, or in a leaf which omits FP. Otherwise replay a straight-line prologue.
// Unknown control flow or dynamic SP arithmetic is left to CFI/the frame chain.
internal UWND_StepResult
arm64_uwnd_step_leaf(MemoryMap *memory_map, U64 function_vaddr, ARM64_RegBlock *regs)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
  if(function_vaddr == 0 || function_vaddr > regs->pc || regs->pc - function_vaddr > 4096 || ((function_vaddr | regs->pc) & 3))
  {
    return result;
  }
  if(regs->pc != function_vaddr)
  {
    // A return already has the caller's live LR and SP. A pending SP add or
    // pointer authentication can be handled without replaying executed calls.
    // Reconstructed frames never enter this helper with a function start.
    U64 tail_pc = regs->pc;
    U64 caller_sp = regs->sp;
    B32 may_authenticate = 1;
    for(U64 idx = 0; idx < 3; idx += 1)
    {
      if(tail_pc > max_U64 - 4) { return result; }
      U32 op = 0;
      Rng1U64 tail_range = r1u64(tail_pc, tail_pc + 4);
      if(memory_map_read(memory_map, tail_range, &op) != sizeof(op))
      {
        result.status = UWND_StepStatus_FailedMemoryRead;
        result.missed_read_vaddr_range = tail_range;
        return result;
      }
      if(idx == 0 && (op & 0xff8003ff) == 0x910003ff) // ADD SP,SP,#frame_size
      {
        U64 amount = ((op >> 10) & 4095ull) << ((op & (1u << 22)) ? 12 : 0);
        if(caller_sp > max_U64 - amount) { return result; }
        caller_sp += amount;
        tail_pc += 4;
        continue;
      }
      if(may_authenticate && (op == 0xd50323bf || op == 0xd50323ff))
      {
        may_authenticate = 0;
        tail_pc += 4;
        continue;
      }
      U64 lr = arm64_address_from_pointer(regs->x[30]);
      if((op == 0xd65f03c0 || op == 0xd65f0bff || op == 0xd65f0fff) &&
         lr != 0 && lr != regs->pc && !(lr & 3) && !(caller_sp & 15))
      {
        regs->pc = lr;
        regs->sp = caller_sp;
        regs->x[30] = 0;
        result.status = UWND_StepStatus_Good;
        return result;
      }
      break;
    }
  }
  U32 code[1024];
  U64 code_size = regs->pc - function_vaddr;
  Rng1U64 range = r1u64(function_vaddr, regs->pc);
  if(code_size != 0 && memory_map_read(memory_map, range, code) != code_size)
  {
    result.status = UWND_StepStatus_FailedMemoryRead;
    result.missed_read_vaddr_range = range;
    return result;
  }
  S64 sp_delta = 0;
  B32 established_fp = 0;
  for(U64 idx = 0; idx < code_size/4; idx += 1)
  {
    U32 op = code[idx];
    // A branch may have skipped instructions or overwritten LR: do not guess.
    if((op & 0x7c000000) == 0x14000000 || (op & 0xff000010) == 0x54000000 ||
       (op & 0x7e000000) == 0x34000000 || (op & 0x7e000000) == 0x36000000 ||
       (op & 0xfe000000) == 0xd6000000)
    {
      return result;
    }
    U32 rd = op & 31;
    U32 rn = (op >> 5) & 31;
    if((op & 0x1f000000) == 0x11000000 && !(op & 0x20000000))
    {
      S64 imm = ((op >> 10) & 4095) << ((op & (1u << 22)) ? 12 : 0);
      if(op & (1u << 30)) {imm = -imm;}
      if(rd == 31)
      {
        if(rn != 31) {return result;}
        sp_delta += imm;
      }
      if(rd == 29 && rn == 31) {established_fp = 1;}
    }
    else if((op & 0x1f200000) == 0x0b200000 && !(op & 0x20000000) && rd == 31)
    {
      return result;
    }
    // Integer/SIMD load/store pairs with SP writeback.
    if((op & 0x3a000000) == 0x28000000 && rn == 31)
    {
      U32 mode = (op >> 23) & 3;
      if(mode == 1 || mode == 3)
      {
        U32 scale = (op & (1u << 26)) ? 2 + (op >> 30) : 2 + ((op >> 31) & 1);
        S64 imm = ((S32)(op << 10) >> 25) * (1ll << scale);
        sp_delta += imm;
      }
      if((op & (1u << 22)) && !(op & (1u << 26)) && (rd == 29 || ((op >> 10) & 31) == 29))
      {
        established_fp = 0;
      }
    }
    // Single-register pre/post-index addressing.
    if((op & 0x3b200000) == 0x38000000 && rn == 31 && ((op >> 10) & 1))
    {
      sp_delta += (S32)(op << 11) >> 23;
      if((op & (1u << 22)) && !(op & (1u << 26)) && rd == 29) {established_fp = 0;}
    }
    if(sp_delta > 0 || sp_delta < -(S64)MB(64)) {return result;}
  }
  U64 lr = arm64_address_from_pointer(regs->x[30]);
  if(!established_fp && lr != 0 && lr != regs->pc && !(lr & 3) && regs->sp <= max_U64 - (U64)-sp_delta)
  {
    regs->sp += (U64)-sp_delta;
    regs->pc = lr;
    regs->x[30] = 0;
    result.status = UWND_StepStatus_Good;
  }
  return result;
}
