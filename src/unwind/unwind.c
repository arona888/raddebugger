// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Generated Code

#include "unwind/generated/unwind.meta.c"

////////////////////////////////
//~ rjf: Moduleless Unwinder

internal UWND_StepResult
uwnd_step_moduleless(Arch arch, MemoryMap *memory_map, void *regs)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
#if defined(ARM64_H)
  if(arch == Arch_arm64)
  {
    ARM64_RegBlock *r = (ARM64_RegBlock *)regs;
    U64 fp = arm64_address_from_pointer(r->x[29]);
    U64 sp = arm64_address_from_pointer(r->sp);
    if(fp == 0)
    {
      U64 lr = arm64_address_from_pointer(r->x[30]);
      if(lr == 0)
      {
        r->pc = 0;
        result.status = UWND_StepStatus_Good;
        return result;
      }
      if(lr != 0 && lr != r->pc && !(lr & 3))
      {
        r->pc = lr;
        r->x[30] = 0;
        result.status = UWND_StepStatus_Good;
      }
      return result;
    }
    // AAPCS64 frame records are aligned, above SP, and advance toward stack base.
    if((fp & 15) || (sp & 15) || fp < sp || fp - sp > MB(64) || fp > max_U64 - 16)
    {
      return result;
    }
    U64 frame[2] = {0};
    Rng1U64 range = r1u64(fp, fp + sizeof(frame));
    if(memory_map_read(memory_map, range, frame) != sizeof(frame))
    {
      result.status = UWND_StepStatus_FailedMemoryRead;
      result.missed_read_vaddr_range = range;
      return result;
    }
    U64 next_fp = arm64_address_from_pointer(frame[0]);
    U64 next_pc = arm64_address_from_pointer(frame[1]);
    if((next_fp != 0 && ((next_fp & 15) || next_fp <= fp || next_fp - fp > MB(64))) ||
       (next_pc & 3) || (next_pc == r->pc && fp + 16 <= sp))
    {
      return result;
    }
    r->x[29] = next_fp;
    r->x[30] = 0;
    r->sp = fp + 16;
    r->pc = next_pc;
    result.status = UWND_StepStatus_Good;
    return result;
  }
#endif
  {
    B32 done = 0;
    ARCH_Info *arch_info = arch_info_from_arch(arch);
    U64 addr_size = byte_size_from_arch(arch);
    
    // rjf: read new ip from stack pointer
    U64 sp = arch_sp_from_reg_block(arch_info, regs);
    U64 new_ip = 0;
    Rng1U64 new_ip_vaddr_range = r1u64(sp, sp+addr_size);
    if(memory_map_read(memory_map, new_ip_vaddr_range, &new_ip) != addr_size)
    {
      done = 1;
      result.status = UWND_StepStatus_FailedMemoryRead;
      result.missed_read_vaddr_range = new_ip_vaddr_range;
    }
    
    // rjf: commit
    if(!done)
    {
      U64 new_sp = sp+addr_size;
      arch_reg_block_write_ip(arch_info, regs, new_ip);
      arch_reg_block_write_sp(arch_info, regs, new_sp);
      result.status = UWND_StepStatus_Good;
    }
  }
  return result;
}

////////////////////////////////
//~ rjf: Abstracted Unwind Step Functions

internal UWND_StepResult
uwnd_step(UWND_Unwinder unwinder, Arch arch, MemoryMap *memory_map, UWND_ModuleInfo *module_info, U64 tls_vaddr, void *regs, U64 *cfa_out)
{
  UWND_StepResult result = {UWND_StepStatus_Error};
#if defined(ARM64_H)
  ARM64_RegBlock original_arm64_regs;
  if(arch == Arch_arm64) {MemoryCopyStruct(&original_arm64_regs, (ARM64_RegBlock *)regs);}
#endif
  switch(unwinder)
  {
    default:{if(arch != Arch_arm64) {result = uwnd_step_moduleless(arch, memory_map, regs);}}break;
#define X(name, namespace_lower) case UWND_Unwinder_##name:{result = namespace_lower##_uwnd_step(arch, memory_map, module_info, tls_vaddr, regs, cfa_out);}break;
    UWND_Unwinder_XList
#undef X
  }
#if defined(ARM64_H)
  if(arch == Arch_arm64 && result.status == UWND_StepStatus_Error)
  {
    MemoryCopyStruct((ARM64_RegBlock *)regs, &original_arm64_regs);
    if(module_info != 0 && module_info->function_vaddr != 0)
    {
      result = arm64_uwnd_step_leaf(memory_map, module_info->function_vaddr, (ARM64_RegBlock *)regs);
    }
    if(result.status == UWND_StepStatus_Error)
    {
      result = uwnd_step_moduleless(arch, memory_map, regs);
    }
  }
#endif
  if(result.status == UWND_StepStatus_Good && cfa_out != 0 && (unwinder == UWND_Unwinder_Null || arch == Arch_arm64))
  {
    *cfa_out = arch_sp_from_reg_block(arch_info_from_arch(arch), regs);
  }
  return result;
}
